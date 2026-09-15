/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_profile.h"

#include "boxes/peers/edit_peer_info_box.h"
#include "data/data_user.h"
#include "flashgram/flashgram_state.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/abstract_button.h"
#include "ui/emoji_config.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_flashgram.h"
#include "styles/style_info_profile_actions.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainterPath>

namespace FlashGram {
namespace {

constexpr auto kGridColumns = 3;
constexpr auto kProfilePreviewCount = 6;

[[nodiscard]] QColor BadgeColor(const QString &badge) {
	const auto lower = badge.toLower();
	if (lower.contains(u"owner"_q)) {
		return QColor(0xF2, 0xA5, 0x3B);
	} else if (lower.contains(u"support"_q)) {
		return QColor(0x3E, 0x9C, 0xF0);
	} else if (lower.contains(u"verified"_q)) {
		return QColor(0x3E, 0xB8, 0x6D);
	}
	return QColor(0x8E, 0x6C, 0xF0);
}

void PaintGiftCard(
		QPainter &p,
		QRect rect,
		const Gift &gift,
		int number,
		bool over,
		const QImage &image,
		bool preview) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::flashgramGiftRadius;
	const auto center = gift.backdropCenter.isValid()
		? gift.backdropCenter
		: st::windowBgOver->c;
	const auto edge = gift.backdropEdge.isValid()
		? gift.backdropEdge
		: center.darker(130);
	auto gradient = QRadialGradient(
		QPointF(rect.center()),
		std::max(rect.width(), rect.height()) * 0.75);
	gradient.setColorAt(0., center);
	gradient.setColorAt(1., edge);
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(rect), radius, radius);
	p.fillPath(path, gradient);
	if (over) {
		p.fillPath(path, QColor(255, 255, 255, 28));
	}

	const auto textHeight = preview ? 0 : st::flashgramGiftTextHeight;
	const auto topSkip = preview ? 0 : st::flashgramGiftRarityHeight;
	const auto size = preview
		? st::flashgramGiftPreviewEmojiSize
		: st::flashgramGiftEmojiSize;
	const auto visualHeight = rect.height() - textHeight - topSkip;
	const auto target = QRect(
		rect.x() + (rect.width() - size) / 2,
		rect.y() + topSkip + (visualHeight - size) / 2,
		size,
		size);
	if (!image.isNull()) {
		p.drawImage(target, image);
	} else if (const auto emoji = Ui::Emoji::Find(QStringView(gift.emoji))) {
		const auto large = Ui::Emoji::GetSizeLarge();
		const auto logical = large / float64(style::DevicePixelRatio());
		p.save();
		p.translate(target.topLeft());
		p.scale(size / logical, size / logical);
		Ui::Emoji::Draw(p, emoji, large, 0, 0);
		p.restore();
	}
	if (preview) {
		return;
	}

	const auto margin = st::flashgramGiftRarityMargin;
	const auto rarity = RarityName(gift.rarity);
	const auto &rarityFont = st::flashgramGiftRarityFont;
	const auto pillWidth = rarityFont->width(rarity)
		+ 2 * st::flashgramGiftRarityPadding;
	const auto pill = QRect(
		rect.x() + rect.width() - margin - pillWidth,
		rect.y() + margin,
		pillWidth,
		st::flashgramGiftRarityHeight);
	p.setPen(Qt::NoPen);
	p.setBrush(RarityColor(gift.rarity));
	p.drawRoundedRect(pill, pill.height() / 2., pill.height() / 2.);
	p.setPen(QColor(255, 255, 255));
	p.setFont(rarityFont->f);
	p.drawText(pill, Qt::AlignCenter, rarity);

	const auto textWidth = rect.width() - 2 * margin;
	const auto nameRect = QRect(
		rect.x() + margin,
		rect.y() + rect.height() - textHeight,
		textWidth,
		textHeight / 2);
	const auto numberRect = nameRect.translated(0, textHeight / 2);
	const auto &nameFont = st::flashgramGiftNameFont;
	p.setFont(nameFont->f);
	p.drawText(
		nameRect,
		int(Qt::AlignHCenter | Qt::AlignBottom),
		nameFont->elided(gift.name, textWidth));
	p.setFont(st::flashgramGiftNumberFont->f);
	p.setPen(QColor(255, 255, 255, 190));
	p.drawText(
		numberRect,
		int(Qt::AlignHCenter | Qt::AlignTop),
		u"#"_q + QString::number(number));
}

class GiftsGrid final : public Ui::RpWidget {
public:
	GiftsGrid(
		QWidget *parent,
		std::vector<OwnedGift> gifts,
		Fn<void(OwnedGift)> open);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	struct Card {
		not_null<Ui::AbstractButton*> button;
		OwnedGift owned;
		not_null<const Gift*> gift;
		QImage image;
	};

	std::vector<Card> _cards;

};

GiftsGrid::GiftsGrid(
	QWidget *parent,
	std::vector<OwnedGift> gifts,
	Fn<void(OwnedGift)> open)
