/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_server.h"

#include "core/version.h"
#include "settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <deque>

namespace FlashGram::Server {
namespace {

// Public client configuration, safe to ship inside the executable.
// The publishable key only reaches what the database grants to the anon
// role: the fg_* RPC functions. Tables have RLS enabled, no client
// policies and no table privileges, so they can't be read or changed
// directly with this key.
constexpr auto kProjectUrl = "https://jnyvpntymhhgxpmlvwhp.supabase.co"_cs;
constexpr auto kPublishableKey
	= "sb_publishable_autMLdNxjVbLWWrcYRY_rg_gN1Jg1Fd"_cs;
constexpr auto kReleasesPage
	= "https://github.com/CriminalFlower/FlashGram/releases"_cs;
constexpr auto kEmojiSetPrefix = "https://t.me/"_cs;
constexpr auto kDefaultEmojiSetUrl = "https://t.me/addemoji/theopenemojis"_cs;
constexpr auto kDefaultEmojiPreviewId = uint64(5433776470080107054ULL);
constexpr auto kRequestTimeoutMs = 10000;
constexpr auto kAccountRetryDelay = crl::time(30000);
constexpr auto kRateLimitBackoff = crl::time(60000);
constexpr auto kMaxRateLimitBackoff = crl::time(15 * 60000);
constexpr auto kFailureBackoffBase = crl::time(2000);
constexpr auto kMaxFailureBackoff = crl::time(120000);
constexpr auto kFailuresBeforeBackoff = 3;
constexpr auto kClientBudgetWindow = crl::time(60000);
constexpr auto kClientBudgetTotal = 120;
constexpr auto kClientBudgetPerCall = 30;
constexpr auto kMaxReplySize = qint64(4 * 1024 * 1024);
constexpr auto kVerificationRefreshDelay = crl::time(3000);
constexpr auto kDirectoryCacheLifetime = crl::time(30000);
constexpr auto kOrgCacheLifetime = crl::time(60000);
constexpr auto kTelegramNftPrefix = "https://t.me/nft/"_cs;
constexpr auto kFragmentPrefix = "https://fragment.com/"_cs;

struct State {
	rpl::variable<Status> status = Status::Checking;
	rpl::event_stream<> configUpdates;
	rpl::event_stream<> accountUpdates;
	bool configRequested = false;
	bool configLoaded = false;
	bool maintenance = false;
	bool emailRegistration = false;
	QString serverMessage;
	QString minimumClientVersion;
	std::vector<EmojiSet> emojiSets;
	std::optional<Release> latest;
	base::flat_map<uint64, Account> accounts;
	base::flat_map<uint64, std::vector<Fn<void(Account)>>> pending;
	base::flat_set<uint64> inflight;
	base::flat_map<uint64, crl::time> failedAt;
	crl::time rateLimitedUntil = 0;
	crl::time failuresUntil = 0;
	int consecutiveFailures = 0;
	std::deque<crl::time> sentTotal;
	base::flat_map<QString, std::deque<crl::time>> sentPerCall;
	rpl::event_stream<> verificationUpdates;
	base::flat_map<uint64, Verification> verifications;
	base::flat_map<uint64, QString> verificationErrors;
	base::flat_map<uint64, crl::time> verificationLoadedAt;
	base::flat_set<uint64> verificationInflight;
	base::flat_map<QString, std::pair<crl::time, std::vector<DirectoryEntry>>> directory;
	base::flat_map<uint64, std::pair<crl::time, std::vector<VerificationBadge>>> orgBadges;
	std::vector<StoreEntry> nftStore;
};

[[nodiscard]] State &GetState() {
	static auto result = State();
	return result;
}

[[nodiscard]] QNetworkAccessManager *Network() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return result;
}

using HttpDone = Fn<void(int code, std::optional<QJsonValue> value)>;

// Client side flood guard: the client never sends more than a fixed
// budget of requests per minute (in total and per function), stays quiet
// after 429 / 503 for as long as the server asks, and backs off
// exponentially while the server keeps failing. A bug or a stuck loop in
// the client can't turn every installation into a load source.
[[nodiscard]] bool TakeBudget(std::deque<crl::time> &sent, int limit) {
	const auto now = crl::now();
	while (!sent.empty() && sent.front() <= now - kClientBudgetWindow) {
		sent.pop_front();
	}
	if (int(sent.size()) >= limit) {
		return false;
	}
	sent.push_back(now);
	return true;
}

[[nodiscard]] bool AllowRequest(const QString &label) {
	auto &state = GetState();
	const auto now = crl::now();
	if (now < state.rateLimitedUntil || now < state.failuresUntil) {
		return false;
	}
	auto &perCall = state.sentPerCall[label];
	if (int(perCall.size()) >= kClientBudgetPerCall
		&& perCall.front() > now - kClientBudgetWindow) {
		return false;
	}
	if (!TakeBudget(state.sentTotal, kClientBudgetTotal)) {
		LOG(("FlashGram Server: client request budget exhausted."));
		return false;
	}
	return TakeBudget(perCall, kClientBudgetPerCall);
}

[[nodiscard]] crl::time RetryAfter(not_null<QNetworkReply*> reply) {
	const auto header = reply->rawHeader("Retry-After").trimmed();
	auto ok = false;
	const auto seconds = header.toLongLong(&ok);
	return (ok && seconds > 0)
		? std::min(crl::time(seconds) * 1000, kMaxRateLimitBackoff)
		: kRateLimitBackoff;
}

void TrackResult(int code, not_null<QNetworkReply*> reply) {
	auto &state = GetState();
	if (code == 429 || code == 503) {
		state.rateLimitedUntil = crl::now() + RetryAfter(reply);
	}
	const auto failed = !code || code >= 500;
	if (!failed) {
		state.consecutiveFailures = 0;
		state.failuresUntil = 0;
		return;
	}
	if (++state.consecutiveFailures < kFailuresBeforeBackoff) {
		return;
	}
	const auto shift = std::min(
		state.consecutiveFailures - kFailuresBeforeBackoff,
		6);
	state.failuresUntil = crl::now() + std::min(
		kFailureBackoffBase << shift,
		kMaxFailureBackoff);
}

void CallHttp(
		const QString &path,
		const QJsonObject &arguments,
		const QByteArray &bearer,
		const QString &label,
		HttpDone done) {
	// After the server answered 429 the client stays quiet for a while
	// instead of retrying, so a flooded server isn't hit even harder.
	// Auth one-time codes have their own per-email interval, so a 429
	// there never pauses the rest of the FlashGram API.
	const auto authRequest = path.startsWith(u"/auth/"_q);
	if (!authRequest && !AllowRequest(label)) {
		crl::on_main([=] {
			done(429, std::nullopt);
		});
		return;
	}
	auto request = QNetworkRequest(QUrl(kProjectUrl.utf16() + path));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("apikey", kPublishableKey.utf8());
	if (!bearer.isEmpty()) {
		request.setRawHeader("Authorization", "Bearer " + bearer);
	}
	request.setTransferTimeout(kRequestTimeoutMs);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto body = QJsonDocument(arguments).toJson(
		QJsonDocument::Compact);
	const auto reply = Network()->post(request, body);
	QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [=](
			qint64 received,
			qint64 total) {
		if (received > kMaxReplySize || total > kMaxReplySize) {
			LOG(("FlashGram Server: %1 reply is too large.").arg(label));
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		reply->deleteLater();
		const auto code = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto bytes = reply->read(kMaxReplySize);

		// PostgREST returns a bare JSON value (object or null), which
		// Qt 5 can't parse as a document, so it is wrapped in an array.
		auto error = QJsonParseError();
		const auto document = QJsonDocument::fromJson(
			'[' + bytes + ']',
			&error);
		const auto parsed = (error.error == QJsonParseError::NoError
			&& document.isArray()
			&& document.array().size() == 1);
		if (!authRequest) {
			TrackResult(code, reply);
		}
		if (reply->error() != QNetworkReply::NoError || code != 200) {
			LOG(("FlashGram Server: %1 failed, http %2, network error %3."
				).arg(label
				).arg(code
				).arg(int(reply->error())));
		}
		done(
			code,
			parsed
				? std::make_optional(document.array().at(0))
				: std::nullopt);
	});
}

void CallRpc(
		const QString &function,
		const QJsonObject &arguments,
		Fn<void(std::optional<QJsonValue>)> done,
		const QByteArray &bearer = QByteArray()) {
	CallHttp(
		u"/rest/v1/rpc/"_q + function,
		arguments,
		bearer,
		function,
		[=](int code, std::optional<QJsonValue> value) {
			done((code == 200) ? value : std::nullopt);
		});
}

[[nodiscard]] std::vector<EmojiSet> DefaultEmojiSets() {
	return { EmojiSet{
		.title = u"The Open Emojis"_q,
		.url = kDefaultEmojiSetUrl.utf16(),
		.description = QString(),
		.icon = QString(),
		.previewDocumentId = kDefaultEmojiPreviewId,
	} };
}

[[nodiscard]] bool IsTrustedEmojiSetUrl(const QString &url) {
	return url.startsWith(kEmojiSetPrefix.utf16())
		&& QUrl(url).isValid();
}

void ApplyConfig(const QJsonObject &object) {
	auto &state = GetState();
	const auto config = object.value(u"config"_q).toObject();

	state.maintenance = config.value(u"maintenance_mode"_q).toBool();
	state.emailRegistration = config.value(
		u"email_registration"_q).toBool(false);
	state.serverMessage = config.value(u"server_message"_q).toString();
	state.minimumClientVersion = config.value(
		u"minimum_client_version"_q).toString();

	state.emojiSets.clear();
	for (const auto &entry : config.value(u"emoji_sets"_q).toArray()) {
		const auto set = entry.toObject();
		auto parsed = EmojiSet{
			.title = set.value(u"title"_q).toString().trimmed(),
			.url = set.value(u"url"_q).toString().trimmed(),
			.description = set.value(u"description"_q).toString(),
			.icon = set.value(u"icon"_q).toString(),
			.previewDocumentId = set.value(
				u"preview_document_id"_q).toVariant().toULongLong(),
		};
		if (!parsed.title.isEmpty() && IsTrustedEmojiSetUrl(parsed.url)) {
			state.emojiSets.push_back(std::move(parsed));
		}
	}

	state.nftStore.clear();
	for (const auto &entry : config.value(u"nft_store"_q).toArray()) {
		const auto item = entry.toObject();
		auto parsed = StoreEntry{
			.title = item.value(u"title"_q).toString().trimmed(),
			.rarity = item.value(u"rarity"_q).toString().trimmed(),
			.priceText = item.value(u"price_text"_q).toString().trimmed(),
			.url = item.value(u"url"_q).toString().trimmed(),
		};
		if (!parsed.title.isEmpty()) {
			state.nftStore.push_back(std::move(parsed));
		}
	}

	state.latest = std::nullopt;
	const auto latest = object.value(u"latest_release"_q).toObject();
	auto release = Release{
		.version = latest.value(u"version"_q).toString(),
		.minimumSupportedVersion = latest.value(
			u"minimum_supported_version"_q).toString(),
		.downloadUrl = latest.value(u"download_url"_q).toString(),
		.notes = latest.value(u"release_notes"_q).toString(),
	};
	if (!release.version.isEmpty()
		&& IsTrustedReleaseUrl(release.downloadUrl)) {
		state.latest = std::move(release);
	}
	state.configLoaded = true;
}

void RequestConfig() {
	auto &state = GetState();
	if (state.configRequested) {
		return;
	}
	state.configRequested = true;
	state.status = Status::Checking;
	CallRpc(u"fg_client_config"_q, {}, [](std::optional<QJsonValue> result) {
		auto &state = GetState();
		state.configRequested = false;
		if (!result || !result->isObject()) {
			state.status = Status::Offline;
			return;
		}
		ApplyConfig(result->toObject());
		state.status = Status::Online;
		state.configUpdates.fire({});
	});
}

[[nodiscard]] QString AccountPath(uint64 telegramUserId) {
	return cWorkingDir()
		+ u"tdata/flashgram/server/"_q
		+ QString::number(telegramUserId)
		+ u".json"_q;
}

[[nodiscard]] QString ReadAccountToken(uint64 telegramUserId) {
	auto file = QFile(AccountPath(telegramUserId));
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	return document.object().value(u"token"_q).toString();
}

void WriteAccountToken(
		uint64 telegramUserId,
		const QString &token,
		const QString &flashgramId) {
	const auto path = AccountPath(telegramUserId);
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("FlashGram Server: could not store the account token."));
		return;
	}
	auto object = QJsonObject();
	object.insert(u"token"_q, token);
	object.insert(u"flashgram_id"_q, flashgramId);
	file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	file.commit();
}

