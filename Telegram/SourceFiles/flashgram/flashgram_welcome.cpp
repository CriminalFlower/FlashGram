/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_welcome.h"

#include "base/call_delayed.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "flashgram/flashgram_state.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QSaveFile>

namespace FlashGram {
namespace {

constexpr auto kWelcomeCompletedKey = "welcome_completed"_cs;
constexpr auto kEmojiReplayDelay = crl::time(2400);

[[nodiscard]] QString AppStatePath() {
	return cWorkingDir() + u"tdata/flashgram/app.json"_q;
}

[[nodiscard]] QJsonObject ReadAppState() {
	auto file = QFile(AppStatePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

void WriteAppState(const QJsonObject &object) {
	const auto path = AppStatePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QSaveFile(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
		file.commit();
	}
}

[[nodiscard]] bool WelcomeCompleted() {
	return ReadAppState().value(kWelcomeCompletedKey.utf16()).toBool();
}

void MarkWelcomeCompleted() {
	auto state = ReadAppState();
	state.insert(kWelcomeCompletedKey.utf16(), true);
	WriteAppState(state);
}

// The welcome screen follows the Windows language as well, because a fresh
// install starts with the English Telegram language pack until the user
// switches it.
[[nodiscard]] bool PreferRussian() {
	if (Lang::Id().startsWith(u"ru"_q)) {
		return true;
	}
	const auto languages = QLocale::system().uiLanguages();
	return !languages.isEmpty() && languages.front().startsWith(u"ru"_q);
}

[[nodiscard]] QString WTr(const char *en, const char *ru) {
	return QString::fromUtf8(PreferRussian() ? ru : en);
}

[[nodiscard]] rpl::producer<QString> WTrValue(const char *en, const char *ru) {
	return rpl::single(WTr(en, ru));
}

void OpenEmojiSet(const QString &url) {
	// Opens the set preview through Telegram's own link handling when a
	// session is available, or in the browser otherwise. The user decides
	// whether to add the set, nothing is installed automatically.
	UrlClickHandler::Open(url);
}

void AddLogo(not_null<Ui::VerticalLayout*> layout) {
	const auto logo = layout->add(object_ptr<Ui::RpWidget>(layout));
	logo->resize(logo->width(), st::flashgramWelcomeLogoSize);
	logo->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(logo);
		auto hq = PainterHighQualityEnabler(p);
		const auto size = st::flashgramWelcomeLogoSize;
		p.drawImage(
			QRect((logo->width() - size) / 2, 0, size, size),
			Window::LogoNoMargin());
	}, logo->lifetime());
}

void AddEmojiSetCard(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Main::Session*> session,
		const Server::EmojiSet &set) {
	const auto card = layout->add(
		object_ptr<Ui::RpWidget>(layout),
		st::flashgramWelcomeCardMargin);
	card->resize(card->width(), st::flashgramWelcomeCardHeight);

	const auto button = Ui::CreateChild<Ui::RoundButton>(
		card,
		WTrValue("Open set", "Открыть набор"),
		st::flashgramWelcomeOpenButton);
	const auto url = set.url;
	button->setClickedCallback([=] {
		OpenEmojiSet(url);
	});

	const auto emojiSize = st::flashgramWelcomeEmojiSize;
	const auto custom = set.previewDocumentId
		? card->lifetime().make_state<std::unique_ptr<Ui::Text::CustomEmoji>>(
			session->data().customEmojiManager().create(
				DocumentId(set.previewDocumentId),
				[=] { card->update(); },
				Data::CustomEmojiSizeTag::Isolated,
				emojiSize))
		: nullptr;

	const auto lottie = custom
		? nullptr
		: card->lifetime().make_state<std::unique_ptr<Lottie::Icon>>(
			Lottie::MakeIcon({
				.name = u"greeting"_q,
				.sizeOverride = QSize(emojiSize, emojiSize),
			}));
	const auto replayScheduled = card->lifetime().make_state<bool>(false);
	const auto playLottie = [=] {
		if (lottie && *lottie && (*lottie)->valid()) {
			(*lottie)->animate(
				[=] { card->update(); },
				0,
				(*lottie)->framesCount() - 1);
		}
	};
	playLottie();

	card->widthValue() | rpl::on_next([=](int width) {
		const auto padding = st::flashgramWelcomeCardPadding;
		button->moveToRight(
			padding.right(),
			(st::flashgramWelcomeCardHeight - button->height()) / 2,
			width);
	}, button->lifetime());

	const auto title = set.title;
	const auto caption = u"CUSTOM EMOJI"_q;
	card->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(card);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::flashgramWelcomeCardRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(card->rect(), radius, radius);

		const auto padding = st::flashgramWelcomeCardPadding;
		const auto emojiTop = (card->height() - emojiSize) / 2;
		if (custom && *custom) {
			(*custom)->paint(p, Ui::Text::CustomEmoji::Context{
				.textColor = st::windowFg->c,
				.size = QSize(emojiSize, emojiSize),
				.now = crl::now(),
				.position = QPoint(padding.left(), emojiTop),
			});
		} else if (lottie && *lottie && (*lottie)->valid()) {
			(*lottie)->paint(p, padding.left(), emojiTop);
			if (!(*lottie)->animating() && !*replayScheduled) {
				*replayScheduled = true;
				base::call_delayed(kEmojiReplayDelay, card, [=] {
					*replayScheduled = false;
					playLottie();
				});
			}
		}

		const auto textLeft = padding.left()
			+ emojiSize
			+ st::flashgramWelcomeCardSkip;
		const auto textRight = button->x() - st::flashgramWelcomeCardSkip;
		const auto textWidth = std::max(textRight - textLeft, 0);
		const auto &captionFont = st::flashgramWelcomeCaptionFont;
		const auto &titleFont = st::flashgramWelcomeSetTitleFont;
		const auto textHeight = captionFont->height
			+ st::flashgramWelcomeCardLineSkip
			+ titleFont->height;
		const auto top = (card->height() - textHeight) / 2;

		p.setFont(captionFont);
		p.setPen(st::windowActiveTextFg);
		p.drawText(
			textLeft,
			top + captionFont->ascent,
			captionFont->elided(caption, textWidth));

		p.setFont(titleFont);
		p.setPen(st::windowBoldFg);
		p.drawText(
			textLeft,
			top
				+ captionFont->height
				+ st::flashgramWelcomeCardLineSkip
				+ titleFont->ascent,
			titleFont->elided(title, textWidth));
	}, card->lifetime());
}

