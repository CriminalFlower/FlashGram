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
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace FlashGram {

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
	Ui::AddDividerText(
		telegram,
		TrValue(
			"Real Telegram Gifts and collectibles loaded from Telegram "
			"servers. Source: Telegram.",
			"Настоящие Telegram Gifts и коллекционные подарки с серверов "
			"Telegram. Источник: Telegram."));

	const auto localWrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		QMargins());
	const auto local = localWrap->entity();
	const auto open = [=](OwnedGift owned) {
		controller->show(Box(LocalGiftDetailsBox, controller, owned, false));
	};
	const auto fillLocal = [=] {
		local->clear();
		const auto profile = LoadProfile(user);
		Ui::AddSkip(local);
		if (profile.gifts.empty()) {
			Ui::AddDividerText(
				local,
				TrValue(
					"No FlashGram Local gifts yet. Try Roulette or Cases.",
					"Пока нет подарков FlashGram Local. Попробуйте "
					"рулетку или кейсы."));
		} else {
			local->add(
				object_ptr<LocalGiftsGrid>(
					local,
					session,
					profile.gifts,
					open),
				st::flashgramBoxGiftsPadding);
		}
		Ui::AddDividerText(
			local,
			TrValue(
				"FlashGram Local gifts are cosmetic and stored only on "
				"this device. Source: FlashGram Local.",
				"Подарки FlashGram Local косметические и хранятся только "
				"на этом устройстве. Источник: FlashGram Local."));
		local->resizeToWidth(local->width());
	};
	fillLocal();
	Changes() | rpl::on_next(fillLocal, local->lifetime());

	telegramWrap->toggle(true, anim::type::instant);
	localWrap->toggle(false, anim::type::instant);
	tabs->sectionActivated() | rpl::on_next([=](int index) {
		telegramWrap->toggle(index == 0, anim::type::instant);
		localWrap->toggle(index == 1, anim::type::instant);
	}, tabs->lifetime());

	box->addButton(TrValue("Roulette", "Рулетка"), [=] {
		controller->show(Box(LootBox, controller));
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

} // namespace FlashGram
