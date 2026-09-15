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
#include "ui/widgets/discrete_sliders.h"
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

not_null<Ui::AbstractButton*> CreatePill(
		not_null<QWidget*> parent,
		QString text,
		bool dimmed,
		Fn<void()> callback) {
	const auto button = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	button->setClickedCallback(std::move(callback));
	button->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(button);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = button->rect();
		auto bg = st::windowBgActive->c;
		bg.setAlpha(button->isOver() ? 40 : 26);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(r, r.height() / 2., r.height() / 2.);
		auto fg = st::windowActiveTextFg->c;
		if (dimmed) {
			fg.setAlphaF(0.55);
		}
		p.setPen(fg);
		p.setFont(st::flashgramInventoryButtonFont->f);
		p.drawText(r, Qt::AlignCenter, text);
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
		not_null<Ui::RpWidget*> widget;
		not_null<LocalGiftView*> gift;
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
		const auto card = Ui::CreateChild<Ui::RpWidget>(this);
		card->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(card);
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = st::flashgramInventoryCardRadius;
			auto path = QPainterPath();
			path.addRoundedRect(QRectF(card->rect()), radius, radius);
			p.fillPath(path, st::windowBgOver->c);
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
					st::windowSubTextFg->c);
				nameLeft += icon.width();
				nameWidth -= icon.width();
			}
			p.setFont(font->f);
			p.setPen(st::windowFg->c);
			p.drawText(
				QRect(nameLeft, nameTop, nameWidth, font->height),
				Qt::AlignCenter,
				font->elided(title, nameWidth));
		}, card->lifetime());

		const auto view = Ui::CreateChild<LocalGiftView>(
			card,
			session,
			*gift,
			number,
			true);
		view->setClickedCallback([=] {
			controller->show(Box(LocalGiftDetailsBox, controller, owned, false));
		});
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
	for (auto i = 0; i != int(_cards.size()); ++i) {
		const auto &card = _cards[i];
		card.widget->setGeometry(
			(i % kColumns) * (width + skip),
			(i / kColumns) * (height + skip),
			width,
			height);
		card.gift->setGeometry(
			padding,
			padding,
			width - 2 * padding,
			st::flashgramInventoryGiftHeight);
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
		p.setPen(st::windowFg->c);
		p.setFont(countFont->f);
		p.drawText(top, int(Qt::AlignLeft | Qt::AlignVCenter), count);
		p.setFont(st::flashgramInventoryValueFont->f);
		p.drawText(top, int(Qt::AlignRight | Qt::AlignVCenter), value);
		p.setPen(st::windowSubTextFg->c);
		p.setFont(subFont->f);
		p.drawText(
			QRect(0, countFont->height, header->width(), subFont->height),
			int(Qt::AlignLeft | Qt::AlignVCenter),
			Tr("Collection estimate", "Оценка по коллекции"));
	}, header->lifetime());
}

} // namespace

void MyGiftsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto user = session->user();
	box->setTitle(TrValue("My Gifts", "Мои подарки"));
	box->setWidth(st::boxWideWidth);
	RequestGiftStickers(session);

	const auto tabs = box->addRow(
		object_ptr<Ui::SettingsSlider>(box, st::defaultTabsSlider),
		QMargins());
	tabs->setSections({ u"Telegram"_q, u"FlashGram"_q });

	const auto telegramWrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		QMargins());
	const auto telegram = telegramWrap->entity();
	Ui::AddSkip(telegram);
	auto inlineGifts = Info::PeerGifts::MakePeerGiftsInner(
		telegram,
		controller,
		user,
		rpl::single(Info::PeerGifts::Descriptor()));
	telegram->add(std::move(inlineGifts.widget));
	Ui::AddSkip(telegram);

	const auto localWrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		QMargins());
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

	telegramWrap->toggle(true, anim::type::instant);
	localWrap->toggle(false, anim::type::instant);
	tabs->sectionActivated() | rpl::on_next([=](int index) {
		telegramWrap->toggle(index == 0, anim::type::instant);
		localWrap->toggle(index == 1, anim::type::instant);
	}, tabs->lifetime());

	box->addButton(TrValue("Roulette", "Рулетка"), [=] {
		controller->show(Box(RouletteBox, controller));
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

} // namespace FlashGram
