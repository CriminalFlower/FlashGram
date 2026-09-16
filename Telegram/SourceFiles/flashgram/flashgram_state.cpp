/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_state.h"

#include "base/random.h"
#include "base/unixtime.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "settings.h"
#include "ui/text/format_values.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace FlashGram {
namespace {

constexpr auto kCatalogResource = ":/flashgram/flashgram_gifts.json"_cs;
constexpr auto kOwnerResource = ":/flashgram/flashgram_owner.json"_cs;
constexpr auto kCatalogOverride = "flashgram_gifts.json"_cs;
constexpr auto kOwnerOverride = "flashgram_owner.json"_cs;
constexpr auto kStarterGiftsCount = 3;
constexpr auto kStarterBalanceCents = int64(100000);

[[nodiscard]] QString LocalFolder() {
	return cWorkingDir() + u"tdata/flashgram/"_q;
}

[[nodiscard]] rpl::event_stream<> &ChangesStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] QJsonObject ReadJsonObject(const QString &path) {
	auto file = QFile(path);
	if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("FlashGram Error: Could not parse '%1': %2"
			).arg(path, error.errorString()));
		return {};
	}
	return document.object();
}

bool WriteJsonObject(const QString &path, const QJsonObject &object) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
	return file.commit();
}

[[nodiscard]] QStringList ReadStringList(const QJsonValue &value) {
	auto result = QStringList();
	for (const auto &entry : value.toArray()) {
		const auto string = entry.toString().trimmed();
		if (!string.isEmpty()) {
			result.push_back(string);
		}
	}
	return result;
}

[[nodiscard]] BalanceAmount ReadBalance(const QJsonValue &value) {
	return { .cents = qRound64(value.toDouble() * 100.) };
}

[[nodiscard]] GiftRarity ParseRarity(const QString &value) {
	const auto lower = value.trimmed().toLower();
	if (lower == u"rare"_q) {
		return GiftRarity::Rare;
	} else if (lower == u"epic"_q) {
		return GiftRarity::Epic;
	} else if (lower == u"legendary"_q) {
		return GiftRarity::Legendary;
	} else if (lower == u"limited"_q) {
		return GiftRarity::Limited;
	} else if (lower == u"collectible"_q) {
		return GiftRarity::Collectible;
	} else if (lower == u"nft"_q
		|| lower == u"nft-style"_q
		|| lower == u"nft_style"_q) {
		return GiftRarity::NftStyle;
	} else if (lower == u"unique"_q) {
		return GiftRarity::Unique;
	}
	return GiftRarity::Common;
}

[[nodiscard]] int DefaultWeight(GiftRarity rarity) {
	switch (rarity) {
	case GiftRarity::Common: return 500;
	case GiftRarity::Rare: return 250;
	case GiftRarity::Epic: return 120;
	case GiftRarity::Legendary: return 50;
	case GiftRarity::Limited: return 40;
	case GiftRarity::Collectible: return 25;
	case GiftRarity::NftStyle: return 10;
	case GiftRarity::Unique: return 5;
	}
	return 1;
}

[[nodiscard]] Gift ParseGift(const QJsonObject &object) {
	auto result = Gift();
	result.id = object.value(u"id"_q).toString().trimmed();
	result.name = object.value(u"name"_q).toString();
	result.kind = (object.value(u"type"_q).toString().toLower()
		== u"collectible"_q)
		? GiftKind::Collectible
		: GiftKind::Ordinary;
	result.rarity = ParseRarity(object.value(u"rarity"_q).toString());
	result.model = object.value(u"model"_q).toString();
	result.backdrop = object.value(u"backdrop"_q).toString();
	result.symbol = object.value(u"symbol"_q).toString();
	result.description = object.value(u"description"_q).toString();
	result.sticker = object.value(u"sticker"_q).toString();
	result.image = object.value(u"image"_q).toString();
	result.animation = object.value(u"animation"_q).toString();
	result.number = object.value(u"number"_q).toInt();
	result.amount = int64(object.value(u"amount"_q).toDouble());
	result.value = ReadBalance(object.value(u"value"_q));
	result.weight = object.value(u"weight"_q).toInt();
	if (result.weight <= 0) {
		result.weight = DefaultWeight(result.rarity);
	}
	const auto colors = object.value(u"backdropColors"_q).toArray();
	if (colors.size() > 0) {
		result.backdropCenter = QColor(colors[0].toString());
	}
	if (colors.size() > 1) {
		result.backdropEdge = QColor(colors[1].toString());
	}
	if (result.name.isEmpty()) {
		result.name = result.id;
	}
	return result;
}

