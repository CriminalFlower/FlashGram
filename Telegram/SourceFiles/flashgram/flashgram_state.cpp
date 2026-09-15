/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_state.h"

#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "settings.h"

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

[[nodiscard]] QString LocalFolder() {
	return cWorkingDir() + u"tdata/flashgram/"_q;
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

[[nodiscard]] Gift ParseGift(const QJsonObject &object) {
	auto result = Gift();
	result.id = object.value(u"id"_q).toString().trimmed();
	result.name = object.value(u"name"_q).toString();
	result.rarity = ParseRarity(object.value(u"rarity"_q).toString());
	result.model = object.value(u"model"_q).toString();
	result.backdrop = object.value(u"backdrop"_q).toString();
	result.symbol = object.value(u"symbol"_q).toString();
	result.description = object.value(u"description"_q).toString();
	result.emoji = object.value(u"emoji"_q).toString();
	result.image = object.value(u"image"_q).toString();
	result.animation = object.value(u"animation"_q).toString();
	result.number = object.value(u"number"_q).toInt();
	result.amount = int64(object.value(u"amount"_q).toDouble());
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

[[nodiscard]] std::vector<Gift> LoadCatalog() {
	auto result = std::vector<Gift>();
	const auto add = [&](const QJsonObject &root) {
		for (const auto &value : root.value(u"gifts"_q).toArray()) {
			auto gift = ParseGift(value.toObject());
			if (gift.id.isEmpty()) {
				continue;
			}
			const auto i = ranges::find(result, gift.id, &Gift::id);
			if (i != end(result)) {
				*i = std::move(gift);
			} else {
				result.push_back(std::move(gift));
			}
		}
	};
	add(ReadJsonObject(kCatalogResource.utf16()));
	add(ReadJsonObject(LocalFolder() + kCatalogOverride.utf16()));
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
		if (root.contains(u"stars"_q)) {
			result.stars.value = int64(root.value(u"stars"_q).toDouble());
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
		} else if (gift.rarity == GiftRarity::Common) {
			gifts.push_back(QJsonObject{
				{ u"id"_q, gift.id },
				{ u"number"_q, gift.number },
			});
		}
	}
	return QJsonObject{
		{ u"flashgramId"_q, GenerateFlashGramId(id) },
		{ u"stars"_q, 0 },
		{ u"badges"_q, QJsonArray() },
		{ u"gifts"_q, gifts },
		{ u"anonymousDisplay"_q, false },
		{ u"ownerProfile"_q, false },
	};
}

void AddUnique(QStringList &list, const QStringList &values) {
	for (const auto &value : values) {
		if (!list.contains(value)) {
			list.push_back(value);
		}
	}
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

const std::vector<Gift> &GiftsCatalog() {
	static const auto result = LoadCatalog();
	return result;
}

const Gift *FindGift(const QString &id) {
	const auto &catalog = GiftsCatalog();
	const auto i = ranges::find(catalog, id, &Gift::id);
	return (i != end(catalog)) ? &*i : nullptr;
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
	const auto path = AccountPath(id);
	auto stored = ReadJsonObject(path);
	if (stored.isEmpty()) {
		stored = MakeStarterState(id);
		if (user->isSelf()) {
			WriteJsonObject(path, stored);
		}
	}

	auto result = Profile();
	result.flashgramId = stored.value(u"flashgramId"_q).toString();
	if (result.flashgramId.isEmpty()) {
		result.flashgramId = GenerateFlashGramId(id);
	}
	result.displayName = user->name();
	result.stars.value = int64(stored.value(u"stars"_q).toDouble());
	result.badges = ReadStringList(stored.value(u"badges"_q));
	result.anonymousDisplay = stored.value(u"anonymousDisplay"_q).toBool();
	result.ownerProfileEnabled = stored.value(u"ownerProfile"_q).toBool();
	for (const auto &value : stored.value(u"gifts"_q).toArray()) {
		const auto object = value.toObject();
		const auto giftId = object.value(u"id"_q).toString();
		if (FindGift(giftId)) {
			result.gifts.push_back({
				.giftId = giftId,
				.number = object.value(u"number"_q).toInt(),
			});
		}
	}

	const auto &owner = Owner();
	result.ownerForced = ranges::contains(owner.telegramUserIds, id);
	result.owner = result.ownerForced
		|| (user->isSelf() && result.ownerProfileEnabled);
	if (result.owner) {
		if (!owner.supportId.isEmpty()) {
			result.flashgramId = owner.supportId;
		}
		if (!owner.name.isEmpty()) {
			result.displayName = owner.name;
		}
		result.bio = owner.bio;
		result.stars.value = std::max(result.stars.value, owner.stars.value);
		auto badges = owner.badges;
		AddUnique(badges, result.badges);
		result.badges = std::move(badges);

		auto ownerGifts = std::vector<OwnedGift>();
		const auto addOwned = [&](const Gift &gift) {
			if (!ranges::contains(result.gifts, gift.id, &OwnedGift::giftId)
				&& !ranges::contains(ownerGifts, gift.id, &OwnedGift::giftId)) {
				ownerGifts.push_back({
					.giftId = gift.id,
					.number = gift.number,
				});
			}
		};
		if (owner.giftIds.isEmpty()) {
			for (const auto &gift : ranges::views::reverse(GiftsCatalog())) {
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
	return result;
}

void SaveAccountFlag(
		not_null<UserData*> user,
		const QString &key,
		bool value) {
	const auto id = UserBareId(user);
	const auto path = AccountPath(id);
	auto stored = ReadJsonObject(path);
	if (stored.isEmpty()) {
		stored = MakeStarterState(id);
	}
	stored.insert(key, value);
	if (!WriteJsonObject(path, stored)) {
		LOG(("FlashGram Error: Could not write account state."));
	}
}

QString FormatStars(StarsAmount amount) {
	const auto negative = (amount.value < 0);
	const auto digits = QString::number(negative
		? -amount.value
		: amount.value);
	auto result = QString();
	result.reserve(digits.size() + digits.size() / 3 + 1);
	for (auto i = 0; i != digits.size(); ++i) {
		if (i > 0 && ((digits.size() - i) % 3 == 0)) {
			result.append(QChar(0x202F));
		}
		result.append(digits[i]);
	}
	return negative ? (u"-"_q + result) : result;
}

QImage LoadGiftImage(const Gift &gift) {
	if (gift.image.isEmpty()) {
		return QImage();
	}
	const auto path = (gift.image.startsWith(u":/"_q)
		|| QDir::isAbsolutePath(gift.image))
		? gift.image
		: (LocalFolder() + gift.image);
	return QImage(path);
}

} // namespace FlashGram