[[nodiscard]] TimeId ParseTime(const QString &value) {
	static const auto kFraction = QRegularExpression(u"\\.\\d+"_q);
	auto normalized = value;
	normalized.remove(kFraction);
	const auto parsed = QDateTime::fromString(normalized, Qt::ISODate);
	return parsed.isValid() ? TimeId(parsed.toSecsSinceEpoch()) : TimeId();
}

[[nodiscard]] ServerGift ParseGift(const QJsonObject &object) {
	return {
		.id = object.value(u"id"_q).toString(),
		.definitionId = object.value(u"definition_id"_q).toString(),
		.number = object.value(u"unique_number"_q).toInt(),
		.acquiredAt = ParseTime(object.value(u"acquired_at"_q).toString()),
		.pinned = object.value(u"pinned"_q).toBool(),
		.inProfile = object.value(u"displayed_in_profile"_q).toBool(),
	};
}

[[nodiscard]] Account ParseAccount(const QJsonObject &object) {
	auto result = Account{
		.flashgramId = object.value(u"flashgram_id"_q).toString(),
		.email = object.value(u"email"_q).toString(),
		.balanceFg = int64(object.value(u"balance_fg"_q).toDouble()),
	};
	for (const auto &gift : object.value(u"gifts"_q).toArray()) {
		result.gifts.push_back(ParseGift(gift.toObject()));
	}
	return result;
}

