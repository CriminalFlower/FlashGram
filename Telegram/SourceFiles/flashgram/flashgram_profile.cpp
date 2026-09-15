/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_profile.h"

#include "data/data_user.h"
#include "flashgram/flashgram_gift_view.h"
#include "flashgram/flashgram_loot.h"
#include "flashgram/flashgram_state.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace FlashGram {
namespace {

constexpr auto kProfilePreviewCount = 6;

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

[[nodiscard]] rpl::producer<QString> Value(const QString &text) {
	return rpl::single(text);
}

void FillSection(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user,
		Fn<void()> rebuild) {
	const auto show = controller->uiShow();
	const auto session = &controller->session();
	const auto profile = LoadProfile(user);
	const auto flashgramId = profile.flashgramId;
	const auto self = user->isSelf();

	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_flashgram_section());

	if (profile.owner) {
		auto header = tr::bold(profile.displayName);
		if (!profile.bio.isEmpty()) {
			header.append(u"\n"_q).append(profile.bio);
		}
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::producer<TextWithEntities>(rpl::single(header)),
				st::boxDividerLabel),
			st::flashgramAboutPadding);
	}
	if (!profile.badges.isEmpty()) {
		Ui::AddSubsectionTitle(
			container,
			TrValue("FlashGram badges", "Бейджи FlashGram"));
		AddBadges(container, profile.badges);
	}

	const auto idButton = Settings::AddButtonWithLabel(
		container,
		profile.owner
			? tr::lng_flashgram_support_id()
			: tr::lng_flashgram_id(),
		Value(flashgramId),
		st::settingsButton,
		{ .icon = &st::menuIconProfile });
	idButton->addClickHandler([=] {
		QGuiApplication::clipboard()->setText(flashgramId);
		show->showToast(tr::lng_flashgram_id_copied(tr::now));
	});

	if (self) {
		const auto balanceButton = Settings::AddButtonWithLabel(
			container,
			rpl::single(u"FlashGram Balance"_q),
			rpl::single(rpl::empty) | rpl::then(Changes()) | rpl::map([=] {
				return FormatBalance(LoadProfile(user).balance);
			}),
			st::settingsButton,
			{ .icon = &st::menuIconEarn });
		balanceButton->addClickHandler([=] {
			controller->show(Box(LootBox, controller));
		});

		const auto rouletteButton = Settings::AddButtonWithIcon(
			container,
			TrValue("Roulette and Cases", "Рулетка и кейсы"),
			st::settingsButton,
			{ .icon = &st::menuIconStar });
		rouletteButton->addClickHandler([=] {
			controller->show(Box(LootBox, controller));
		});

		const auto giftsButton = Settings::AddButtonWithLabel(
			container,
			TrValue("Gifts", "Подарки"),
			rpl::single(rpl::empty) | rpl::then(Changes()) | rpl::map([=] {
				return QString::number(LoadProfile(user).gifts.size());
			}),
			st::settingsButton,
			{ .icon = &st::menuIconGiftPremium });
		giftsButton->addClickHandler([=] {
			controller->show(Box(MyGiftsBox, controller));
		});
	}

	const auto badgesText = profile.badges.isEmpty()
		? tr::lng_flashgram_badges_none(tr::now)
		: profile.badges.join(u", "_q);
	const auto badgesButton = Settings::AddButtonWithLabel(
		container,
		tr::lng_flashgram_badges(),
		Value(badgesText),
		st::settingsButton,
		{ .icon = &st::menuIconInfo });
	badgesButton->addClickHandler([=] {
		show->showToast(badgesText);
	});

	if (self) {
		const auto anonymous = container->lifetime().make_state<
			rpl::variable<bool>>(profile.anonymousDisplay);
		const auto phone = user->phone();
		Settings::AddButtonWithLabel(
			container,
			tr::lng_flashgram_shown_number(),
			anonymous->value() | rpl::map([=](bool hidden) {
				return (hidden || phone.isEmpty())
					? flashgramId
					: Ui::FormatPhone(phone);
			}),
			st::settingsButton,
			{ .icon = &st::menuIconStealth });

		const auto anonymousToggle = Settings::AddButtonWithIcon(
			container,
			tr::lng_flashgram_anonymous(),
			st::settingsButton,
			{ .icon = &st::menuIconStealth });
		anonymousToggle->toggleOn(rpl::single(profile.anonymousDisplay));
		anonymousToggle->toggledChanges(
		) | rpl::on_next([=](bool enabled) {
			*anonymous = enabled;
			SaveAccountFlag(user, u"anonymousDisplay"_q, enabled);
		}, anonymousToggle->lifetime());

		if (!profile.ownerForced) {
			const auto ownerToggle = Settings::AddButtonWithIcon(
				container,
				tr::lng_flashgram_owner_mode(),
				st::settingsButton,
				{ .icon = &st::menuIconEarn });
			ownerToggle->toggleOn(rpl::single(profile.ownerProfileEnabled));
			ownerToggle->toggledChanges(
			) | rpl::on_next([=](bool enabled) {
				SaveAccountFlag(user, u"ownerProfile"_q, enabled);
				rebuild();
			}, ownerToggle->lifetime());
		}

		auto preview = std::vector<OwnedGift>();
		for (const auto &owned : profile.gifts) {
			if (owned.inProfile
				&& int(preview.size()) < kProfilePreviewCount) {
				preview.push_back(owned);
			}
		}
		if (!preview.empty()) {
			Ui::AddSkip(container);
			container->add(
				object_ptr<LocalGiftsGrid>(
					container,
					session,
					std::move(preview),
					[=](OwnedGift owned) {
						controller->show(Box(
							LocalGiftDetailsBox,
							controller,
							owned,
							false));
					}),
				st::flashgramGiftsPadding);
		}
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_flashgram_about());
}

} // namespace

void AddProfileSection(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	if (user->isBot() || !HasProfile(user)) {
		return;
	}
	const auto inner = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto rebuild = inner->lifetime().make_state<Fn<void()>>();
	*rebuild = [=] {
		inner->clear();
		FillSection(inner, controller, user, [=] {
			crl::on_main(inner, [=] {
				(*rebuild)();
			});
		});
		inner->resizeToWidth(container->width());
	};
	(*rebuild)();
}

} // namespace FlashGram