[[nodiscard]] LootCase ParseCase(const QJsonObject &object) {
	auto result = LootCase();
	result.id = object.value(u"id"_q).toString().trimmed();
	result.name = object.value(u"name"_q).toString();
	result.description = object.value(u"description"_q).toString();
	result.price = ReadBalance(object.value(u"price"_q));
	result.giftIds = ReadStringList(object.value(u"gifts"_q));
	const auto colors = object.value(u"colors"_q).toArray();
	if (colors.size() > 0) {
		result.top = QColor(colors[0].toString());
	}
	if (colors.size() > 1) {
		result.bottom = QColor(colors[1].toString());
	}
	return result;
}

struct Catalog {
	std::vector<Gift> gifts;
	std::vector<LootCase> cases;
};

[[nodiscard]] Catalog LoadCatalog() {
	auto result = Catalog();
	const auto add = [&](const QJsonObject &root) {
		for (const auto &value : root.value(u"gifts"_q).toArray()) {
			auto gift = ParseGift(value.toObject());
			if (gift.id.isEmpty()) {
				continue;
			}
			const auto i = ranges::find(result.gifts, gift.id, &Gift::id);
			if (i != end(result.gifts)) {
				*i = std::move(gift);
			} else {
				result.gifts.push_back(std::move(gift));
			}
		}
		for (const auto &value : root.value(u"cases"_q).toArray()) {
			auto entry = ParseCase(value.toObject());
			if (entry.id.isEmpty()) {
				continue;
			}
			const auto i = ranges::find(result.cases, entry.id, &LootCase::id);
			if (i != end(result.cases)) {
				*i = std::move(entry);
			} else {
				result.cases.push_back(std::move(entry));
			}
		}
	};
	add(ReadJsonObject(kCatalogResource.utf16()));
	add(ReadJsonObject(LocalFolder() + kCatalogOverride.utf16()));
	return result;
}

[[nodiscard]] const Catalog &CatalogData() {
	static const auto result = LoadCatalog();
	return result;
}

[[nodiscard]] OwnerConfig LoadOwnerConfig() {
	auto result = OwnerConfig();
	const auto apply = [&](const QJsonObject &root) {
		const auto string = [&](const QString &key, QString &field) {
			if (root.contains(key)) {
				field = root.value(key).toString();
			}
		};
		string(u"name"_q, result.name);
		string(u"supportId"_q, result.supportId);
		string(u"bio"_q, result.bio);
		if (root.contains(u"balance"_q)) {
			result.balance = ReadBalance(root.value(u"balance"_q));
		}
		if (root.contains(u"badges"_q)) {
			result.badges = ReadStringList(root.value(u"badges"_q));
		}
		if (root.contains(u"gifts"_q)) {
			result.giftIds = ReadStringList(root.value(u"gifts"_q));
		}
		if (root.contains(u"telegramUserIds"_q)) {
			result.telegramUserIds.clear();
			for (const auto &id : root.value(u"telegramUserIds"_q).toArray()) {
				const auto value = id.isString()
					? id.toString().toULongLong()
					: uint64(id.toDouble());
				if (value) {
					result.telegramUserIds.push_back(value);
				}
			}
		}
	};
	apply(ReadJsonObject(kOwnerResource.utf16()));
	apply(ReadJsonObject(LocalFolder() + kOwnerOverride.utf16()));
	return result;
}

[[nodiscard]] uint64 UserBareId(not_null<UserData*> user) {
	return peerToUser(user->id).bare;
}

[[nodiscard]] QString AccountPath(uint64 id) {
	return LocalFolder()
		+ u"accounts/"_q
		+ QString::number(id)
		+ u".json"_q;
}

