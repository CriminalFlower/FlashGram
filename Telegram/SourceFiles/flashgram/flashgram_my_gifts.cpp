/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_loot.h"

#include "data/data_user.h"
#include "flashgram/flashgram_gift_view.h"
#include "info/peer_gifts/info_peer_gifts_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainterPath>

namespace FlashGram {
namespace {

constexpr auto kColumns = 2;

void FillRoundedPath(
		QPainter &p,
		const QRectF &rect,
		float64 radius,
		const QBrush &brush) {
	auto hq = PainterHighQualityEnabler(p);
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	p.fillPath(path, brush);
}

[[nodiscard]] std::pair<QColor, QColor> CardColors(const Gift &gift) {
	const auto center = gift.backdropCenter.isValid()
		? gift.backdropCenter
		: RarityColor(gift.rarity).darker(140);
	const auto edge = gift.backdropEdge.isValid()
		? gift.backdropEdge
		: center.darker(190);
	return { center, edge };
}

not_null<Ui::AbstractButton*> CreatePill(
		not_null<QWidget*> parent,
		QString text,
		bool dimmed,
		Fn<void()> callback) {
	const auto button = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	button->setClickedCallback(std::move(callback));
	button->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(button);
		const auto r = QRectF(button->rect());
		FillRoundedPath(
			p,
			r,
			st::flashgramInventoryButtonHeight / 3.,
			QColor(255, 255, 255, button->isOver() ? 58 : 42));
		p.setPen(QColor(255, 255, 255, dimmed ? 150 : 240));
		p.setFont(st::flashgramInventoryButtonFont->f);
		p.drawText(button->rect(), Qt::AlignCenter, text);
	}, button->lifetime());
	button->show();
	return button;
}

class InventoryGrid final : public Ui::RpWidget {
public:
	InventoryGrid(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const std::vector<OwnedGift> &gifts);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	struct Card {
		not_null<Ui::AbstractButton*> widget;
		not_null<GiftStickerView*> gift;
		not_null<Ui::AbstractButton*> profile;
		not_null<Ui::AbstractButton*> sell;
	};
	std::vector<Card> _cards;

};

