/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

// FlashGram local state. Nothing here is Telegram server state: these
// values are never sent to Telegram, never mixed with real Telegram
// Stars or Telegram Gifts and live only in the local working folder
// (tdata/flashgram). Real Telegram data keeps its own code paths.
namespace FlashGram {

inline constexpr auto kLocalSource = "FlashGram";

enum class GiftRarity : uchar {
	Common,
	Rare,
	Epic,
	Legendary,
	Limited,
	Collectible,
	NftStyle,
	Unique,
};

enum class GiftKind : uchar {
	Ordinary,
	Collectible,
};

enum class BadgeKind : uchar {
	Owner,
	Support,
	Major,
	Hold,
	Verified,
	Custom,
};

[[nodiscard]] QString RarityName(GiftRarity rarity);
[[nodiscard]] QColor RarityColor(GiftRarity rarity);
[[nodiscard]] BadgeKind ParseBadgeKind(const QString &badge);
[[nodiscard]] QColor BadgeColor(const QString &badge);

struct BalanceAmount {
	int64 cents = 0;
};

struct Gift {
	QString id;
	QString name;
	GiftKind kind = GiftKind::Ordinary;
	GiftRarity rarity = GiftRarity::Common;
	QString model;
	QString backdrop;
	QString symbol;
	QString description;
	QString sticker;
	QString image;
	QString animation;
	QColor backdropCenter;
	QColor backdropEdge;
	int number = 0;
	int64 amount = 0;
	BalanceAmount value;
	int weight = 0;
};

struct OwnedGift {
	QString uid;
	QString giftId;
	int number = 0;
	TimeId obtainedAt = 0;
	uint64 ownerId = 0;
	uint64 previousOwnerId = 0;
	BalanceAmount price;
	bool pinned = false;
	bool inProfile = false;
	bool listedForSale = false;
};

enum class GiftFlag : uchar {
	Pinned,
	InProfile,
};

// Transfers and sales between people change ownership, so they must be
// applied atomically by a FlashGram server to keep a single owner per
// gift. Until that server exists the client only keeps this model and
// shows the transfer and sale UI in a disabled state.
struct GiftTransaction {
	enum class Type : uchar {
		Transfer,
		Sale,
	};
	Type type = Type::Transfer;
	QString giftUid;
	uint64 fromUserId = 0;
	QString toFlashGramId;
	BalanceAmount price;
	TimeId createdAt = 0;
};

struct LootCase {
	QString id;
	QString name;
	QString description;
	BalanceAmount price;
	QStringList giftIds;
	QColor top;
	QColor bottom;
};

struct OwnerConfig {
	QString name;
	QString supportId;
	QString bio;
	BalanceAmount balance;
	QStringList badges;
	QStringList giftIds;
	std::vector<uint64> telegramUserIds;
};

struct Profile {
	QString flashgramId;
	QString displayName;
	QString bio;
	BalanceAmount balance;
	QStringList badges;
	std::vector<OwnedGift> gifts;
	bool owner = false;
	bool ownerForced = false;
	bool ownerProfileEnabled = false;
	bool anonymousDisplay = false;
};

[[nodiscard]] const std::vector<Gift> &GiftsCatalog();
[[nodiscard]] const Gift *FindGift(const QString &id);
[[nodiscard]] const std::vector<LootCase> &Cases();
[[nodiscard]] const LootCase *FindCase(const QString &id);
[[nodiscard]] const OwnerConfig &Owner();

[[nodiscard]] bool HasProfile(not_null<UserData*> user);
[[nodiscard]] Profile LoadProfile(not_null<UserData*> user);
void SaveAccountFlag(
	not_null<UserData*> user,
	const QString &key,
	bool value);
[[nodiscard]] bool SpendBalance(
	not_null<UserData*> user,
	BalanceAmount amount);
OwnedGift AddInventoryGift(not_null<UserData*> user, OwnedGift gift);
void SetGiftFlag(
	not_null<UserData*> user,
	const QString &uid,
	GiftFlag flag,
	bool value);
[[nodiscard]] rpl::producer<> Changes();
[[nodiscard]] bool GiftBackendAvailable();

[[nodiscard]] BalanceAmount CollectionValue(
	const std::vector<OwnedGift> &gifts);
[[nodiscard]] QString GiftTitle(const Gift &gift, int number);
[[nodiscard]] QString GiftsCountText(int count);

[[nodiscard]] const Gift *RollGift(const QStringList &pool);
[[nodiscard]] int RollNumber(const Gift &gift);

[[nodiscard]] QString FormatBalance(BalanceAmount amount);
[[nodiscard]] QString FormatCount(int64 value);
[[nodiscard]] QImage LoadGiftImage(const Gift &gift);

[[nodiscard]] QString Tr(const char *en, const char *ru);
[[nodiscard]] rpl::producer<QString> TrValue(const char *en, const char *ru);

} // namespace FlashGram