void FinishAccount(uint64 telegramUserId, std::optional<Account> account) {
	auto &state = GetState();
	state.inflight.remove(telegramUserId);
	auto callbacks = std::vector<Fn<void(Account)>>();
	if (const auto i = state.pending.find(telegramUserId)
		; i != end(state.pending)) {
		callbacks = std::move(i->second);
		state.pending.erase(i);
	}
	if (!account || account->flashgramId.isEmpty()) {
		state.failedAt[telegramUserId] = crl::now();
		return;
	}
	state.failedAt.remove(telegramUserId);
	state.accounts[telegramUserId] = *account;
	state.accountUpdates.fire({});
	for (const auto &callback : callbacks) {
		callback(*account);
	}
}

void RegisterAccount(uint64 telegramUserId) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_telegram_user_id"_q, double(telegramUserId));
	CallRpc(u"fg_register"_q, arguments, [=](
			std::optional<QJsonValue> result) {
		if (!result || !result->isObject()) {
			FinishAccount(telegramUserId, std::nullopt);
			return;
		}
		const auto object = result->toObject();
		const auto token = object.value(u"token"_q).toString();
		auto account = ParseAccount(object);
		if (token.isEmpty() || account.flashgramId.isEmpty()) {
			FinishAccount(telegramUserId, std::nullopt);
			return;
		}
		WriteAccountToken(telegramUserId, token, account.flashgramId);
		FinishAccount(telegramUserId, std::move(account));
	});
}