InventoryGrid::InventoryGrid(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	const std::vector<OwnedGift> &gifts)
: RpWidget(parent) {
	const auto session = &controller->session();
	const auto user = session->user();
	for (const auto &owned : gifts) {
		const auto gift = FindGift(owned.giftId);
		if (!gift) {
			continue;
		}
		const auto number = owned.number ? owned.number : gift->number;
		const auto title = GiftTitle(*gift, number);
		const auto pinned = owned.pinned;
		const auto colors = CardColors(*gift);
		const auto card = Ui::CreateChild<Ui::AbstractButton>(this);
		card->setClickedCallback([=] {
			controller->show(Box(LocalGiftDetailsBox, controller, owned, false));
		});
		card->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(card);
			auto hq = PainterHighQualityEnabler(p);
			const auto r = QRectF(card->rect());
			const auto radius = st::flashgramInventoryCardRadius;
			auto gradient = QRadialGradient(
				QPointF(r.center().x(), r.height() * 0.32),
				r.height() * 0.85);
			gradient.setColorAt(0., colors.first);
			gradient.setColorAt(1., colors.second);
			FillRoundedPath(p, r, radius, gradient);

			auto clip = QPainterPath();
			clip.addRoundedRect(r, radius, radius);
			p.setClipPath(clip);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 14));
			const auto step = st::flashgramInventoryGiftHeight / 4.;
			for (auto row = 0; row * step < r.height(); ++row) {
				for (auto column = 0; column * step < r.width(); ++column) {
					const auto x = column * step + ((row % 2) ? step / 2. : 0.);
					p.drawEllipse(QPointF(x, row * step), 4., 4.);
				}
			}
			p.setClipping(false);

			const auto padding = st::flashgramInventoryCardPadding;
			const auto &font = st::flashgramInventoryNameFont;
			const auto nameTop = padding + st::flashgramInventoryGiftHeight;
			auto nameLeft = padding;
			auto nameWidth = card->width() - 2 * padding;
			if (pinned) {
				const auto &icon = st::menuIconPin;
				icon.paint(
					p,
					padding,
					nameTop + (font->height - icon.height()) / 2,
					card->width(),
					QColor(255, 255, 255, 200));
				nameLeft += icon.width();
				nameWidth -= icon.width();
			}
			p.setFont(font->f);
			p.setPen(QColor(255, 255, 255));
			p.drawText(
				QRect(nameLeft, nameTop, nameWidth, font->height),
				Qt::AlignCenter,
				font->elided(title, nameWidth));

			const auto mark = st::flashgramFgMarkSize;
			const auto markRect = QRect(
				card->width() - padding - mark,
				padding,
				mark,
				mark);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(0, 0, 0, 50));
			p.drawEllipse(markRect);
			p.setFont(st::flashgramFgMarkFont->f);
			p.setPen(QColor(255, 255, 255, 200));
			p.drawText(markRect, Qt::AlignCenter, u"FG"_q);
		}, card->lifetime());

		const auto view = Ui::CreateChild<GiftStickerView>(
			card,
			session,
			*gift);
		view->show();

		const auto uid = owned.uid;
		const auto inProfile = owned.inProfile;
		const auto profile = CreatePill(
			card,
			inProfile
				? Tr("In profile ✓", "В профиле ✓")
				: Tr("Show in profile", "В профиль"),
			false,
			[=] { SetGiftFlag(user, uid, GiftFlag::InProfile, !inProfile); });
		const auto sell = CreatePill(
			card,
			(gift->value.cents > 0)
				? Tr("Sell for %1", "Продать за %1").arg(
					FormatBalance(gift->value))
				: Tr("Sell", "Продать"),
			!GiftBackendAvailable(),
			[=] { controller->show(Box(SellGiftBox, controller, owned)); });
		card->show();
		_cards.push_back({ card, view, profile, sell });
	}
}

int InventoryGrid::resizeGetHeight(int newWidth) {
	const auto skip = st::flashgramInventorySkip;
	const auto width = std::max((newWidth - skip) / kColumns, 1);
	const auto height = st::flashgramInventoryCardHeight;
	const auto padding = st::flashgramInventoryCardPadding;
	const auto buttonHeight = st::flashgramInventoryButtonHeight;
	const auto giftHeight = st::flashgramInventoryGiftHeight;
	for (auto i = 0; i != int(_cards.size()); ++i) {
		const auto &card = _cards[i];
		card.widget->setGeometry(
			(i % kColumns) * (width + skip),
			(i / kColumns) * (height + skip),
			width,
			height);
		card.gift->setGeometry(
			(width - giftHeight) / 2,
			padding,
			giftHeight,
			giftHeight);
		card.sell->setGeometry(
			padding,
			height - padding - buttonHeight,
			width - 2 * padding,
			buttonHeight);
		card.profile->setGeometry(
			padding,
			height
				- padding
				- 2 * buttonHeight
				- st::flashgramInventoryButtonSkip,
			width - 2 * padding,
			buttonHeight);
	}
	const auto rows = (int(_cards.size()) + kColumns - 1) / kColumns;
	return rows ? (rows * height + (rows - 1) * skip) : 0;
}

void AddInventoryHeader(
		not_null<Ui::VerticalLayout*> container,
		const std::vector<OwnedGift> &gifts) {
	const auto count = GiftsCountText(int(gifts.size()));
	const auto value = FormatBalance(CollectionValue(gifts));
	const auto header = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::flashgramInventoryHeaderPadding);
	header->resize(header->width(), st::flashgramInventoryHeaderHeight);
	header->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(header);
		const auto &countFont = st::flashgramInventoryCountFont;
		const auto &subFont = st::flashgramInventorySubFont;
		const auto top = QRect(0, 0, header->width(), countFont->height);
		p.setPen(st::windowSubTextFg->c);
		p.setFont(countFont->f);
		p.drawText(top, int(Qt::AlignLeft | Qt::AlignVCenter), count);
		p.setPen(st::windowFg->c);
		p.setFont(st::flashgramInventoryValueFont->f);
		p.drawText(top, int(Qt::AlignRight | Qt::AlignVCenter), value);
		p.setPen(st::windowSubTextFg->c);
		p.setFont(subFont->f);
		p.drawText(
			QRect(0, countFont->height, header->width(), subFont->height),
			int(Qt::AlignLeft | Qt::AlignVCenter),
			Tr(
				"Collection estimate · updated today",
				"Оценка по коллекции · обновлено сегодня"));
	}, header->lifetime());
}