: RpWidget(parent) {
	for (const auto &owned : gifts) {
		const auto gift = FindGift(owned.giftId);
		if (!gift) {
			continue;
		}
		const auto button = Ui::CreateChild<Ui::AbstractButton>(this);
		const auto index = int(_cards.size());
		_cards.push_back({
			.button = button,
			.owned = owned,
			.gift = gift,
			.image = LoadGiftImage(*gift),
		});
		button->setClickedCallback([=] {
			open(owned);
		});
		button->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(button);
			const auto &card = _cards[index];
			PaintGiftCard(
				p,
				button->rect(),
				*card.gift,
				card.owned.number ? card.owned.number : card.gift->number,
				button->isOver(),
				card.image,
				false);
		}, button->lifetime());
		button->show();
	}
}

int GiftsGrid::resizeGetHeight(int newWidth) {
	const auto skip = st::flashgramGiftSkip;
	const auto cardWidth = std::max(
		(newWidth - skip * (kGridColumns - 1)) / kGridColumns,
		1);
	const auto cardHeight = cardWidth + st::flashgramGiftTextHeight;
	for (auto i = 0; i != int(_cards.size()); ++i) {
		const auto row = i / kGridColumns;
		const auto column = i % kGridColumns;
		_cards[i].button->setGeometry(
			column * (cardWidth + skip),
			row * (cardHeight + skip),
			cardWidth,
			cardHeight);
	}
	const auto rows = (int(_cards.size()) + kGridColumns - 1) / kGridColumns;
	return rows ? (rows * cardHeight + (rows - 1) * skip) : 0;
}

void AddBadges(
		not_null<Ui::VerticalLayout*> container,
		const QStringList &list) {
	if (list.isEmpty()) {
		return;
	}
	const auto badges = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::flashgramBadgesPadding);
	badges->resize(badges->width(), st::flashgramBadgeHeight);
	badges->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(badges);
		auto hq = PainterHighQualityEnabler(p);
		const auto &font = st::flashgramBadgeFont;
		p.setFont(font->f);
		auto left = 0;
		for (const auto &badge : list) {
			const auto width = font->width(badge)
				+ 2 * st::flashgramBadgePadding;
			if (left + width > badges->width()) {
				break;
			}
			const auto rect = QRect(
				left,
				0,
				width,
				st::flashgramBadgeHeight);
			p.setPen(Qt::NoPen);
			p.setBrush(BadgeColor(badge));
			p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
			p.setPen(QColor(255, 255, 255));
			p.drawText(rect, Qt::AlignCenter, badge);
			left += width + st::flashgramBadgeSkip;
		}
	}, badges->lifetime());
}

void FillSection(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> user,
		Fn<void()> rebuild) {
	const auto profile = LoadProfile(user);
	const auto flashgramId = profile.flashgramId;

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		profile.owner
			? rpl::single(profile.displayName)
			: tr::lng_flashgram_section());
	AddBadges(container, profile.badges);
	if (profile.owner && !profile.bio.isEmpty()) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(profile.bio),
				st::boxDividerLabel),
			st::flashgramAboutPadding);
	}

	container->add(EditPeerInfoBox::CreateButton(
		container,
		profile.owner
			? tr::lng_flashgram_support_id()
			: tr::lng_flashgram_id(),
		rpl::producer<QString>(rpl::single(flashgramId)),
		[=] {
			QGuiApplication::clipboard()->setText(flashgramId);
			show->showToast(tr::lng_flashgram_id_copied(tr::now));
		},
		st::infoSharedMediaCountButton,
		{ .icon = &st::menuIconProfile }));

	const auto stars = TextWithEntities{
		.text = QString::fromUtf8("\xE2\xAD\x90 ") + FormatStars(profile.stars),
	};
	container->add(EditPeerInfoBox::CreateButton(
		container,
		tr::lng_flashgram_stars(),
		rpl::producer<TextWithEntities>(rpl::single(stars)),
		[=] { show->showToast(tr::lng_flashgram_stars_about(tr::now)); },
		st::infoSharedMediaCountButton,
		{ .icon = &st::menuIconPremium }));

	if (user->isSelf()) {
		const auto anonymous = container->lifetime().make_state<
			rpl::variable<bool>>(profile.anonymousDisplay);
		const auto phone = user->phone();
		auto shown = anonymous->value() | rpl::map([=](bool hidden) {
			return (hidden || phone.isEmpty())
				? flashgramId
				: Ui::FormatPhone(phone);
		});
		container->add(EditPeerInfoBox::CreateButton(
			container,
			tr::lng_flashgram_shown_number(),
			rpl::producer<QString>(std::move(shown)),
			[] {},
			st::infoSharedMediaCountButton,
			{ .icon = &st::menuIconStealth }));

		const auto anonymousToggle = container->add(
			object_ptr<Ui::SettingsButton>(
				container,
				tr::lng_flashgram_anonymous(),
				st::settingsButtonNoIcon));
		anonymousToggle->toggleOn(rpl::single(profile.anonymousDisplay));
		anonymousToggle->toggledChanges(
		) | rpl::on_next([=](bool enabled) {
			*anonymous = enabled;
			SaveAccountFlag(user, u"anonymousDisplay"_q, enabled);
		}, anonymousToggle->lifetime());

		if (!profile.ownerForced) {
			const auto ownerToggle = container->add(
				object_ptr<Ui::SettingsButton>(
					container,
					tr::lng_flashgram_owner_mode(),
					st::settingsButtonNoIcon));
			ownerToggle->toggleOn(rpl::single(profile.ownerProfileEnabled));
			ownerToggle->toggledChanges(
			) | rpl::on_next([=](bool enabled) {
				SaveAccountFlag(user, u"ownerProfile"_q, enabled);
				rebuild();
			}, ownerToggle->lifetime());
		}
	}

	const auto &gifts = profile.gifts;
	const auto count = int(gifts.size());
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(tr::lng_flashgram_gifts_count(
			tr::now,
			lt_count,
			count)));
	if (gifts.empty()) {
		Ui::AddDividerText(container, tr::lng_flashgram_gifts_empty());
	} else {
		const auto open = [=](OwnedGift owned) {
			show->show(Box(GiftDetailsBox, user, owned));
		};
		auto preview = std::vector<OwnedGift>(
			begin(gifts),
			begin(gifts) + std::min(count, kProfilePreviewCount));
		container->add(
			object_ptr<GiftsGrid>(container, std::move(preview), open),
			st::flashgramGiftsPadding);
		if (count > kProfilePreviewCount) {
			container->add(EditPeerInfoBox::CreateButton(
				container,
				tr::lng_flashgram_gifts_show_all(),
				rpl::producer<QString>(rpl::single(QString::number(count))),
				[=] { show->show(Box(GiftsBox, show, user)); },
				st::infoSharedMediaCountButton,
				{ .icon = &st::menuIconGiftPremium }));
		}
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_flashgram_about());
	Ui::AddSkip(container);
}

} // namespace