void FetchAccount(uint64 telegramUserId) {
	const auto token = ReadAccountToken(telegramUserId);
	if (token.isEmpty()) {
		RegisterAccount(telegramUserId);
		return;
	}
	auto arguments = QJsonObject();
	arguments.insert(u"p_token"_q, token);
	CallRpc(u"fg_me"_q, arguments, [=](std::optional<QJsonValue> result) {
		if (!result) {
			FinishAccount(telegramUserId, std::nullopt);
		} else if (result->isNull()) {
			RegisterAccount(telegramUserId);
		} else {
			FinishAccount(telegramUserId, ParseAccount(result->toObject()));
		}
	});
}

void CallAccountRpc(
		uint64 telegramUserId,
		const QString &function,
		QJsonObject arguments,
		Fn<void(ActionResult)> done) {
	const auto token = ReadAccountToken(telegramUserId);
	if (token.isEmpty()) {
		done({ .error = u"no_account"_q });
		return;
	}
	arguments.insert(u"p_token"_q, token);
	CallRpc(function, arguments, [=](std::optional<QJsonValue> value) {
		if (!value) {
			done({ .error = u"network"_q });
			return;
		}
		const auto object = value->toObject();
		auto result = ActionResult();
		result.error = object.value(u"error"_q).toString();
		if (object.contains(u"gift"_q)) {
			result.gift = ParseGift(object.value(u"gift"_q).toObject());
		}
		if (object.contains(u"balance_fg"_q)) {
			result.balanceFg = int64(
				object.value(u"balance_fg"_q).toDouble());
		}
		done(std::move(result));
	});
}

void ApplyToCache(
		uint64 telegramUserId,
		const ActionResult &result,
		const QString &removedGiftId) {
	auto &state = GetState();
	const auto i = state.accounts.find(telegramUserId);
	if (i == end(state.accounts)) {
		return;
	}
	auto &account = i->second;
	if (result.balanceFg >= 0) {
		account.balanceFg = result.balanceFg;
	}
	if (result.gift) {
		account.gifts.insert(begin(account.gifts), *result.gift);
	}
	if (!removedGiftId.isEmpty()) {
		account.gifts.erase(
			ranges::remove(account.gifts, removedGiftId, &ServerGift::id),
			end(account.gifts));
	}
	state.accountUpdates.fire({});
}

using ObjectDone = Fn<void(QString error, QJsonObject object)>;

void CallObjectRpc(
		const QString &function,
		const QJsonObject &arguments,
		ObjectDone done) {
	CallHttp(
		u"/rest/v1/rpc/"_q + function,
		arguments,
		QByteArray(),
		function,
		[=](int code, std::optional<QJsonValue> value) {
			if (code == 429) {
				done(u"rate_limited"_q, {});
			} else if (code != 200 || !value || !value->isObject()) {
				done(u"network"_q, {});
			} else {
				const auto object = value->toObject();
				done(object.value(u"error"_q).toString(), object);
			}
		});
}

void CallTokenObjectRpc(
		uint64 telegramUserId,
		const QString &function,
		QJsonObject arguments,
		ObjectDone done) {
	const auto token = ReadAccountToken(telegramUserId);
	if (token.isEmpty()) {
		done(u"no_account"_q, {});
		return;
	}
	arguments.insert(u"p_token"_q, token);
	CallObjectRpc(function, arguments, std::move(done));
}

[[nodiscard]] VerificationRequest ParseVerificationRequest(
		const QJsonObject &object) {
	return {
		.id = object.value(u"id"_q).toString(),
		.type = object.value(u"type"_q).toString(),
		.level = object.value(u"level"_q).toString(),
		.status = object.value(u"status"_q).toString(),
		.displayName = object.value(u"display_name"_q).toString(),
		.username = object.value(u"username"_q).toString(),
		.target = object.value(u"target"_q).toString(),
		.flashgramId = object.value(u"flashgram_id"_q).toString(),
		.reason = object.value(u"reason"_q).toString(),
		.createdAt = ParseTime(object.value(u"created_at"_q).toString()),
		.reviewedAt = ParseTime(object.value(u"reviewed_at"_q).toString()),
		.publicListing = object.value(u"public_listing"_q).toBool(),
	};
}