void ShowUpdateIfAvailable(not_null<Window::Controller*> window) {
	static auto shown = false;
	if (shown || window->locked()) {
		return;
	}
	if (const auto update = Server::AvailableUpdate()) {
		shown = true;
		window->show(Box(UpdateBox, *update));
	}
}

} // namespace

void OnApplicationStarted(not_null<Window::Controller*> window) {
	Server::Start();

	const auto widget = window->widget();
	const auto flowOpen = widget->lifetime().make_state<bool>(false);
	const auto flowStarted = widget->lifetime().make_state<bool>(false);
	const auto finishFlow = [=] {
		*flowOpen = false;
		if (Server::ConfigLoaded()) {
			ShowUpdateIfAvailable(window);
		}
	};

	// Order after the Telegram login: FlashGram registration (email, only
	// while the server enables it), then the welcome screen. Both need a
	// Telegram session: the account is bound to it and the welcome loads a
	// custom emoji preview.
	window->sessionControllerValue(
	) | rpl::filter([=](Window::SessionController *controller) {
		return controller && !*flowStarted && !window->locked();
	}) | rpl::on_next([=](Window::SessionController *controller) {
		*flowStarted = true;
		const auto weak = base::make_weak(controller);
		const auto userId = controller->session().userId().bare;
		Server::RequestAccount(userId, crl::guard(widget, [=](
				Server::Account account) {
			const auto strong = weak.get();
			if (!strong || window->locked()) {
				return;
			}
			const auto welcome = [=] {
				const auto strong = weak.get();
				if (!strong || WelcomeCompleted()) {
					finishFlow();
					return;
				}
				window->show(Box(
					WelcomeBox,
					&strong->session(),
					crl::guard(widget, finishFlow)));
			};
			if (!account.email.isEmpty()
				|| !Server::EmailRegistrationEnabled()) {
				if (!WelcomeCompleted()) {
					*flowOpen = true;
					welcome();
				}
				return;
			}
			*flowOpen = true;
			window->show(Box(
				RegistrationBox,
				not_null(strong),
				crl::guard(widget, welcome),
				crl::guard(widget, finishFlow)));
		}));
	}, widget->lifetime());

	Server::ConfigUpdates(
	) | rpl::on_next([=] {
		if (!*flowOpen) {
			ShowUpdateIfAvailable(window);
		}
	}, widget->lifetime());
}

void RegistrationBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void()> registered,
		Fn<void()> dismissed) {
	box->setTitle(WTrValue(
		"FlashGram registration",
		"Регистрация в FlashGram"));
	box->setWidth(st::boxWideWidth);
	box->setCloseByOutsideClick(false);
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto show = controller->uiShow();
	const auto userId = controller->session().userId().bare;
	const auto succeeded = box->lifetime().make_state<bool>(false);
	box->boxClosing() | rpl::on_next([=] {
		if (!*succeeded && dismissed) {
			dismissed();
		}
	}, box->lifetime());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			WTrValue(
				"Link an email to your FlashGram account to keep your "
				"FlashGram ID, balance and gifts.",
				"Привяжите почту к аккаунту FlashGram, чтобы сохранить "
				"FlashGram ID, баланс и подарки."),
			st::boxLabel));
	Ui::AddSkip(box->verticalLayout());

	const auto email = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		WTrValue("Email", "Электронная почта"),
		QString()));
	const auto codeWrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::InputField>>(
			box,
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				WTrValue("Code from the email", "Код из письма"),
				QString())));
	codeWrap->toggle(false, anim::type::instant);
	const auto code = codeWrap->entity();

	Ui::AddSkip(box->verticalLayout());
	const auto github = box->addRow(object_ptr<Ui::RoundButton>(
		box,
		WTrValue("Connect with GitHub", "Подключиться через GitHub"),
		st::flashgramWelcomeOpenButton));
	box->verticalLayout()->widthValue() | rpl::on_next([=](int width) {
		const auto padding = st::boxRowPadding;
		github->setFullWidth(width - padding.left() - padding.right());
	}, github->lifetime());
	github->setClickedCallback([=] {
		show->showToast(WTr(
			"This feature will arrive in the next update.",
			"Эта функция появится в новом обновлении."));
	});

	const auto codeSent = box->lifetime().make_state<rpl::variable<bool>>(
		false);
	const auto busy = box->lifetime().make_state<bool>(false);
	const auto sentTo = box->lifetime().make_state<QString>();
	const auto submit = [=] {
		if (*busy) {
			return;
		}
		static const auto kEmail = QRegularExpression(
			u"^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$"_q);
		const auto address = email->getLastText().trimmed().toLower();
		if (!kEmail.match(address).hasMatch()) {
			email->showError();
			return;
		}
		if (!codeSent->current() || address != *sentTo) {
			*busy = true;
			Server::RequestEmailCode(address, crl::guard(box, [=](
					QString error) {
				*busy = false;
				// A rate limit here means a code for this address was sent
				// moments ago, so the code field is shown instead of an error.
				const auto alreadySent = (error == u"rate_limited"_q);
				if (!error.isEmpty() && !alreadySent) {
					show->showToast(ServerErrorText(error));
					return;
				}
				*sentTo = address;
				*codeSent = true;
				codeWrap->toggle(true, anim::type::normal);
				code->setFocusFast();
				show->showToast(alreadySent
					? WTr(
						"A code was already sent to %1. Enter the code from "
						"the latest email.",
						"Код уже отправлен на %1. Введите код из последнего "
						"письма.").arg(address)
					: WTr(
						"We sent a code to %1.",
						"Мы отправили код на %1.").arg(address));
			}));
			return;
		}
		const auto value = code->getLastText().trimmed();
		if (value.size() < 6) {
			code->showError();
			return;
		}
		*busy = true;
		Server::VerifyEmailCode(userId, address, value, crl::guard(box, [=](
				QString error) {
			*busy = false;
			if (!error.isEmpty()) {
				show->showToast(ServerErrorText(error));
				return;
			}
			*succeeded = true;
			show->showToast(WTr(
				"Email linked to FlashGram.",
				"Почта привязана к FlashGram."));
			const auto callback = registered;
			box->closeBox();
			if (callback) {
				callback();
			}
		}));
	};
	box->addButton(
		codeSent->value() | rpl::map([](bool sent) {
			return sent
				? WTr("Confirm", "Подтвердить")
				: WTr("Get code", "Получить код");
		}),
		submit);
}

void WelcomeBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		Fn<void()> done) {
	box->setStyle(st::flashgramScreenBox);
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	box->setCloseByOutsideClick(false);
	box->setCloseByEscape(false);

	const auto finished = box->lifetime().make_state<bool>(false);
	const auto finish = [=] {
		if (*finished) {
			return;
		}
		*finished = true;
		MarkWelcomeCompleted();
		if (done) {
			done();
		}
	};
	box->boxClosing() | rpl::on_next(finish, box->lifetime());

	const auto layout = box->verticalLayout();
	Ui::AddSkip(layout, st::flashgramWelcomeTop);
	AddLogo(layout);

	layout->add(
		object_ptr<Ui::FlatLabel>(
			layout,
			WTrValue("Welcome to FlashGram", "Добро пожаловать в FlashGram"),
			st::flashgramWelcomeTitle),
		st::flashgramWelcomeTitlePadding,
		style::al_top);
	layout->add(
		object_ptr<Ui::FlatLabel>(
			layout,
			WTrValue(
				"A fast Telegram client\nwith extra FlashGram features.",
				"Быстрый Telegram-клиент\nс дополнительными функциями FlashGram."),
			st::flashgramWelcomeText),
		st::flashgramWelcomeTextPadding,
		style::al_top);

	const auto sets = Server::EmojiSets();
	AddEmojiSetCard(layout, session, sets.front());

	const auto button = layout->add(
		object_ptr<Ui::RoundButton>(
			layout,
			WTrValue("LET'S GO", "ПОЕХАЛИ"),
			st::flashgramWelcomeButton),
		st::flashgramWelcomeButtonMargin);
	layout->widthValue() | rpl::on_next([=](int width) {
		const auto margin = st::flashgramWelcomeButtonMargin;
		button->setFullWidth(width - margin.left() - margin.right());
	}, button->lifetime());
	button->setClickedCallback([=] {
		finish();
		box->closeBox();
	});
}

void EmojiSetsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setTitle(WTrValue("Emoji & Stickers", "Эмодзи и стикеры"));
	box->setWidth(st::boxWideWidth);

	const auto layout = box->verticalLayout();
	layout->add(
		object_ptr<Ui::FlatLabel>(
			layout,
			WTrValue(
				"Sets selected by FlashGram. Opening a set only shows "
				"its preview, you choose whether to add it.",
				"Наборы, выбранные FlashGram. Открытие набора только "
				"показывает его, добавлять или нет — решаете вы."),
			st::boxDividerLabel),
		st::flashgramEmojiSetsAboutPadding);
	for (const auto &set : Server::EmojiSets()) {
		AddEmojiSetCard(layout, session, set);
	}
	Ui::AddSkip(layout);

	box->addButton(WTrValue("Close", "Закрыть"), [=] { box->closeBox(); });
}

void UpdateBox(not_null<Ui::GenericBox*> box, Server::Release release) {
	box->setTitle(Server::UpdateRequired()
		? WTrValue(
			"FlashGram update required",
			"Требуется обновление FlashGram")
		: WTrValue(
			"FlashGram update available",
			"Доступно обновление FlashGram"));
	box->setWidth(st::boxWideWidth);

	auto text = tr::bold(u"FlashGram "_q + release.version);
	if (!release.notes.isEmpty()) {
		text.append(u"\n\n"_q).append(release.notes);
	}
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(text),
			st::boxLabel));

	const auto download = release.downloadUrl;
	box->addButton(WTrValue("Download", "Скачать"), [=] {
		if (Server::IsTrustedReleaseUrl(download)) {
			File::OpenUrl(download);
		}
		box->closeBox();
	});
	box->addButton(WTrValue("Details", "Подробнее"), [=] {
		File::OpenUrl(Server::ReleasesPageUrl());
	});
}

} // namespace FlashGram