[[nodiscard]] QString GenerateFlashGramId(uint64 id) {
	const auto mixed = (id * 0x9E3779B97F4A7C15ULL) ^ (id >> 29);
	return u"FG-"_q + QString::number(10000000ULL + (mixed % 90000000ULL));
}

[[nodiscard]] QJsonObject MakeStarterState(uint64 id) {
	auto gifts = QJsonArray();
	for (const auto &gift : GiftsCatalog()) {
		if (gifts.size() >= kStarterGiftsCount) {
			break;
		} else if (gift.kind == GiftKind::Ordinary
			&& gift.rarity == GiftRarity::Common) {
			gifts.push_back(QJsonObject{
				{ u"id"_q, gift.id },
				{ u"number"_q, gift.number },
			});
		}
	}
	return QJsonObject{
		{ u"flashgramId"_q, GenerateFlashGramId(id) },
		{ u"balance"_q, double(kStarterBalanceCents) },
		{ u"badges"_q, QJsonArray() },
		{ u"gifts"_q, gifts },
		{ u"anonymousDisplay"_q, false },
		{ u"ownerProfile"_q, false },
	};
}

[[nodiscard]] QJsonObject LoadAccount(not_null<UserData*> user) {
	const auto id = UserBareId(user);
	auto stored = ReadJsonObject(AccountPath(id));
	if (stored.isEmpty()) {
		stored = MakeStarterState(id);
		if (user->isSelf()) {
			WriteJsonObject(AccountPath(id), stored);
		}
	}
	return stored;
}

void SaveAccount(not_null<UserData*> user, const QJsonObject &object) {
	if (!WriteJsonObject(AccountPath(UserBareId(user)), object)) {
		LOG(("FlashGram Error: Could not write account state."));
	}
	ChangesStream().fire({});
}

[[nodiscard]] bool IsOwnerAccount(
		not_null<UserData*> user,
		const QJsonObject &stored) {
	return ranges::contains(Owner().telegramUserIds, UserBareId(user))
		|| (user->isSelf() && stored.value(u"ownerProfile"_q).toBool());
}

void AddUnique(QStringList &list, const QStringList &values) {
	for (const auto &value : values) {
		if (!list.contains(value)) {
			list.push_back(value);
		}
	}
}

[[nodiscard]] QString GroupDigits(int64 value) {
	const auto digits = QString::number(value);
	auto result = QString();
	result.reserve(digits.size() + digits.size() / 3 + 1);
	for (auto i = 0; i != digits.size(); ++i) {
		if (i > 0 && ((digits.size() - i) % 3 == 0)) {
			result.append(QChar(0x202F));
		}
		result.append(digits[i]);
	}
	return result;
}

} // namespace

QString RarityName(GiftRarity rarity) {
	switch (rarity) {
	case GiftRarity::Common: return tr::lng_flashgram_rarity_common(tr::now);
	case GiftRarity::Rare: return tr::lng_flashgram_rarity_rare(tr::now);
	case GiftRarity::Epic: return tr::lng_flashgram_rarity_epic(tr::now);
	case GiftRarity::Legendary:
		return tr::lng_flashgram_rarity_legendary(tr::now);
	case GiftRarity::Limited: return tr::lng_flashgram_rarity_limited(tr::now);
	case GiftRarity::Collectible:
		return tr::lng_flashgram_rarity_collectible(tr::now);
	case GiftRarity::NftStyle: return tr::lng_flashgram_rarity_nft(tr::now);
	case GiftRarity::Unique: return tr::lng_flashgram_rarity_unique(tr::now);
	}
	Unexpected("Rarity in FlashGram::RarityName.");
}