[[nodiscard]] VerificationBadge ParseBadge(const QJsonObject &badge) {
	return {
		.type = badge.value(u"verification_type"_q).toString(),
		.grantedAt = ParseTime(badge.value(u"issued_at"_q).toString()),
		.title = badge.value(u"verification_title"_q).toString(),
		.description = badge.value(u"verification_description"_q).toString(),
		.issuer = badge.value(u"issuer"_q).toString(),
		.expiresAt = ParseTime(badge.value(u"expires_at"_q).toString()),
	};
}

[[nodiscard]] Verification ParseVerification(const QJsonObject &object) {
	auto result = Verification{
		.admin = object.value(u"is_admin"_q).toBool(),
		.emailLinked = object.value(u"email_linked"_q).toBool(),
		.violations = object.value(u"violations"_q).toBool(),
		.emailRequired = object.value(u"email_required"_q).toBool(true),
	};
	for (const auto &value : object.value(u"requests"_q).toArray()) {
		result.requests.push_back(ParseVerificationRequest(value.toObject()));
	}
	for (const auto &value : object.value(u"badges"_q).toArray()) {
		result.badges.push_back(ParseBadge(value.toObject()));
	}
	return result;
}

[[nodiscard]] QString AuthError(
		int code,
		const std::optional<QJsonValue> &value,
		const QString &fallback) {
	if (!code) {
		return u"network"_q;
	} else if (code == 429) {
		return u"rate_limited"_q;
	}
	const auto object = value ? value->toObject() : QJsonObject();
	const auto errorCode = object.value(u"error_code"_q).toString();
	if (errorCode == u"otp_expired"_q
		|| errorCode == u"invalid_credentials"_q) {
		return u"invalid_code"_q;
	} else if (errorCode == u"validation_failed"_q
		|| errorCode == u"email_address_invalid"_q) {
		return u"invalid_email"_q;
	} else if (errorCode.contains(u"rate_limit"_q)) {
		return u"rate_limited"_q;
	} else if (!errorCode.isEmpty()) {
		LOG(("FlashGram Server: auth error %1.").arg(errorCode));
	}
	return fallback;
}

} // namespace

QString ClientVersion() {
	return FlashGramVersionStr.utf16();
}

int CompareVersions(const QString &a, const QString &b) {
	const auto parts = [](const QString &version) {
		auto result = std::vector<int>();
		for (const auto &part : version.split('.')) {
			auto digits = QString();
			for (const auto ch : part) {
				if (!ch.isDigit()) {
					break;
				}
				digits.append(ch);
			}
			result.push_back(digits.toInt());
		}
		return result;
	};
	const auto left = parts(a);
	const auto right = parts(b);
	const auto count = std::max(left.size(), right.size());
	for (auto i = 0; i != int(count); ++i) {
		const auto l = (i < int(left.size())) ? left[i] : 0;
		const auto r = (i < int(right.size())) ? right[i] : 0;
		if (l != r) {
			return (l < r) ? -1 : 1;
		}
	}
	return 0;
}

void Start() {
	if (!GetState().configLoaded) {
		RequestConfig();
	}
}

void RefreshHealth() {
	auto &state = GetState();
	if (!state.configLoaded) {
		RequestConfig();
		return;
	}
	CallRpc(u"fg_health"_q, {}, [](std::optional<QJsonValue> result) {
		const auto object = result ? result->toObject() : QJsonObject();
		GetState().status = (object.value(u"status"_q).toString()
			== u"ok"_q)
			? Status::Online
			: Status::Offline;
	});
}

Status CurrentStatus() {
	return GetState().status.current();
}

rpl::producer<Status> StatusValue() {
	return GetState().status.value();
}

rpl::producer<> ConfigUpdates() {
	return GetState().configUpdates.events();
}

bool ConfigLoaded() {
	return GetState().configLoaded;
}

bool MaintenanceMode() {
	return GetState().maintenance;
}

bool EmailRegistrationEnabled() {
	return GetState().emailRegistration;
}

QString ServerMessage() {
	return GetState().serverMessage;
}

std::vector<EmojiSet> EmojiSets() {
	const auto &sets = GetState().emojiSets;
	return sets.empty() ? DefaultEmojiSets() : sets;
}

std::optional<Release> AvailableUpdate() {
	const auto &latest = GetState().latest;
	if (!latest || CompareVersions(latest->version, ClientVersion()) <= 0) {
		return std::nullopt;
	}
	return latest;
}

bool UpdateRequired() {
	const auto &state = GetState();
	auto minimum = state.minimumClientVersion;
	if (state.latest
		&& CompareVersions(state.latest->minimumSupportedVersion, minimum) > 0) {
		minimum = state.latest->minimumSupportedVersion;
	}
	return !minimum.isEmpty()
		&& (CompareVersions(ClientVersion(), minimum) < 0);
}

