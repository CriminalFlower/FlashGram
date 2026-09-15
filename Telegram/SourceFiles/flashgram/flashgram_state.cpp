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

[[nodiscard]] QString BalanceKey(bool owner) {
	return owner ? u"ownerBalance"_q : u"balance"_q;
}

[[nodiscard]] BalanceAmount StoredBalance(
		const QJsonObject &stored,
		bool owner) {
	const auto key = BalanceKey(owner);
	if (stored.contains(key)) {
		return { .cents = int64(stored.value(key).toDouble()) };
	}
	return owner ? Owner().balance : BalanceAmount();
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
	auto index = 0;
	for (const auto &value : stored.value(u"gifts"_q).toArray()) {
		const auto object = value.toObject();
		const auto giftId = object.value(u"id"_q).toString();
		if (FindGift(giftId)) {
			auto owned = OwnedGift{
				.uid = object.value(u"uid"_q).toString(),
				.giftId = giftId,
				.number = object.value(u"number"_q).toInt(),
				.obtainedAt = TimeId(object.value(u"obtainedAt"_q).toInt()),
				.previousOwnerId = uint64(
					object.value(u"previousOwnerId"_q).toDouble()),
			};
			if (owned.uid.isEmpty()) {
				owned.uid = u"s:%1:%2:%3"_q
					.arg(giftId)
					.arg(owned.number)
					.arg(index);
			}
			result.gifts.push_back(std::move(owned));
		}
		++index;
	}

	const auto &owner = Owner();
	result.ownerForced = ranges::contains(owner.telegramUserIds, id);
	result.owner = IsOwnerAccount(user, stored);
	result.balance = StoredBalance(stored, result.owner);
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
		const auto object = flags.value(owned.uid).toObject();
		owned.ownerId = id;
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

bool SpendBalance(not_null<UserData*> user, BalanceAmount amount) {
	auto stored = LoadAccount(user);
	const auto owner = IsOwnerAccount(user, stored);
	const auto current = StoredBalance(stored, owner);
	if (amount.cents < 0 || current.cents < amount.cents) {
		return false;
	}
	stored.insert(BalanceKey(owner), double(current.cents - amount.cents));
	SaveAccount(user, stored);
	return true;
}

OwnedGift AddInventoryGift(not_null<UserData*> user, OwnedGift gift) {
	auto stored = LoadAccount(user);
	auto gifts = stored.value(u"gifts"_q).toArray();
	if (gift.uid.isEmpty()) {
		gift.uid = QString::number(base::RandomValue<uint64>(), 16);
	}
	if (!gift.obtainedAt) {
		gift.obtainedAt = base::unixtime::now();
	}
	gift.ownerId = UserBareId(user);
	gifts.push_front(QJsonObject{
		{ u"uid"_q, gift.uid },
		{ u"id"_q, gift.giftId },
		{ u"number"_q, gift.number },
		{ u"obtainedAt"_q, int(gift.obtainedAt) },
		{ u"ownerId"_q, double(gift.ownerId) },
		{ u"previousOwnerId"_q, double(gift.previousOwnerId) },
		{ u"source"_q, QString::fromLatin1(kLocalSource) },
	});
	stored.insert(u"gifts"_q, gifts);
	SaveAccount(user, stored);
	return gift;
}

void SetGiftFlag(
		not_null<UserData*> user,
		const QString &uid,
		GiftFlag flag,
		bool value) {
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
	return ChangesStream().events();
}

bool GiftBackendAvailable() {
	return false;
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