QColor RarityColor(GiftRarity rarity) {
	switch (rarity) {
	case GiftRarity::Common: return QColor(0x8A, 0x94, 0xA6);
	case GiftRarity::Rare: return QColor(0x3E, 0x9C, 0xF0);
	case GiftRarity::Epic: return QColor(0x9B, 0x5D, 0xE5);
	case GiftRarity::Legendary: return QColor(0xF2, 0xA5, 0x3B);
	case GiftRarity::Limited: return QColor(0xE5, 0x4F, 0x6D);
	case GiftRarity::Collectible: return QColor(0x2F, 0xB5, 0x9B);
	case GiftRarity::NftStyle: return QColor(0x5B, 0x6C, 0xF0);
	case GiftRarity::Unique: return QColor(0xE0, 0x45, 0x2B);
	}
	Unexpected("Rarity in FlashGram::RarityColor.");
}

BadgeKind ParseBadgeKind(const QString &badge) {
	const auto lower = badge.toLower();
	if (lower.contains(u"owner"_q)) {
		return BadgeKind::Owner;
	} else if (lower.contains(u"support"_q)) {
		return BadgeKind::Support;
	} else if (lower.contains(u"major"_q)) {
		return BadgeKind::Major;
	} else if (lower.contains(u"hold"_q)) {
		return BadgeKind::Hold;
	} else if (lower.contains(u"verified"_q)) {
		return BadgeKind::Verified;
	}
	return BadgeKind::Custom;
}

QColor BadgeColor(const QString &badge) {
	switch (ParseBadgeKind(badge)) {
	case BadgeKind::Owner: return QColor(0xF2, 0xA5, 0x3B);
	case BadgeKind::Support: return QColor(0x3E, 0x9C, 0xF0);
	case BadgeKind::Major: return QColor(0xE5, 0x4F, 0x6D);
	case BadgeKind::Hold: return QColor(0x2F, 0xB5, 0x9B);
	case BadgeKind::Verified: return QColor(0x3E, 0xB8, 0x6D);
	case BadgeKind::Custom: return QColor(0x8E, 0x6C, 0xF0);
	}
	return QColor(0x8E, 0x6C, 0xF0);
}

const std::vector<Gift> &GiftsCatalog() {
	return CatalogData().gifts;
}

const Gift *FindGift(const QString &id) {
	const auto &catalog = GiftsCatalog();
	const auto i = ranges::find(catalog, id, &Gift::id);
	return (i != end(catalog)) ? &*i : nullptr;
}

const std::vector<LootCase> &Cases() {
	return CatalogData().cases;
}

const LootCase *FindCase(const QString &id) {
	const auto &cases = Cases();
	const auto i = ranges::find(cases, id, &LootCase::id);
	return (i != end(cases)) ? &*i : nullptr;
}

const OwnerConfig &Owner() {
	static const auto result = LoadOwnerConfig();
	return result;
}

bool HasProfile(not_null<UserData*> user) {
	return user->isSelf()
		|| ranges::contains(Owner().telegramUserIds, UserBareId(user));
}