bool IsTrustedReleaseUrl(const QString &url) {
	return url.startsWith(kReleasesPage.utf16())
		&& QUrl(url).isValid();
}

QString ReleasesPageUrl() {
	return kReleasesPage.utf16();
}

void RequestAccount(uint64 telegramUserId, Fn<void(Account)> done) {
	auto &state = GetState();
	if (!telegramUserId) {
		return;
	}
	if (const auto i = state.accounts.find(telegramUserId)
		; i != end(state.accounts)) {
		done(i->second);
		return;
	}
	if (const auto i = state.failedAt.find(telegramUserId)
		; i != end(state.failedAt)
		&& crl::now() - i->second < kAccountRetryDelay) {
		return;
	}
	state.pending[telegramUserId].push_back(std::move(done));
	if (!state.inflight.contains(telegramUserId)) {
		state.inflight.emplace(telegramUserId);
		FetchAccount(telegramUserId);
	}
}

void RefreshAccount(uint64 telegramUserId) {
	auto &state = GetState();
	if (telegramUserId && !state.inflight.contains(telegramUserId)) {
		state.inflight.emplace(telegramUserId);
		FetchAccount(telegramUserId);
	}
}

const Account *CachedAccount(uint64 telegramUserId) {
	const auto &accounts = GetState().accounts;
	const auto i = accounts.find(telegramUserId);
	return (i != end(accounts)) ? &i->second : nullptr;
}

rpl::producer<> AccountUpdates() {
	return GetState().accountUpdates.events();
}

void OpenCase(
		uint64 telegramUserId,
		const QString &caseId,
		Fn<void(ActionResult)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_case_id"_q, caseId);
	CallAccountRpc(telegramUserId, u"fg_open_case"_q, arguments, [=](
			ActionResult result) {
		if (result.error.isEmpty()) {
			ApplyToCache(telegramUserId, result, QString());
		}
		done(std::move(result));
	});
}

void SpinRoulette(uint64 telegramUserId, Fn<void(ActionResult)> done) {
	CallAccountRpc(telegramUserId, u"fg_spin_roulette"_q, {}, [=](
			ActionResult result) {
		if (result.error.isEmpty()) {
			ApplyToCache(telegramUserId, result, QString());
		}
		done(std::move(result));
	});
}

void SellGift(
		uint64 telegramUserId,
		const QString &giftId,
		Fn<void(ActionResult)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_gift_id"_q, giftId);
	CallAccountRpc(telegramUserId, u"fg_sell_gift"_q, arguments, [=](
			ActionResult result) {
		if (result.error.isEmpty()) {
			ApplyToCache(telegramUserId, result, giftId);
		}
		done(std::move(result));
	});
}

void TransferGift(
		uint64 telegramUserId,
		const QString &giftId,
		const QString &recipientFlashGramId,
		Fn<void(ActionResult)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_gift_id"_q, giftId);
	arguments.insert(u"p_to_flashgram_id"_q, recipientFlashGramId);
	CallAccountRpc(telegramUserId, u"fg_transfer_gift"_q, arguments, [=](
			ActionResult result) {
		if (result.error.isEmpty()) {
			ApplyToCache(telegramUserId, result, giftId);
		}
		done(std::move(result));
	});
}

void SetGiftFlags(
		uint64 telegramUserId,
		const QString &giftId,
		std::optional<bool> pinned,
		std::optional<bool> inProfile) {
	auto &state = GetState();
	if (const auto i = state.accounts.find(telegramUserId)
		; i != end(state.accounts)) {
		for (auto &gift : i->second.gifts) {
			if (gift.id == giftId) {
				gift.pinned = pinned.value_or(gift.pinned);
				gift.inProfile = inProfile.value_or(gift.inProfile);
			}
		}
		state.accountUpdates.fire({});
	}
	const auto token = ReadAccountToken(telegramUserId);
	if (token.isEmpty()) {
		return;
	}
	auto arguments = QJsonObject();
	arguments.insert(u"p_token"_q, token);
	arguments.insert(u"p_gift_id"_q, giftId);
	arguments.insert(
		u"p_pinned"_q,
		pinned ? QJsonValue(*pinned) : QJsonValue(QJsonValue::Null));
	arguments.insert(
		u"p_displayed_in_profile"_q,
		inProfile ? QJsonValue(*inProfile) : QJsonValue(QJsonValue::Null));
	CallRpc(u"fg_set_gift_flags"_q, arguments, [=](
			std::optional<QJsonValue> result) {
		if (!result || !result->toBool()) {
			RefreshAccount(telegramUserId);
		}
	});
}

