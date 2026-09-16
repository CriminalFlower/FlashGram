/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_profile.h"

#include "data/data_user.h"
#include "flashgram/flashgram_cover.h"
#include "flashgram/flashgram_gift_view.h"
#include "flashgram/flashgram_identity.h"
#include "flashgram/flashgram_loot.h"
#include "flashgram/flashgram_server.h"
#include "flashgram/flashgram_state.h"
#include "flashgram/flashgram_verification.h"
#include "flashgram/flashgram_welcome.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
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
	AddOrgVerificationCard(container, controller, user);
	AddUsernamesCard(container, controller, user);

	const auto shownId = container->lifetime().make_state<
		rpl::variable<QString>>(flashgramId);
	const auto linkedEmail = container->lifetime().make_state<
		rpl::variable<QString>>();
	if (self) {
		const auto userId = peerToUser(user->id).bare;
		const auto owner = profile.owner;
		const auto apply = [=](const Server::Account &account) {
			if (!owner) {
				*shownId = account.flashgramId;
			}
			*linkedEmail = account.email;
		};
		Server::RequestAccount(userId, crl::guard(container, apply));
		Server::AccountUpdates(
		) | rpl::on_next([=] {
			if (const auto account = Server::CachedAccount(userId)) {
				apply(*account);
			}
		}, container->lifetime());
	}
	const auto idButton = Settings::AddButtonWithLabel(
		container,
		profile.owner
			? TrValue("Support ID", "ID поддержки")
			: tr::lng_flashgram_id(),
		shownId->value(),
		st::settingsButton,
		{ .icon = &st::menuIconProfile });
	idButton->addClickHandler([=] {
		QGuiApplication::clipboard()->setText(shownId->current());
		show->showToast(Tr(
			"FlashGram ID copied to clipboard.",
			"FlashGram ID скопирован."));
	});

	if (self) {
		Settings::AddButtonWithLabel(
			container,
			rpl::single(u"FlashGram Server"_q),
			Server::StatusValue() | rpl::map([](Server::Status status) {
				switch (status) {
				case Server::Status::Online:
					return Server::MaintenanceMode()
						? Tr("● Maintenance", "● Обслуживание")
						: u"● Online"_q;
				case Server::Status::Offline:
					return u"○ Offline"_q;
				case Server::Status::Checking:
					return Tr("Checking...", "Проверка...");
				}
				Unexpected("Status in FlashGram server row.");
			}),
			st::settingsButton,
			{ .icon = &st::menuIconIpAddress }
		)->addClickHandler([] {
			Server::RefreshHealth();
		});

		const auto userId = peerToUser(user->id).bare;
		Server::RequestVerification(userId);
		auto verificationValue = rpl::single(
			rpl::empty
		) | rpl::then(
			Server::VerificationUpdates()
		) | rpl::map([=] {
			return Server::CachedVerification(userId);
		});
		const auto verifiedWrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
				container,
				object_ptr<Ui::RpWidget>(container),
				st::flashgramBadgesPadding));
		const auto verifiedRow = verifiedWrap->entity();
		verifiedRow->resize(verifiedRow->width(), st::flashgramBadgeHeight);
		const auto verifiedLevel = verifiedRow->lifetime().make_state<
			QString>();
		verifiedRow->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(verifiedRow);
			const auto height = st::flashgramBadgeHeight;
			const auto width = VerificationBadgeWidth(height);
			PaintVerificationBadge(
				p,
				QRectF(0, 0, width, height),
				*verifiedLevel);
			p.setPen(st::windowFg->c);
			p.setFont(st::flashgramBadgeFont->f);
			p.drawText(
				QRect(
					width + st::flashgramBadgeSkip,
					0,
					verifiedRow->width(),
					height),
				int(Qt::AlignLeft | Qt::AlignVCenter),
				VerificationLevelName(*verifiedLevel)
					+ Tr(" · Verified by FlashGram", " · подтверждено FlashGram"));
		}, verifiedRow->lifetime());
		verifiedWrap->toggleOn(rpl::duplicate(
			verificationValue
		) | rpl::map([=](const Server::Verification *verification) {
			if (verification) {
				for (const auto &request : verification->requests) {
					if (request.status == u"approved"_q) {
						*verifiedLevel = request.level;
						verifiedRow->update();
						return true;
					}
				}
			}
			return false;
		}), anim::type::instant);

		Settings::AddButtonWithLabel(
			container,
			rpl::single(u"FlashGram Verification"_q),
			rpl::duplicate(
				verificationValue
			) | rpl::map([=](const Server::Verification *verification) {
				if (!verification) {
					return Tr("Not verified", "Не подтверждено");
				}
				auto pending = false;
				for (const auto &request : verification->requests) {
					if (request.status == u"approved"_q) {
						return u"Verified · FG"_q;
					} else if (request.status == u"pending"_q) {
						pending = true;
					}
				}
				return pending
					? Tr("Under review", "На рассмотрении")
					: Tr("Not verified", "Не подтверждено");
			}),
			st::settingsButton,
			{ .icon = &st::menuIconAdmin }
		)->addClickHandler([=] {
			controller->show(Box(VerificationBox, controller));
		});

		const auto adminWrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		Settings::AddButtonWithIcon(
			adminWrap->entity(),
			TrValue("Verification Requests", "Заявки на верификацию"),
			st::settingsButton,
			{ .icon = &st::menuIconInfo }
		)->addClickHandler([=] {
			controller->show(Box(VerificationRequestsBox, controller));
		});
		Settings::AddButtonWithIcon(
			adminWrap->entity(),
			TrValue("Major / Hold badges", "Бейджи Major / Hold"),
			st::settingsButton,
			{ .icon = &st::menuIconAdmin }
		)->addClickHandler([=] {
			controller->show(Box(OrgVerificationGrantBox, controller));
		});
		adminWrap->toggleOn(std::move(
			verificationValue
		) | rpl::map([](const Server::Verification *verification) {
			return verification && verification->admin;
		}), anim::type::instant);

		Settings::AddButtonWithIcon(
			container,
			TrValue("Emoji & Stickers", "Эмодзи и стикеры"),
			st::settingsButton,
			{ .icon = &st::menuIconEmoji }
		)->addClickHandler([=] {
			controller->show(Box(EmojiSetsBox, session));
		});

		Settings::AddButtonWithLabel(
			container,
			TrValue("Email", "Почта"),
			rpl::combine(
				linkedEmail->value(),
				rpl::single(rpl::empty) | rpl::then(Server::ConfigUpdates())
			) | rpl::map([](const QString &email, auto) {
				return !email.isEmpty()
					? email
					: Server::EmailRegistrationEnabled()
					? Tr("Link", "Привязать")
					: Tr("Optional", "Необязательно");
			}),
			st::settingsButton,
			{ .icon = &st::menuIconInfo }
		)->addClickHandler([=] {
			if (!linkedEmail->current().isEmpty()) {
				return;
			} else if (!Server::EmailRegistrationEnabled()) {
				show->showToast(Tr(
					"Email is optional: FlashGram ID, balance and gifts work "
					"without it.",
					"Почта необязательна: FlashGram ID, баланс и подарки "
					"работают без неё."));
				return;
			}
			controller->show(Box(
				RegistrationBox,
				controller,
				Fn<void()>(),
				Fn<void()>()));
		});

		Settings::AddButtonWithLabel(
			container,
			TrValue("FlashGram version", "Версия FlashGram"),
			rpl::single(Server::ClientVersion()),
			st::settingsButton,
			{ .icon = &st::menuIconInfo });
	}

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
		? Tr("None", "Нет")
		: profile.badges.join(u", "_q);
	const auto badgesButton = Settings::AddButtonWithLabel(
		container,
		TrValue("Badges", "Значки"),
		Value(badgesText),
		st::settingsButton,
		{ .icon = &st::menuIconInfo });
	badgesButton->addClickHandler([=] {
		show->showToast(badgesText);
	});

	if (self) {
		auto phoneMode = rpl::single(rpl::empty) | rpl::then(Changes());
		Settings::AddButtonWithLabel(
			container,
			TrValue("Shown in FlashGram", "Номер в FlashGram"),
			rpl::duplicate(phoneMode) | rpl::map([=] {
				return DisplayedPhone(user);
			}),
			st::settingsButton,
			{ .icon = &st::menuIconStealth }
		)->addClickHandler([=] {
			controller->show(Box(PhoneDisplayBox, user));
		});
		Settings::AddButtonWithLabel(
			container,
			TrValue("Anonymous Mode", "Анонимный режим"),
			std::move(phoneMode) | rpl::map([=] {
				switch (LoadPhoneDisplay(user)) {
				case PhoneDisplay::Masked:
					return Tr("Anonymous phone", "Анонимный номер");
				case PhoneDisplay::FlashGramId: return u"FlashGram ID"_q;
				case PhoneDisplay::Real: break;
				}
				return Tr("Real phone", "Реальный номер");
			}),
			st::settingsButton,
			{ .icon = &st::menuIconStealth }
		)->addClickHandler([=] {
			controller->show(Box(PhoneDisplayBox, user));
		});

		Settings::AddButtonWithLabel(
			container,
			TrValue("Profile background", "Фон профиля"),
			ProfileCoverChanges(
			) | rpl::map([=] {
				return HasProfileCover(user)
					? Tr("Photo", "Фото")
					: Tr("Not set", "Не выбран");
			}) | rpl::type_erased,
			st::settingsButton,
			{ .icon = &st::menuIconPhoto }
		)->addClickHandler([=] {
			ChooseProfileCover(controller);
		});
		const auto removeCover = container->add(
			object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
				container,
				object_ptr<Ui::SettingsButton>(
					container,
					TrValue(
						"Remove profile background",
						"Убрать фон профиля"),
					st::settingsAttentionButton)));
		removeCover->toggleOn(ProfileCoverChanges(
		) | rpl::map([=] {
			return HasProfileCover(user);
		}));
		removeCover->finishAnimating();
		removeCover->entity()->addClickHandler([=] {
			RemoveProfileCover(user);
		});

		Settings::AddButtonWithIcon(
			container,
			TrValue("Buy NFT Gifts", "Купить NFT-подарки"),
			st::settingsButton,
			{ .icon = &st::menuIconShop }
		)->addClickHandler([=] {
			controller->show(Box(NftStoreBox, controller));
		});

		if (!profile.ownerForced) {
			const auto ownerToggle = Settings::AddButtonWithIcon(
				container,
				TrValue("Owner Mode", "Режим владельца"),
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
	Ui::AddDividerText(container, TrValue(
		"FlashGram data is stored locally on this device. "
		"It is separate from Telegram Stars, Telegram Gifts and your "
		"Telegram account, and your phone number privacy settings "
		"are not changed.",
		"Данные FlashGram хранятся на этом устройстве. Они не связаны "
		"со звёздами, подарками и аккаунтом Telegram, а настройки "
		"приватности номера не меняются."));
}

} // namespace

void AddProfileSection(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	if (user->isBot()) {
		return;
	} else if (!HasProfile(user)) {
		AddOrgVerificationCard(container, controller, user);
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
