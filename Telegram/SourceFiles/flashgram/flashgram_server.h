/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// FlashGram backend client. It only serves FlashGram-specific features:
// server health, public client config, update metadata, the FlashGram
// account (FlashGram ID, email, FG balance, gifts) and gift actions.
// Telegram login, keys, codes, passwords, messages and session files are
// never sent here. When the server is unreachable Telegram keeps working
// and FlashGram server features are shown as offline.
//
// Balance changes and gift ownership are decided only by the server:
// the client asks for an action and shows the server's answer.
namespace FlashGram::Server {

enum class Status : uchar {
	Checking,
	Online,
	Offline,
};

struct EmojiSet {
	QString title;
	QString url;
	QString description;
	QString icon;
	uint64 previewDocumentId = 0;
};

struct Release {
	QString version;
	QString minimumSupportedVersion;
	QString downloadUrl;
	QString notes;
};

struct ServerGift {
	QString id;
	QString definitionId;
	int number = 0;
	TimeId acquiredAt = 0;
	bool pinned = false;
	bool inProfile = false;
};

struct Account {
	QString flashgramId;
	QString email;
	int64 balanceFg = 0;
	std::vector<ServerGift> gifts;
};

struct ActionResult {
	QString error;
	std::optional<ServerGift> gift;
	int64 balanceFg = -1;
};

// FlashGram Verification is a FlashGram-only badge. It is granted by
// FlashGram admins on the FlashGram server and never changes Telegram
// server verification. The client can only submit a request and read
// its status; approval and badges are decided server-side.
struct VerificationRequest {
	QString id;
	QString type;
	QString level;
	QString status;
	QString displayName;
	QString username;
	QString target;
	QString flashgramId;
	QString reason;
	TimeId createdAt = 0;
	TimeId reviewedAt = 0;
	bool publicListing = false;
};

// Every badge here is FlashGram data from the FlashGram server, including
// the organization badges "major" and "hold". None of it is Telegram data.
struct VerificationBadge {
	QString type;
	TimeId grantedAt = 0;
	QString title;
	QString description;
	QString issuer;
	TimeId expiresAt = 0;
};

// A curated link to a real listing in an official store (Telegram NFT
// gift pages or Fragment). Purchases happen only in that store.
struct StoreEntry {
	QString title;
	QString rarity;
	QString priceText;
	QString url;
};

struct Verification {
	std::vector<VerificationRequest> requests;
	std::vector<VerificationBadge> badges;
	bool admin = false;
	bool emailLinked = false;
	bool violations = false;
	bool emailRequired = true;
};

struct VerificationSubmit {
	QString type;
	QString level;
	QString displayName;
	QString username;
	QString target;
	bool publicListing = false;
};

struct DirectoryEntry {
	QString displayName;
	QString username;
	QString type;
	QString level;
	TimeId verifiedAt = 0;
	int badges = 0;
};

[[nodiscard]] QString ClientVersion();
[[nodiscard]] int CompareVersions(const QString &a, const QString &b);

void Start();
void RefreshHealth();
[[nodiscard]] Status CurrentStatus();
[[nodiscard]] rpl::producer<Status> StatusValue();
[[nodiscard]] rpl::producer<> ConfigUpdates();
[[nodiscard]] bool ConfigLoaded();
[[nodiscard]] bool MaintenanceMode();

// Email linking is optional until a verified sending domain is set up on
// the server, so FlashGram never asks for an email it can't deliver to.
[[nodiscard]] bool EmailRegistrationEnabled();
[[nodiscard]] QString ServerMessage();
[[nodiscard]] std::vector<EmojiSet> EmojiSets();
[[nodiscard]] std::optional<Release> AvailableUpdate();
[[nodiscard]] bool UpdateRequired();
[[nodiscard]] bool IsTrustedReleaseUrl(const QString &url);
[[nodiscard]] QString ReleasesPageUrl();

void RequestAccount(uint64 telegramUserId, Fn<void(Account)> done);
void RefreshAccount(uint64 telegramUserId);
[[nodiscard]] const Account *CachedAccount(uint64 telegramUserId);
[[nodiscard]] rpl::producer<> AccountUpdates();

void OpenCase(
	uint64 telegramUserId,
	const QString &caseId,
	Fn<void(ActionResult)> done);
void SpinRoulette(uint64 telegramUserId, Fn<void(ActionResult)> done);
void SellGift(
	uint64 telegramUserId,
	const QString &giftId,
	Fn<void(ActionResult)> done);
void TransferGift(
	uint64 telegramUserId,
	const QString &giftId,
	const QString &recipientFlashGramId,
	Fn<void(ActionResult)> done);
void SetGiftFlags(
	uint64 telegramUserId,
	const QString &giftId,
	std::optional<bool> pinned,
	std::optional<bool> inProfile);

void RequestVerification(uint64 telegramUserId, bool force = false);
[[nodiscard]] const Verification *CachedVerification(uint64 telegramUserId);
[[nodiscard]] QString VerificationError(uint64 telegramUserId);
[[nodiscard]] rpl::producer<> VerificationUpdates();
void SubmitVerification(
	uint64 telegramUserId,
	VerificationSubmit request,
	Fn<void(QString error)> done);
void SetVerificationListing(
	uint64 telegramUserId,
	const QString &type,
	bool publicListing,
	Fn<void(QString error)> done);
void RequestDirectory(
	const QString &category,
	Fn<void(QString error, std::vector<DirectoryEntry>)> done);
void RequestVerificationQueue(
	uint64 telegramUserId,
	const QString &status,
	Fn<void(QString error, std::vector<VerificationRequest>)> done);
void ReviewVerification(
	uint64 telegramUserId,
	const QString &requestId,
	const QString &action,
	const QString &reason,
	Fn<void(QString error)> done);

void RequestOrgVerifications(
	uint64 telegramUserId,
	Fn<void(std::vector<VerificationBadge>)> done);
void GrantOrgVerification(
	uint64 telegramUserId,
	const QString &flashgramId,
	const QString &type,
	const QString &description,
	int days,
	Fn<void(QString error)> done);
void RevokeOrgVerification(
	uint64 telegramUserId,
	const QString &flashgramId,
	const QString &type,
	Fn<void(QString error)> done);

[[nodiscard]] std::vector<StoreEntry> NftStoreEntries();
[[nodiscard]] bool IsTrustedStoreUrl(const QString &url);

// Email registration goes through Supabase Auth one-time codes. The auth
// session is used once to link the verified email and is not stored.
void RequestEmailCode(const QString &email, Fn<void(QString error)> done);
void VerifyEmailCode(
	uint64 telegramUserId,
	const QString &email,
	const QString &code,
	Fn<void(QString error)> done);

} // namespace FlashGram::Server