void RequestVerification(uint64 telegramUserId, bool force) {
	auto &state = GetState();
	if (!telegramUserId || state.verificationInflight.contains(telegramUserId)) {
		return;
	}
	if (const auto i = state.verificationLoadedAt.find(telegramUserId)
		; !force
		&& i != end(state.verificationLoadedAt)
		&& crl::now() - i->second < kVerificationRefreshDelay) {
		return;
	}
	if (ReadAccountToken(telegramUserId).isEmpty()) {
		RequestAccount(telegramUserId, [=](Account) {
			RequestVerification(telegramUserId, true);
		});
		return;
	}
	state.verificationInflight.emplace(telegramUserId);
	CallTokenObjectRpc(
		telegramUserId,
		u"fg_verification_status"_q,
		{},
		[=](QString error, QJsonObject object) {
			auto &state = GetState();
			state.verificationInflight.remove(telegramUserId);
			if (error.isEmpty()) {
				state.verificationErrors.remove(telegramUserId);
				state.verifications[telegramUserId] = ParseVerification(object);
				state.verificationLoadedAt[telegramUserId] = crl::now();
			} else {
				state.verificationErrors[telegramUserId] = error;
			}
			state.verificationUpdates.fire({});
		});
}

const Verification *CachedVerification(uint64 telegramUserId) {
	const auto &verifications = GetState().verifications;
	const auto i = verifications.find(telegramUserId);
	return (i != end(verifications)) ? &i->second : nullptr;
}

QString VerificationError(uint64 telegramUserId) {
	const auto &errors = GetState().verificationErrors;
	const auto i = errors.find(telegramUserId);
	return (i != end(errors)) ? i->second : QString();
}

rpl::producer<> VerificationUpdates() {
	return GetState().verificationUpdates.events();
}

void SubmitVerification(
		uint64 telegramUserId,
		VerificationSubmit request,
		Fn<void(QString error)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_type"_q, request.type);
	arguments.insert(u"p_level"_q, request.level);
	arguments.insert(u"p_display_name"_q, request.displayName);
	arguments.insert(u"p_username"_q, request.username);
	arguments.insert(u"p_target"_q, request.target);
	arguments.insert(u"p_public_listing"_q, request.publicListing);
	CallTokenObjectRpc(
		telegramUserId,
		u"fg_verification_submit"_q,
		arguments,
		[=](QString error, QJsonObject) {
			if (error.isEmpty()) {
				RequestVerification(telegramUserId, true);
			}
			done(error);
		});
}

void SetVerificationListing(
		uint64 telegramUserId,
		const QString &type,
		bool publicListing,
		Fn<void(QString error)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_type"_q, type);
	arguments.insert(u"p_public"_q, publicListing);
	CallTokenObjectRpc(
		telegramUserId,
		u"fg_verification_set_listing"_q,
		arguments,
		[=](QString error, QJsonObject) {
			GetState().directory.clear();
			RequestVerification(telegramUserId, true);
			done(error);
		});
}

void RequestDirectory(
		const QString &category,
		Fn<void(QString error, std::vector<DirectoryEntry>)> done) {
	auto &state = GetState();
	if (const auto i = state.directory.find(category)
		; i != end(state.directory)
		&& crl::now() - i->second.first < kDirectoryCacheLifetime) {
		done(QString(), i->second.second);
		return;
	}
	auto arguments = QJsonObject();
	arguments.insert(
		u"p_category"_q,
		category.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(category));
	arguments.insert(u"p_limit"_q, 50);
	CallObjectRpc(
		u"fg_verified_directory"_q,
		arguments,
		[=](QString error, QJsonObject object) {
			auto entries = std::vector<DirectoryEntry>();
			if (error.isEmpty()) {
				for (const auto &value : object.value(u"entries"_q).toArray()) {
					const auto entry = value.toObject();
					entries.push_back({
						.displayName = entry.value(u"display_name"_q).toString(),
						.username = entry.value(u"username"_q).toString(),
						.type = entry.value(u"type"_q).toString(),
						.level = entry.value(u"level"_q).toString(),
						.verifiedAt = ParseTime(
							entry.value(u"verified_at"_q).toString()),
						.badges = entry.value(u"badges"_q).toInt(),
					});
				}
				GetState().directory[category] = { crl::now(), entries };
			}
			done(error, std::move(entries));
		});
}

void RequestVerificationQueue(
		uint64 telegramUserId,
		const QString &status,
		Fn<void(QString error, std::vector<VerificationRequest>)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_status"_q, status);
	CallTokenObjectRpc(
		telegramUserId,
		u"fg_verification_admin_list"_q,
		arguments,
		[=](QString error, QJsonObject object) {
			auto requests = std::vector<VerificationRequest>();
			if (error.isEmpty()) {
				for (const auto &value : object.value(u"requests"_q).toArray()) {
					requests.push_back(
						ParseVerificationRequest(value.toObject()));
				}
			}
			done(error, std::move(requests));
		});
}