void AddProfileSection(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> user) {
	if (user->isBot() || !HasProfile(user)) {
		return;
	}
	const auto inner = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto rebuild = inner->lifetime().make_state<Fn<void()>>();
	*rebuild = [=] {
		inner->clear();
		FillSection(inner, show, user, [=] {
			crl::on_main(inner, [=] {
				(*rebuild)();
			});
		});
		inner->resizeToWidth(container->width());
	};
	(*rebuild)();
}

void GiftsBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> user) {
	const auto profile = LoadProfile(user);
	box->setTitle(rpl::single(tr::lng_flashgram_gifts_count(
		tr::now,
		lt_count,
		int(profile.gifts.size()))));
	box->setWidth(st::boxWideWidth);
	const auto open = [=](OwnedGift owned) {
		show->show(Box(GiftDetailsBox, user, owned));
	};
	box->addRow(
		object_ptr<GiftsGrid>(box, profile.gifts, open),
		st::flashgramBoxGiftsPadding);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void GiftDetailsBox(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> user,
		OwnedGift owned) {
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });

	const auto found = FindGift(owned.giftId);
	if (!found) {
		box->setTitle(tr::lng_flashgram_gifts());
		return;
	}
	const auto gift = *found;
	const auto profile = LoadProfile(user);
	const auto number = owned.number ? owned.number : gift.number;
	box->setTitle(rpl::single(gift.name));

	const auto image = LoadGiftImage(gift);
	const auto preview = box->addRow(object_ptr<Ui::RpWidget>(box));
	preview->resize(preview->width(), st::flashgramGiftPreviewHeight);
	preview->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(preview);
		PaintGiftCard(p, preview->rect(), gift, number, false, image, true);
	}, preview->lifetime());
	Ui::AddSkip(box->verticalLayout());

	const auto addField = [&](const QString &name, const QString &value) {
		if (value.isEmpty()) {
			return;
		}
		auto text = tr::bold(name);
		text.append(u": "_q).append(value);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::producer<TextWithEntities>(rpl::single(text)),
			st::boxLabel));
		Ui::AddSkip(box->verticalLayout(), st::flashgramDetailsRowSkip);
	};
	const auto numberText = u"#"_q
		+ QString::number(number)
		+ (gift.amount > 0
			? (u" / "_q + FormatStars({ .value = gift.amount }))
			: QString());
	addField(tr::lng_flashgram_gift_number(tr::now), numberText);
	addField(tr::lng_flashgram_gift_rarity(tr::now), RarityName(gift.rarity));
	addField(tr::lng_gift_unique_model(tr::now), gift.model);
	addField(tr::lng_gift_unique_backdrop(tr::now), gift.backdrop);
	addField(tr::lng_gift_unique_symbol(tr::now), gift.symbol);
	addField(
		tr::lng_flashgram_gift_owner(tr::now),
		profile.owner
			? (profile.displayName + u" ("_q + profile.flashgramId + ')')
			: profile.displayName);
	addField(
		tr::lng_flashgram_gift_source(tr::now),
		tr::lng_flashgram_gift_source_local(tr::now));
	addField(tr::lng_flashgram_gift_id(tr::now), gift.id);
	addField(tr::lng_flashgram_gift_animation(tr::now), gift.animation);
	addField(tr::lng_flashgram_gift_description(tr::now), gift.description);
}

} // namespace FlashGram