void AddTitleRow(not_null<Ui::VerticalLayout*> layout, Fn<void()> back) {
	const auto row = layout->add(object_ptr<Ui::RpWidget>(layout));
	row->resize(
		row->width(),
		st::flashgramTopBarHeight + st::flashgramGiftsTitleHeight);
	const auto backButton = Ui::CreateChild<Ui::AbstractButton>(row);
	backButton->setClickedCallback(back);
	const auto backText = Tr("Back", "Назад");
	backButton->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(backButton);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = backButton->rect();
		FillRoundedPath(
			p,
			QRectF(r),
			r.height() / 2.,
			backButton->isOver() ? st::windowBgRipple->c : st::windowBgOver->c);
		const auto icon = st::flashgramCapsuleIcon;
		const auto left = st::flashgramCapsulePadding;
		const auto y = r.height() / 2.;
		p.setPen(QPen(
			st::windowFg->c,
			st::flashgramCapsuleStroke,
			Qt::SolidLine,
			Qt::RoundCap));
		p.drawLine(QPointF(left, y), QPointF(left + icon, y));
		p.drawLine(
			QPointF(left, y),
			QPointF(left + icon * 0.45, y - icon * 0.45));
		p.drawLine(
			QPointF(left, y),
			QPointF(left + icon * 0.45, y + icon * 0.45));
		p.setPen(st::windowFg->c);
		p.setFont(st::flashgramCapsuleFont->f);
		p.drawText(
			QRect(left + icon + left / 2, 0, r.width(), r.height()),
			int(Qt::AlignLeft | Qt::AlignVCenter),
			backText);
	}, backButton->lifetime());
	backButton->show();

	const auto title = Tr("My Gifts", "Мои подарки");
	row->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(row);
		auto hq = PainterHighQualityEnabler(p);
		const auto &font = st::flashgramGiftsBigTitleFont;
		const auto left = st::flashgramTopBarSide;
		const auto top = st::flashgramTopBarHeight + st::flashgramGiftsTitleTop;
		const auto textWidth = font->width(title);
		{
			const auto radius = st::flashgramGiftsTitleHeight * 0.5;
			p.save();
			p.translate(left + textWidth / 2., top + font->height / 2.);
			p.scale((textWidth * 0.65) / radius, 1.);
			auto glow = QRadialGradient(QPointF(), radius);
			glow.setColorAt(0., QColor(0x6A, 0x5C, 0xF0, 80));
			glow.setColorAt(0.6, QColor(0x3A, 0x8C, 0xF0, 30));
			glow.setColorAt(1., QColor(0x3A, 0x8C, 0xF0, 0));
			p.fillRect(QRectF(-radius, -radius, radius * 2, radius * 2), glow);
			p.restore();
		}
		p.setPen(st::windowFg->c);
		p.setFont(font->f);
		p.drawText(
			QRect(left, top, row->width(), font->height),
			int(Qt::AlignLeft | Qt::AlignVCenter),
			title);
	}, row->lifetime());

	row->widthValue() | rpl::on_next([=](int width) {
		const auto &font = st::flashgramCapsuleFont;
		backButton->setGeometry(
			st::flashgramTopBarSide,
			(st::flashgramTopBarHeight - st::flashgramCapsuleHeight) / 2,
			st::flashgramCapsulePadding * 2
				+ st::flashgramCapsuleIcon
				+ st::flashgramCapsulePadding / 2
				+ font->width(backText),
			st::flashgramCapsuleHeight);
	}, row->lifetime());
}

