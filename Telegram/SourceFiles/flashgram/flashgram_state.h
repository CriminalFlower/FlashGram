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

[[nodiscard]] QString RarityName(GiftRarity rarity);
[[nodiscard]] QColor RarityColor(GiftRarity rarity);

struct StarsAmount {
	int64 value = 0;
};

struct Gift {
	QString id;
	QString name;
	GiftRarity rarity = GiftRarity::Common;
	QString model;
	QString backdrop;
	QString symbol;
	QString description;
	QString emoji;
	QString image;
	QString animation;
	QColor backdropCenter;
	QColor backdropEdge;
	int number = 0;
	int64 amount = 0;
};

struct OwnedGift {
	QString giftId;
	int number = 0;
};

struct OwnerConfig {
	QString name;
	QString supportId;
	QString bio;
	StarsAmount stars;
	QStringList badges;
	QStringList giftIds;
	std::vector<uint64> telegramUserIds;
};

struct Profile {
	QString flashgramId;
	QString displayName;
	QString bio;
	StarsAmount stars;
	QStringList badges;
	std::vector<OwnedGift> gifts;
	bool owner = false;
	bool ownerForced = false;
	bool ownerProfileEnabled = false;
	bool anonymousDisplay = false;
};

[[nodiscard]] const std::vector<Gift> &GiftsCatalog();
[[nodiscard]] const Gift *FindGift(const QString &id);
[[nodiscard]] const OwnerConfig &Owner();

[[nodiscard]] bool HasProfile(not_null<UserData*> user);
[[nodiscard]] Profile LoadProfile(not_null<UserData*> user);
void SaveAccountFlag(
	not_null<UserData*> user,
	const QString &key,
	bool value);

[[nodiscard]] QString FormatStars(StarsAmount amount);
[[nodiscard]] QImage LoadGiftImage(const Gift &gift);

} // namespace FlashGram