void ReviewVerification(
		uint64 telegramUserId,
		const QString &requestId,
		const QString &action,
		const QString &reason,
		Fn<void(QString error)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_request_id"_q, requestId);
	arguments.insert(u"p_action"_q, action);
	arguments.insert(u"p_reason"_q, reason);
	CallTokenObjectRpc(
		telegramUserId,
		u"fg_verification_review"_q,
		arguments,
		[=](QString error, QJsonObject) {
			if (error.isEmpty()) {
				GetState().directory.clear();
				RequestVerification(telegramUserId, true);
			}
			done(error);
		});
}

void RequestOrgVerifications(
		uint64 telegramUserId,
		Fn<void(std::vector<VerificationBadge>)> done) {
	auto &state = GetState();
	if (!telegramUserId) {
		done({});
		return;
	} else if (const auto i = state.orgBadges.find(telegramUserId)
		; i != end(state.orgBadges)
		&& crl::now() - i->second.first < kOrgCacheLifetime) {
		done(i->second.second);
		return;
	}
	auto arguments = QJsonObject();
	arguments.insert(u"p_telegram_user_id"_q, double(telegramUserId));
	CallObjectRpc(
		u"fg_org_verifications"_q,
		arguments,
		[=](QString error, QJsonObject object) {
			auto badges = std::vector<VerificationBadge>();
			if (error.isEmpty()) {
				for (const auto &value : object.value(u"badges"_q).toArray()) {
					badges.push_back(ParseBadge(value.toObject()));
				}
				GetState().orgBadges[telegramUserId] = { crl::now(), badges };
			}
			done(std::move(badges));
		});
}

void GrantOrgVerification(
		uint64 telegramUserId,
		const QString &flashgramId,
		const QString &type,
		const QString &description,
		int days,
		Fn<void(QString error)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_flashgram_id"_q, flashgramId);
	arguments.insert(u"p_type"_q, type);
	arguments.insert(u"p_description"_q, description);
	arguments.insert(u"p_days"_q, days);
	CallTokenObjectRpc(
		telegramUserId,
		u"fg_org_verification_grant"_q,
		arguments,
		[=](QString error, QJsonObject) {
			if (error.isEmpty()) {
				GetState().orgBadges.clear();
				RequestVerification(telegramUserId, true);
			}
			done(error);
		});
}

void RevokeOrgVerification(
		uint64 telegramUserId,
		const QString &flashgramId,
		const QString &type,
		Fn<void(QString error)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"p_flashgram_id"_q, flashgramId);
	arguments.insert(u"p_type"_q, type);
	CallTokenObjectRpc(
		telegramUserId,
		u"fg_org_verification_revoke"_q,
		arguments,
		[=](QString error, QJsonObject) {
			if (error.isEmpty()) {
				GetState().orgBadges.clear();
				RequestVerification(telegramUserId, true);
			}
			done(error);
		});
}

std::vector<StoreEntry> NftStoreEntries() {
	return GetState().nftStore;
}

bool IsTrustedStoreUrl(const QString &url) {
	return (url.startsWith(kTelegramNftPrefix.utf16())
			|| url.startsWith(kFragmentPrefix.utf16()))
		&& QUrl(url).isValid();
}

void RequestEmailCode(const QString &email, Fn<void(QString error)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"email"_q, email);
	arguments.insert(u"create_user"_q, true);
	CallHttp(
		u"/auth/v1/otp"_q,
		arguments,
		QByteArray(),
		u"auth otp"_q,
		[=](int code, std::optional<QJsonValue> value) {
			done((code == 200)
				? QString()
				: AuthError(code, value, u"email_send_failed"_q));
		});
}

void VerifyEmailCode(
		uint64 telegramUserId,
		const QString &email,
		const QString &code,
		Fn<void(QString error)> done) {
	auto arguments = QJsonObject();
	arguments.insert(u"type"_q, u"email"_q);
	arguments.insert(u"email"_q, email);
	arguments.insert(u"token"_q, code);
	CallHttp(
		u"/auth/v1/verify"_q,
		arguments,
		QByteArray(),
		u"auth verify"_q,
		[=](int http, std::optional<QJsonValue> value) {
			const auto accessToken = value
				? value->toObject().value(u"access_token"_q).toString()
				: QString();
			if (http != 200 || accessToken.isEmpty()) {
				done(AuthError(http, value, u"invalid_code"_q));
				return;
			}
			const auto token = ReadAccountToken(telegramUserId);
			if (token.isEmpty()) {
				done(u"no_account"_q);
				return;
			}
			auto link = QJsonObject();
			link.insert(u"p_token"_q, token);
			CallRpc(u"fg_link_email"_q, link, [=](
					std::optional<QJsonValue> result) {
				if (!result) {
					done(u"network"_q);
					return;
				}
				const auto object = result->toObject();
				const auto error = object.value(u"error"_q).toString();
				if (error.isEmpty()) {
					auto &state = GetState();
					if (const auto i = state.accounts.find(telegramUserId)
						; i != end(state.accounts)) {
						i->second.email = object.value(u"email"_q).toString();
						state.accountUpdates.fire({});
					}
				}
				done(error);
			}, accessToken.toUtf8());
		});
}

} // namespace FlashGram::Server