Profile LoadProfile(not_null<UserData*> user) {
	const auto id = UserBareId(user);
	const auto stored = LoadAccount(user);

	auto result = Profile();
	result.flashgramId = stored.value(u"flashgramId"_q).toString();
	if (result.flashgramId.isEmpty()) {
		result.flashgramId = GenerateFlashGramId(id);
	}
	result.displayName = user->name();
	result.badges = ReadStringList(stored.value(u"badges"_q));
	result.anonymousDisplay = stored.value(u"anonymousDisplay"_q).toBool();
	result.ownerProfileEnabled = stored.value(u"ownerProfile"_q).toBool();
	const auto account = user->isSelf() ? Server::CachedAccount(id) : nullptr;
	if (account) {
		result.flashgramId = account->flashgramId;
		result.balance = { .cents = account->balanceFg * 100 };
		for (const auto &gift : account->gifts) {
			if (FindGift(gift.definitionId)) {
				result.gifts.push_back(OwnedFromServer(gift, id));
			}
		}
	} else if (user->isSelf()) {
		Server::RequestAccount(id, [](Server::Account) {});
	}

	const auto &owner = Owner();
	result.ownerForced = ranges::contains(owner.telegramUserIds, id);
	result.owner = IsOwnerAccount(user, stored);
	if (result.owner) {
		if (!owner.supportId.isEmpty()) {
			result.flashgramId = owner.supportId;
		}
		if (!owner.name.isEmpty()) {
			result.displayName = owner.name;
		}
		result.bio = owner.bio;
		auto badges = owner.badges;
		AddUnique(badges, result.badges);
		result.badges = std::move(badges);

		auto ownerGifts = std::vector<OwnedGift>();
		const auto addOwned = [&](const Gift &gift) {
			if (!ranges::contains(result.gifts, gift.id, &OwnedGift::giftId)
				&& !ranges::contains(ownerGifts, gift.id, &OwnedGift::giftId)) {
				ownerGifts.push_back({
					.uid = u"o:"_q + gift.id,
					.giftId = gift.id,
					.number = gift.number,
				});
			}
		};
		if (owner.giftIds.isEmpty()) {
			for (const auto &gift : GiftsCatalog()) {
				addOwned(gift);
			}
		} else {
			for (const auto &giftId : owner.giftIds) {
				if (const auto gift = FindGift(giftId)) {
					addOwned(*gift);
				}
			}
		}
		result.gifts.insert(
			begin(result.gifts),
			begin(ownerGifts),
			end(ownerGifts));
	}
	const auto flags = stored.value(u"giftFlags"_q).toObject();
	for (auto &owned : result.gifts) {
		owned.ownerId = id;
		if (!owned.uid.startsWith(u"o:"_q)) {
			continue;
		}
		const auto object = flags.value(owned.uid).toObject();
		owned.pinned = object.value(u"pinned"_q).toBool();
		owned.inProfile = object.value(u"inProfile"_q).toBool();
		owned.listedForSale = object.value(u"listedForSale"_q).toBool();
		owned.price.cents = int64(object.value(u"price"_q).toDouble());
	}
	ranges::stable_sort(result.gifts, [](
			const OwnedGift &a,
			const OwnedGift &b) {
		return a.pinned && !b.pinned;
	});
	return result;
}

void SaveAccountFlag(
		not_null<UserData*> user,
		const QString &key,
		bool value) {
	auto stored = LoadAccount(user);
	stored.insert(key, value);
	SaveAccount(user, stored);
}

void SetGiftFlag(
		not_null<UserData*> user,
		const QString &uid,
		GiftFlag flag,
		bool value) {
	if (!uid.startsWith(u"o:"_q)) {
		Server::SetGiftFlags(
			UserBareId(user),
			uid,
			(flag == GiftFlag::Pinned) ? std::make_optional(value) : std::nullopt,
			(flag == GiftFlag::InProfile)
				? std::make_optional(value)
				: std::nullopt);
		return;
	}
	auto stored = LoadAccount(user);
	auto flags = stored.value(u"giftFlags"_q).toObject();
	auto object = flags.value(uid).toObject();
	object.insert(
		(flag == GiftFlag::Pinned) ? u"pinned"_q : u"inProfile"_q,
		value);
	flags.insert(uid, object);
	stored.insert(u"giftFlags"_q, flags);
	SaveAccount(user, stored);
}

rpl::producer<> Changes() {
	return rpl::merge(ChangesStream().events(), Server::AccountUpdates());
}

PhoneDisplay LoadPhoneDisplay(not_null<UserData*> user) {
	const auto stored = LoadAccount(user);
	const auto mode = stored.value(u"phoneDisplay"_q).toString();
	if (mode == u"masked"_q) {
		return PhoneDisplay::Masked;
	} else if (mode == u"id"_q) {
		return PhoneDisplay::FlashGramId;
	} else if (mode.isEmpty() && stored.value(u"anonymousDisplay"_q).toBool()) {
		return PhoneDisplay::FlashGramId;
	}
	return PhoneDisplay::Real;
}

void SavePhoneDisplay(not_null<UserData*> user, PhoneDisplay mode) {
	auto stored = LoadAccount(user);
	stored.insert(
		u"phoneDisplay"_q,
		(mode == PhoneDisplay::Masked)
			? u"masked"_q
			: (mode == PhoneDisplay::FlashGramId)
			? u"id"_q
			: u"real"_q);
	stored.insert(u"anonymousDisplay"_q, mode != PhoneDisplay::Real);
	SaveAccount(user, stored);
}