[[nodiscard]] rpl::producer<int> AddSegmentedTabs(
		not_null<Ui::VerticalLayout*> layout) {
	const auto tabs = layout->add(
		object_ptr<Ui::RpWidget>(layout),
		st::flashgramGiftsTabsMargin);
	tabs->resize(tabs->width(), st::flashgramGiftsTabsHeight);
	const auto active = tabs->lifetime().make_state<rpl::variable<int>>(0);
	const auto labels = std::array{ u"Telegram"_q, u"FlashGram"_q };
	auto buttons = std::vector<not_null<Ui::AbstractButton*>>();
	for (auto i = 0; i != 2; ++i) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(tabs);
		button->setClickedCallback([=] {
			*active = i;
			tabs->update();
		});
		button->show();
		buttons.push_back(button);
	}
	tabs->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(tabs);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = QRectF(tabs->rect());
		FillRoundedPath(p, r, r.height() / 2., st::windowBgOver->c);
		const auto inset = st::flashgramGiftsTabsInset;
		const auto half = r.width() / 2.;
		const auto current = active->current();
		const auto pill = QRectF(
			inset + current * half,
			inset,
			half - 2 * inset,
			r.height() - 2 * inset);
		FillRoundedPath(p, pill, pill.height() / 2., st::windowBgRipple->c);
		p.setFont(st::flashgramGiftsTabsFont->f);
		for (auto i = 0; i != 2; ++i) {
			p.setPen((i == current)
				? st::windowFg->c
				: st::windowSubTextFg->c);
			p.drawText(
				QRectF(i * half, 0, half, r.height()),
				Qt::AlignCenter,
				labels[i]);
		}
	}, tabs->lifetime());
	tabs->widthValue() | rpl::on_next([=](int width) {
		const auto half = width / 2;
		buttons[0]->setGeometry(0, 0, half, st::flashgramGiftsTabsHeight);
		buttons[1]->setGeometry(half, 0, width - half, st::flashgramGiftsTabsHeight);
	}, tabs->lifetime());
	return active->value();
}

} // namespace

void MyGiftsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto user = session->user();
	box->setStyle(st::flashgramScreenBox);
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	RequestGiftStickers(session);
	const auto layout = box->verticalLayout();

	AddTitleRow(layout, [=] { box->closeBox(); });
	auto activeTab = AddSegmentedTabs(layout);

	const auto telegramWrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			object_ptr<Ui::VerticalLayout>(layout)));
	const auto telegram = telegramWrap->entity();
	Ui::AddSkip(telegram);
	auto inlineGifts = Info::PeerGifts::MakePeerGiftsInner(
		telegram,
		controller,
		user,
		rpl::single(Info::PeerGifts::Descriptor()));
	telegram->add(std::move(inlineGifts.widget));
	Ui::AddSkip(telegram);

	const auto localWrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			object_ptr<Ui::VerticalLayout>(layout)));
	const auto local = localWrap->entity();
	const auto fillLocal = [=] {
		local->clear();
		const auto profile = LoadProfile(user);
		AddInventoryHeader(local, profile.gifts);
		if (profile.gifts.empty()) {
			Ui::AddDividerText(
				local,
				TrValue(
					"No FlashGram gifts yet. Try Roulette or Cases.",
					"Пока нет подарков FlashGram. Попробуйте рулетку "
					"или кейсы."));
		} else {
			local->add(
				object_ptr<InventoryGrid>(local, controller, profile.gifts),
				st::flashgramInventoryPadding);
		}
		local->resizeToWidth(local->width());
	};
	fillLocal();
	Changes() | rpl::on_next([=] {
		crl::on_main(local, fillLocal);
	}, local->lifetime());

	std::move(activeTab) | rpl::on_next([=](int index) {
		telegramWrap->toggle(index == 0, anim::type::instant);
		localWrap->toggle(index == 1, anim::type::instant);
	}, layout->lifetime());
}

} // namespace FlashGram