QString MaskedPhone(const QString &phone) {
	const auto formatted = Ui::FormatPhone(phone);
	const auto firstSpace = formatted.indexOf(' ');
	auto total = 0;
	for (const auto ch : formatted) {
		if (ch.isDigit()) {
			++total;
		}
	}
	auto result = QString();
	auto index = 0;
	for (auto i = 0; i != formatted.size(); ++i) {
		const auto ch = formatted[i];
		if (!ch.isDigit()) {
			result.append(ch);
			continue;
		}
		++index;
		const auto code = (firstSpace < 0) ? (index <= 1) : (i < firstSpace);
		result.append((code || index > total - 2) ? ch : QChar(0x2022));
	}
	return result;
}

QString DisplayedPhone(not_null<UserData*> user) {
	const auto phone = user->phone();
	switch (LoadPhoneDisplay(user)) {
	case PhoneDisplay::Masked:
		return phone.isEmpty() ? LoadProfile(user).flashgramId : MaskedPhone(phone);
	case PhoneDisplay::FlashGramId:
		return LoadProfile(user).flashgramId;
	case PhoneDisplay::Real:
		break;
	}
	return phone.isEmpty() ? QString() : Ui::FormatPhone(phone);
}

bool GiftBackendAvailable() {
	return Server::CurrentStatus() == Server::Status::Online;
}

OwnedGift OwnedFromServer(const Server::ServerGift &gift, uint64 ownerId) {
	return {
		.uid = gift.id,
		.giftId = gift.definitionId,
		.number = gift.number,
		.obtainedAt = gift.acquiredAt,
		.ownerId = ownerId,
		.pinned = gift.pinned,
		.inProfile = gift.inProfile,
	};
}

QString ServerErrorText(const QString &error) {
	if (error == u"network"_q) {
		return Tr(
			"FlashGram Server is unavailable. Try again later.",
			"Сервер FlashGram недоступен. Попробуйте позже.");
	} else if (error == u"no_account"_q) {
		return Tr(
			"FlashGram account isn't ready yet. Try again in a moment.",
			"Аккаунт FlashGram ещё не готов. Попробуйте чуть позже.");
	} else if (error == u"not_enough_balance"_q) {
		return Tr(
			"Not enough FG on your balance.",
			"Недостаточно FG на балансе.");
	} else if (error == u"empty_pool"_q) {
		return Tr(
			"NFT prizes are temporarily unavailable.",
			"NFT-призы временно недоступны.");
	} else if (error == u"recipient_not_found"_q) {
		return Tr(
			"No FlashGram user with this ID.",
			"Пользователь с таким FlashGram ID не найден.");
	} else if (error == u"self_transfer"_q) {
		return Tr(
			"You can't transfer a gift to yourself.",
			"Нельзя передать подарок самому себе.");
	} else if (error == u"gift_not_found"_q) {
		return Tr(
			"This gift is no longer in your collection.",
			"Этого подарка больше нет в вашей коллекции.");
	} else if (error == u"email_taken"_q) {
		return Tr(
			"This email is already linked to another FlashGram account.",
			"Эта почта уже привязана к другому аккаунту FlashGram.");
	} else if (error == u"invalid_email"_q) {
		return Tr("Check the email address.", "Проверьте адрес почты.");
	} else if (error == u"invalid_code"_q) {
		return Tr(
			"Wrong or expired code.",
			"Неверный или устаревший код.");
	} else if (error == u"rate_limited"_q) {
		return Tr(
			"Too many attempts. Try again later.",
			"Слишком много попыток. Попробуйте позже.");
	} else if (error == u"email_required"_q) {
		return Tr(
			"Link an email to confirm your FlashGram ID first.",
			"Сначала привяжите почту, чтобы подтвердить FlashGram ID.");
	} else if (error == u"profile_incomplete"_q) {
		return Tr(
			"Fill in your name and username first.",
			"Сначала заполните имя и username.");
	} else if (error == u"invalid_target"_q) {
		return Tr(
			"Enter a valid @username of the channel or bot.",
			"Укажите корректный @username канала или бота.");
	} else if (error == u"has_violations"_q) {
		return Tr(
			"Your FlashGram Verification was revoked recently. "
			"Try again in 30 days.",
			"Верификация FlashGram недавно была отозвана. "
			"Попробуйте через 30 дней.");
	} else if (error == u"already_requested"_q) {
		return Tr(
			"You already have an active request of this type.",
			"У вас уже есть активная заявка этого типа.");
	} else if (error == u"forbidden"_q) {
		return Tr(
			"Only FlashGram admins can do this.",
			"Это доступно только администраторам FlashGram.");
	} else if (error == u"invalid_state"_q || error == u"not_found"_q) {
		return Tr(
			"This request was already reviewed.",
			"Эта заявка уже рассмотрена.");
	} else if (error == u"email_send_failed"_q) {
		return Tr(
			"Couldn't send the email. Try again later.",
			"Не удалось отправить письмо. Попробуйте позже.");
	}
	return Tr("FlashGram Server error.", "Ошибка сервера FlashGram.");
}

BalanceAmount CollectionValue(const std::vector<OwnedGift> &gifts) {
	auto result = BalanceAmount();
	for (const auto &owned : gifts) {
		if (const auto gift = FindGift(owned.giftId)) {
			result.cents += gift->value.cents;
		}
	}
	return result;
}

QString GiftTitle(const Gift &gift, int number) {
	return (gift.kind == GiftKind::Collectible && number > 0)
		? (gift.name + u" #"_q + FormatCount(number))
		: gift.name;
}

QString GiftsCountText(int count) {
	if (!Lang::Id().startsWith(u"ru"_q)) {
		return QString::number(count)
			+ ((count == 1) ? u" gift"_q : u" gifts"_q);
	}
	const auto mod10 = count % 10;
	const auto mod100 = count % 100;
	const auto word = (mod10 == 1 && mod100 != 11)
		? "подарок"
		: (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
		? "подарка"
		: "подарков";
	return QString::number(count) + ' ' + QString::fromUtf8(word);
}

const Gift *RollGift(const QStringList &pool) {
	auto candidates = std::vector<const Gift*>();
	auto total = int64(0);
	for (const auto &gift : GiftsCatalog()) {
		if (pool.isEmpty() || pool.contains(gift.id)) {
			candidates.push_back(&gift);
			total += std::max(gift.weight, 1);
		}
	}
	if (candidates.empty() || total <= 0) {
		return nullptr;
	}
	auto roll = int64(base::RandomValue<uint32>() % uint64(total));
	for (const auto gift : candidates) {
		roll -= std::max(gift->weight, 1);
		if (roll < 0) {
			return gift;
		}
	}
	return candidates.back();
}

int RollNumber(const Gift &gift) {
	const auto limit = (gift.amount > 0) ? gift.amount : 100000;
	return 1 + int(base::RandomValue<uint32>() % uint64(limit));
}

QString FormatBalance(BalanceAmount amount) {
	const auto negative = (amount.cents < 0);
	const auto cents = negative ? -amount.cents : amount.cents;
	const auto result = GroupDigits(cents / 100)
		+ ((cents % 100)
			? ('.' + QString::number(cents % 100).rightJustified(2, '0'))
			: QString())
		+ u" FG"_q;
	return negative ? (u"-"_q + result) : result;
}

QString FormatCount(int64 value) {
	return GroupDigits(value);
}

QImage LoadGiftImage(const Gift &gift) {
	if (gift.image.isEmpty()) {
		return QImage();
	}
	const auto path = (gift.image.startsWith(u":/"_q)
		|| QDir::isAbsolutePath(gift.image))
		? gift.image
		: (LocalFolder() + gift.image);
	auto result = QImage(path);
	if (result.isNull()) {
		LOG(("FlashGram Error: Could not load gift image '%1'.").arg(path));
	}
	return result;
}

QString Tr(const char *en, const char *ru) {
	return Lang::Id().startsWith(u"ru"_q)
		? QString::fromUtf8(ru)
		: QString::fromUtf8(en);
}

rpl::producer<QString> TrValue(const char *en, const char *ru) {
	return rpl::single(Tr(en, ru));
}

} // namespace FlashGram
