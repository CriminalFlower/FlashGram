/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_verification.h"

#include "base/unique_qptr.h"
#include "base/unixtime.h"
#include "data/data_user.h"
#include "flashgram/flashgram_identity.h"
#include "flashgram/flashgram_server.h"
#include "flashgram/flashgram_state.h"
#include "flashgram/flashgram_ui.h"
#include "flashgram/flashgram_welcome.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/userpic_view.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QRegularExpression>
#include <QtGui/QPainterPath>

namespace FlashGram {
namespace {

using namespace Design;

constexpr auto kTypeChannel = 0;
constexpr auto kTypePersonal = 1;
constexpr auto kTypeBot = 2;
constexpr auto kPageVerification = 0;
constexpr auto kPageTop = 1;
constexpr auto kPageCatalog = 2;
constexpr auto kPageProfile = 3;
constexpr auto kTabsDuration = crl::time(220);

[[nodiscard]] QString TypeKey(int type) {
	return (type == kTypeChannel)
		? u"channel"_q
		: (type == kTypeBot)
		? u"bot"_q
		: u"personal"_q;
}

[[nodiscard]] int TypeIndex(const QString &key) {
	return (key == u"channel"_q)
		? kTypeChannel
		: (key == u"bot"_q)
		? kTypeBot
		: kTypePersonal;
}

[[nodiscard]] QString TypeName(int type) {
	return (type == kTypeChannel)
		? Tr("Channel", "Канал")
		: (type == kTypeBot)
		? Tr("Bot", "Бот")
		: Tr("Personal", "Личный");
}

[[nodiscard]] QString LevelShort(const QString &level) {
	if (level == u"creator"_q) {
		return u"Creator"_q;
	} else if (level == u"business"_q) {
		return u"Business"_q;
	} else if (level == u"developer"_q) {
		return u"Developer"_q;
	} else if (level == u"partner"_q) {
		return u"Partner"_q;
	} else if (level == u"support"_q) {
		return u"Support"_q;
	} else if (level == u"major"_q) {
		return u"Major"_q;
	} else if (level == u"hold"_q) {
		return u"Hold"_q;
	}
	return u"Verified"_q;
}

[[nodiscard]] QString StatusName(const QString &status) {
	if (status == u"pending"_q) {
		return Tr("Under review", "На рассмотрении");
	} else if (status == u"approved"_q) {
		return Tr("Approved", "Одобрена");
	} else if (status == u"rejected"_q) {
		return Tr("Rejected", "Отклонена");
	} else if (status == u"revoked"_q) {
		return Tr("Revoked", "Отозвана");
	}
	return status;
}

[[nodiscard]] QColor StatusColor(const QString &status) {
	if (status == u"approved"_q) {
		return C().green;
	} else if (status == u"pending"_q) {
		return C().lavender;
	}
	return C().red;
}

[[nodiscard]] std::pair<QColor, QColor> LevelColors(const QString &level) {
	if (level == u"creator"_q) {
		return { QColor(0xFF, 0x6F, 0xA8), QColor(0x9D, 0x5C, 0xFF) };
	} else if (level == u"business"_q) {
		return { QColor(0xF7, 0xBE, 0x3F), QColor(0xF0, 0x78, 0x3A) };
	} else if (level == u"developer"_q) {
		return { QColor(0x35, 0xC7, 0x6E), QColor(0x14, 0x9A, 0x8F) };
	} else if (level == u"partner"_q) {
		return { QColor(0xFF, 0x8C, 0x3F), QColor(0xE6, 0x44, 0x6D) };
	} else if (level == u"support"_q) {
		return { QColor(0x40, 0xA2, 0xFF), QColor(0x3F, 0x5B, 0xFF) };
	} else if (level == u"major"_q) {
		return { QColor(0xFF, 0x6B, 0x5B), QColor(0xC2, 0x1F, 0x5B) };
	} else if (level == u"hold"_q) {
		return { QColor(0x33, 0xD6, 0xA6), QColor(0x14, 0x7C, 0xD6) };
	}
	return { QColor(0x1C, 0xC8, 0xB4), QColor(0x6B, 0x5C, 0xFF) };
}

[[nodiscard]] QString FormatDate(TimeId date) {
	return date
		? base::unixtime::parse(date).toString(u"dd.MM.yyyy"_q)
		: QString();
}

[[nodiscard]] QString Handle(const QString &username) {
	return username.isEmpty() ? QString() : (u"@"_q + username);
}

[[nodiscard]] QPainterPath ShieldPath(const QRectF &r) {
	const auto x = r.x();
	const auto y = r.y();
	const auto w = r.width();
	const auto h = r.height();
	auto path = QPainterPath();
	path.moveTo(x + w * 0.5, y);
	path.cubicTo(
		x + w * 0.7, y + h * 0.1,
		x + w * 0.88, y + h * 0.12,
		x + w, y + h * 0.14);
	path.lineTo(x + w, y + h * 0.48);
	path.cubicTo(
		x + w, y + h * 0.76,
		x + w * 0.74, y + h * 0.92,
		x + w * 0.5, y + h);
	path.cubicTo(
		x + w * 0.26, y + h * 0.92,
		x, y + h * 0.76,
		x, y + h * 0.48);
	path.lineTo(x, y + h * 0.14);
	path.cubicTo(
		x + w * 0.12, y + h * 0.12,
		x + w * 0.3, y + h * 0.1,
		x + w * 0.5, y);
	path.closeSubpath();
	return path;
}

[[nodiscard]] QPainterPath StarPath(QPointF center, float64 outer) {
	const auto inner = outer * 0.45;
	auto path = QPainterPath();
	for (auto i = 0; i != 10; ++i) {
		const auto radius = (i % 2) ? inner : outer;
		const auto angle = (-90. + i * 36.) * M_PI / 180.;
		const auto point = QPointF(
			center.x() + std::cos(angle) * radius,
			center.y() + std::sin(angle) * radius);
		if (!i) {
			path.moveTo(point);
		} else {
			path.lineTo(point);
		}
	}
	path.closeSubpath();
	return path;
}

void PaintInitials(QPainter &p, const QRectF &rect, const QString &name) {
	static const auto kPairs = std::array{
		std::pair{ QColor(0xFF, 0x88, 0x5E), QColor(0xFF, 0x51, 0x6A) },
		std::pair{ QColor(0xFF, 0xCD, 0x6A), QColor(0xFF, 0xA8, 0x5C) },
		std::pair{ QColor(0x82, 0xB1, 0xFF), QColor(0x66, 0x5F, 0xFF) },
		std::pair{ QColor(0xA0, 0xDE, 0x7E), QColor(0x54, 0xCB, 0x68) },
		std::pair{ QColor(0x53, 0xED, 0xD6), QColor(0x28, 0xC9, 0xB7) },
		std::pair{ QColor(0x72, 0xD5, 0xFD), QColor(0x2A, 0x9E, 0xF1) },
		std::pair{ QColor(0xE0, 0xA2, 0xF3), QColor(0xD6, 0x69, 0xED) },
	};
	auto hq = PainterHighQualityEnabler(p);
	const auto &colors = kPairs[qHash(name) % kPairs.size()];
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	gradient.setColorAt(0., colors.first);
	gradient.setColorAt(1., colors.second);
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawEllipse(rect);

	auto letters = QString();
	for (const auto &word : name.split(' ', Qt::SkipEmptyParts)) {
		letters.append(word.at(0).toUpper());
		if (letters.size() == 2) {
			break;
		}
	}
	auto font = st::semiboldFont->f;
	font.setPixelSize(std::max(int(rect.height() * 0.38), 1));
	p.setFont(font);
	p.setPen(QColor(255, 255, 255));
	p.drawText(rect, Qt::AlignCenter, letters);
}

void PaintTypeAvatar(
		QPainter &p,
		const QRect &rect,
		int type,
		const QString &name) {
	if (type == kTypePersonal) {
		PaintInitials(p, QRectF(rect), name);
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	if (type == kTypeChannel) {
		gradient.setColorAt(0., QColor(0x5A, 0xB4, 0xFF));
		gradient.setColorAt(1., QColor(0x23, 0x62, 0xE8));
	} else {
		gradient.setColorAt(0., QColor(0xB0, 0x8C, 0xFF));
		gradient.setColorAt(1., QColor(0x5B, 0x4B, 0xE8));
	}
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawEllipse(rect);
	const auto &icon = (type == kTypeChannel)
		? st::menuIconChannel
		: st::menuIconBot;
	const auto scale = rect.width() / float64(icon.width() * 2);
	p.save();
	p.translate(rect.center() + QPoint(1, 1));
	p.scale(scale, scale);
	icon.paint(
		p,
		-icon.width() / 2,
		-icon.height() / 2,
		icon.width(),
		QColor(255, 255, 255));
	p.restore();
}

struct Identity {
	int type = kTypePersonal;
	QString level;
	QString name;
	QString handle;
};

using AvatarPainter = Fn<void(QPainter&, QRect)>;

[[nodiscard]] int IdentityHeight() {
	return st::flashgramVerifyPreviewAvatar;
}

void PaintNameWithBadge(
		QPainter &p,
		const QRect &rect,
		const style::font &font,
		const QColor &color,
		const QString &name,
		const QString &level,
		bool centered) {
	const auto badgeHeight = st::flashgramVerifyBadgeHeight;
	const auto badgeWidth = VerificationBadgeWidth(badgeHeight);
	const auto skip = st::flashgramVerifyInlineSkip;
	const auto available = std::max(
		rect.width() - (level.isEmpty() ? 0 : (badgeWidth + skip)),
		0);
	const auto text = font->elided(name, available);
	const auto textWidth = font->width(text);
	const auto full = textWidth
		+ (level.isEmpty() ? 0 : (skip + badgeWidth));
	const auto left = centered
		? (rect.x() + (rect.width() - full) / 2)
		: rect.x();
	DrawText(
		p,
		font,
		color,
		QRect(left, rect.y(), textWidth + 1, rect.height()),
		text);
	if (!level.isEmpty()) {
		PaintVerificationBadge(
			p,
			QRectF(
				left + textWidth + skip,
				rect.y() + (rect.height() - badgeHeight) / 2.,
				badgeWidth,
				badgeHeight),
			level);
	}
}

void PaintIdentity(
		QPainter &p,
		const QRect &row,
		const Identity &identity,
		const AvatarPainter &avatar) {
	const auto size = row.height();
	const auto avatarRect = QRect(row.x(), row.y(), size, size);
	if (avatar) {
		avatar(p, avatarRect);
	} else {
		PaintTypeAvatar(p, avatarRect, identity.type, identity.name);
	}
	const auto left = row.x() + size + st::flashgramVerifyPreviewSkip;
	const auto width = row.right() - left;
	const auto &nameFont = st::flashgramVerifyNameFont;
	const auto &textFont = st::flashgramVerifyTextFont;
	const auto lines = nameFont->height + 2 * textFont->height;
	auto top = row.y() + (size - lines) / 2;
	PaintNameWithBadge(
		p,
		QRect(left, top, width, nameFont->height),
		nameFont,
		C().text,
		identity.name,
		identity.level,
		false);
	top += nameFont->height;
	DrawText(
		p,
		textFont,
		C().sub,
		QRect(left, top, width, textFont->height),
		textFont->elided(
			identity.handle.isEmpty() ? TypeName(identity.type) : identity.handle,
			width));
	top += textFont->height;
	DrawText(
		p,
		textFont,
		LevelColors(identity.level).first,
		QRect(left, top, width, textFont->height),
		VerificationLevelName(identity.level));
}

[[nodiscard]] QString RulesText() {
	return Tr(
		"FlashGram Verification is a badge inside FlashGram only.\n\n"
		"• It is not Telegram verification. Telegram's verified mark is "
		"never granted and nothing changes on Telegram servers.\n"
		"• Requests are reviewed manually by the FlashGram team. "
		"Approval, rejection and revocation happen only on the "
		"FlashGram server.\n"
		"• The profile must be real, filled in and must not "
		"impersonate other people, brands or Telegram.\n"
		"• Spam, fraud and scams lead to revocation.\n"
		"• Your profile appears in Top and Catalog only if you turn "
		"on public listing. Private data is never shown.",
		"Верификация FlashGram — это бейдж только внутри FlashGram.\n\n"
		"• Это не верификация Telegram. Галочка Telegram не выдаётся, "
		"на серверах Telegram ничего не меняется.\n"
		"• Заявки вручную рассматривает команда FlashGram. Одобрение, "
		"отказ и отзыв происходят только на сервере FlashGram.\n"
		"• Профиль должен быть настоящим, заполненным и не выдавать "
		"себя за других людей, бренды или Telegram.\n"
		"• Спам, мошенничество и обман ведут к отзыву бейджа.\n"
		"• В Топе и Каталоге профиль виден, только если вы сами "
		"включили публичность. Приватные данные не показываются.");
}

void RulesBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(TrValue("General rules", "Общие правила"));
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<Ui::FlatLabel>(box, RulesText(), st::boxLabel));
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void TargetBox(
		not_null<Ui::GenericBox*> box,
		int type,
		QString current,
		Fn<void(QString)> done) {
	box->setTitle((type == kTypeChannel)
		? TrValue("Channel username", "Username канала")
		: TrValue("Bot username", "Username бота"));
	box->setWidth(st::boxWideWidth);
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(u"@username"_q),
		current.isEmpty() ? QString() : (u"@"_q + current)));
	field->setMaxLength(33);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			Tr(
				"Only the owner of the channel or bot should request "
				"verification. The FlashGram team checks ownership.",
				"Подавать заявку должен владелец канала или бота. "
				"Команда FlashGram проверит владение."),
			st::boxDividerLabel),
		st::flashgramSecondaryPadding);
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto submit = [=] {
		auto value = field->getLastText().trimmed();
		if (value.startsWith('@')) {
			value = value.mid(1);
		}
		static const auto kValid = QRegularExpression(
			u"^[A-Za-z0-9_]{3,32}$"_q);
		if (!kValid.match(value).hasMatch()) {
			field->showError();
			return;
		}
		done(value);
		box->closeBox();
	};
	field->submits() | rpl::on_next([=](Qt::KeyboardModifiers) {
		submit();
	}, field->lifetime());
	box->addButton(TrValue("Save", "Сохранить"), submit);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void SubmitConfirmBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Server::VerificationSubmit request,
		AvatarPainter avatar) {
	const auto userId = peerToUser(controller->session().user()->id).bare;
	const auto type = TypeIndex(request.type);
	box->setTitle(TrValue("Badge preview", "Предпросмотр бейджа"));
	box->setWidth(st::boxWideWidth);

	const auto identity = Identity{
		.type = type,
		.level = request.level,
		.name = request.displayName,
		.handle = Handle((type == kTypePersonal)
			? request.username
			: request.target),
	};
	const auto preview = box->addRow(
		object_ptr<Block>(
			box,
			st::boxBg->c,
			[](int) {
				return st::flashgramVerifyCardMargin.top()
					+ st::flashgramVerifyCardPadding.top()
					+ IdentityHeight()
					+ st::flashgramVerifyCardPadding.bottom()
					+ st::flashgramVerifyCardMargin.bottom();
			},
			[=](QPainter &p, QRect r) {
				const auto card = CardRect(r);
				PaintCard(p, card, C().card);
				PaintIdentity(
					p,
					card.marginsRemoved(st::flashgramVerifyCardPadding),
					identity,
					avatar);
			}),
		QMargins());
	controller->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		preview->update();
	}, preview->lifetime());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			Tr(
				"This is how your FlashGram badge will look inside "
				"FlashGram. It is not Telegram verification and doesn't "
				"change your profile on Telegram servers. The FlashGram "
				"team reviews every request.",
				"Так будет выглядеть ваш бейдж внутри FlashGram. Это не "
				"верификация Telegram, и профиль на серверах Telegram не "
				"изменится. Каждую заявку проверяет команда FlashGram."),
			st::boxDividerLabel),
		st::flashgramSecondaryPadding);
	const auto listing = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			Tr(
				"Show my profile in FlashGram Top and Catalog",
				"Показывать профиль в Топе и Каталоге FlashGram"),
			true,
			st::defaultCheckbox),
		st::flashgramSecondaryPadding);

	const auto sending = box->lifetime().make_state<bool>(false);
	box->addButton(TrValue("Submit request", "Отправить заявку"), [=] {
		if (*sending) {
			return;
		}
		*sending = true;
		auto data = request;
		data.publicListing = listing->checked();
		Server::SubmitVerification(userId, data, crl::guard(box, [=](
				QString error) {
			*sending = false;
			if (!error.isEmpty()) {
				controller->uiShow()->showToast(ServerErrorText(error));
				return;
			}
			controller->uiShow()->showToast(Tr(
				"Request sent. The FlashGram team will review it.",
				"Заявка отправлена. Команда FlashGram её рассмотрит."));
			box->closeBox();
		}));
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

struct ScreenState {
	rpl::variable<int> page = kPageVerification;
	rpl::variable<int> type = kTypePersonal;
	rpl::variable<QString> level = u"verified"_q;
	rpl::variable<QString> category = u"creators"_q;
	std::array<QString, 3> targets;
	rpl::event_stream<> changed;
	Ui::PeerUserpicView userpic;
	base::unique_qptr<Ui::PopupMenu> menu;
};

enum class ActionKind {
	Loading,
	Retry,
	Submit,
	Resubmit,
	Pending,
	Verified,
};

[[nodiscard]] const Server::VerificationRequest *FindRequest(
		const Server::Verification *verification,
		const QString &type) {
	if (!verification) {
		return nullptr;
	}
	for (const auto &request : verification->requests) {
		if (request.type == type) {
			return &request;
		}
	}
	return nullptr;
}

[[nodiscard]] const Server::VerificationRequest *FindApproved(
		const Server::Verification *verification) {
	if (!verification) {
		return nullptr;
	}
	for (const auto &request : verification->requests) {
		if (request.status == u"approved"_q) {
			return &request;
		}
	}
	return nullptr;
}

[[nodiscard]] ActionKind ComputeAction(uint64 userId, const QString &type) {
	const auto verification = Server::CachedVerification(userId);
	if (!verification) {
		return (!Server::VerificationError(userId).isEmpty()
			|| Server::CurrentStatus() == Server::Status::Offline)
			? ActionKind::Retry
			: ActionKind::Loading;
	} else if (const auto request = FindRequest(verification, type)) {
		if (request->status == u"pending"_q) {
			return ActionKind::Pending;
		} else if (request->status == u"approved"_q) {
			return ActionKind::Verified;
		}
		return ActionKind::Resubmit;
	}
	return ActionKind::Submit;
}

[[nodiscard]] QString ActionText(ActionKind kind) {
	switch (kind) {
	case ActionKind::Loading: return Tr("Loading...", "Загрузка...");
	case ActionKind::Retry:
		return Tr("FlashGram Server is offline", "Сервер FlashGram недоступен");
	case ActionKind::Submit:
		return Tr("Apply for verification", "Подать заявку");
	case ActionKind::Resubmit:
		return Tr("Apply again", "Подать заявку повторно");
	case ActionKind::Pending:
		return Tr("Request under review", "Заявка на рассмотрении");
	case ActionKind::Verified:
		return Tr("You are FlashGram Verified", "Вы верифицированы в FlashGram");
	}
	return QString();
}

class Screen final {
public:
	Screen(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller);

private:
	[[nodiscard]] uint64 userId() const;
	[[nodiscard]] QString typeKey() const;
	[[nodiscard]] Identity identity() const;
	[[nodiscard]] AvatarPainter avatarPainter() const;
	void paintUserpic(QPainter &p, const QRect &rect) const;

	void setupHeader();
	void setupNavigation();
	void setupVerificationPage(not_null<Ui::VerticalLayout*> page);
	void setupDirectoryPage(
		not_null<Ui::VerticalLayout*> page,
		bool catalog);
	void setupProfilePage(not_null<Ui::VerticalLayout*> page);
	void addTitle(
		not_null<Ui::VerticalLayout*> page,
		Fn<QString()> title,
		Fn<QString()> subtitle);
	void addNote(not_null<Ui::VerticalLayout*> page, Fn<QString()> text);
	not_null<Block*> addRowCard(
		not_null<Ui::VerticalLayout*> page,
		Fn<void(QPainter&, QRect)> icon,
		Fn<QString()> text,
		Fn<bool()> visible,
		Fn<void()> click);
	void showMenu();
	void submit();
	void refreshOn(not_null<Block*> block);

	const not_null<Ui::GenericBox*> _box;
	const not_null<Window::SessionController*> _controller;
	const not_null<UserData*> _user;
	const not_null<ScreenState*> _state;

};

Screen::Screen(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller)
: _box(box)
, _controller(controller)
, _user(controller->session().user())
, _state(box->lifetime().make_state<ScreenState>()) {
	_box->setStyle(ScreenBoxStyle());
	_box->setNoContentMargin(true);
	_box->setWidth(st::boxWideWidth);
	_box->setMaxHeight(st::flashgramVerifyBoxHeight);
	_box->setMinHeight(st::flashgramVerifyBoxHeight);

	Server::RequestVerification(userId(), true);
	rpl::merge(
		Server::VerificationUpdates(),
		Server::StatusValue() | rpl::to_empty,
		_state->type.changes() | rpl::to_empty,
		_state->level.changes() | rpl::to_empty,
		_state->page.changes() | rpl::to_empty,
		_controller->session().downloaderTaskFinished()
	) | rpl::on_next([=] {
		_state->changed.fire({});
	}, _box->lifetime());

	setupHeader();
	setupNavigation();

	const auto layout = _box->verticalLayout();
	const auto addPage = [&](int index) {
		const auto wrap = layout->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				layout,
				object_ptr<Ui::VerticalLayout>(layout)));
		wrap->toggleOn(_state->page.value() | rpl::map([=](int page) {
			return (page == index);
		}), anim::type::instant);
		return wrap->entity();
	};
	setupVerificationPage(addPage(kPageVerification));
	setupDirectoryPage(addPage(kPageTop), false);
	setupDirectoryPage(addPage(kPageCatalog), true);
	setupProfilePage(addPage(kPageProfile));
}

uint64 Screen::userId() const {
	return peerToUser(_user->id).bare;
}

QString Screen::typeKey() const {
	return TypeKey(_state->type.current());
}

Identity Screen::identity() const {
	const auto type = _state->type.current();
	const auto target = _state->targets[type];
	const auto name = (type == kTypePersonal)
		? _user->name()
		: target.isEmpty()
		? ((type == kTypeChannel)
			? Tr("My channel", "Мой канал")
			: Tr("My bot", "Мой бот"))
		: target;
	return {
		.type = type,
		.level = _state->level.current(),
		.name = name,
		.handle = Handle((type == kTypePersonal) ? _user->username() : target),
	};
}

void Screen::paintUserpic(QPainter &p, const QRect &rect) const {
	_user->paintUserpic(
		p,
		_state->userpic,
		rect.x(),
		rect.y(),
		rect.width(),
		true);
}

AvatarPainter Screen::avatarPainter() const {
	if (_state->type.current() != kTypePersonal) {
		return nullptr;
	}
	const auto user = _user;
	const auto state = _state;
	return [=](QPainter &p, QRect rect) {
		user->paintUserpic(
			p,
			state->userpic,
			rect.x(),
			rect.y(),
			rect.width(),
			true);
	};
}

void Screen::refreshOn(not_null<Block*> block) {
	_state->changed.events() | rpl::on_next([=] {
		block->refresh();
	}, block->lifetime());
}

void Screen::setupHeader() {
	const auto header = _box->setPinnedToTopContent(object_ptr<Block>(
		_box,
		QColor(),
		[](int) { return st::flashgramVerifyHeaderHeight; },
		[=](QPainter &p, QRect r) {
			const auto onHero = (_state->page.current() == kPageVerification);
			p.fillRect(r, onHero ? C().heroTop : C().bg);
			const auto &font = st::flashgramVerifyTitleFont;
			const auto &subFont = st::flashgramVerifyHeaderSubFont;
			const auto left = st::flashgramVerifySide
				+ st::flashgramVerifyIconButton
				+ st::flashgramVerifyTitleSkip;
			const auto lines = font->height + subFont->height;
			const auto top = (r.height() - lines) / 2;
			PaintNameWithBadge(
				p,
				QRect(
					left,
					top,
					r.width() - left - 2 * st::flashgramVerifyIconButton
						- st::flashgramVerifySide,
					font->height),
				font,
				C().text,
				u"FlashGram"_q,
				u"verified"_q,
				false);
			DrawText(
				p,
				subFont,
				QColor(255, 255, 255, 190),
				QRect(left, top + font->height, r.width(), subFont->height),
				u"Verification"_q);
		}));

	const auto iconBg = [](QPainter &p, QRect r, bool over) {
		if (over) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 34));
			p.drawEllipse(r);
		}
	};
	const auto size = float64(st::flashgramVerifyIconSize);
	const auto back = CreateButton(header, [=](QPainter &p, QRect r, bool over) {
		iconBg(p, r, over);
		PaintBackArrow(p, QRectF(r).center(), size, C().text);
	}, [=] {
		if (_state->page.current() != kPageVerification) {
			_state->page = kPageVerification;
		} else {
			_box->closeBox();
		}
	});
	const auto collapse = CreateButton(header, [=](QPainter &p, QRect r, bool over) {
		iconBg(p, r, over);
		PaintChevronDown(p, QRectF(r).center(), size, C().text);
	}, [=] {
		_box->closeBox();
	});
	const auto more = CreateButton(header, [=](QPainter &p, QRect r, bool over) {
		iconBg(p, r, over);
		PaintDots(p, QRectF(r).center(), size, C().text);
	}, [=] {
		showMenu();
	});
	header->setLayoutCallback([=](int width, int height) {
		const auto button = st::flashgramVerifyIconButton;
		const auto top = (height - button) / 2;
		const auto side = st::flashgramVerifySide;
		back->setGeometry(side, top, button, button);
		more->setGeometry(width - side - button, top, button, button);
		collapse->setGeometry(width - side - 2 * button, top, button, button);
	});
	refreshOn(header);
}

void Screen::showMenu() {
	_state->menu = base::make_unique_q<Ui::PopupMenu>(
		_box,
		st::popupMenuWithIcons);
	_state->menu->addAction(Tr("Refresh", "Обновить"), [=] {
		Server::RequestVerification(userId(), true);
	}, &st::menuIconInfo);
	_state->menu->addAction(Tr("General rules", "Общие правила"), [=] {
		_controller->show(Box(RulesBox));
	}, &st::menuIconFaq);
	const auto verification = Server::CachedVerification(userId());
	if (verification && verification->admin) {
		_state->menu->addAction(
			Tr("Verification Requests", "Заявки на верификацию"),
			[=] { _controller->show(Box(VerificationRequestsBox, _controller)); },
			&st::menuIconAdmin);
		_state->menu->addAction(
			Tr("Major / Hold badges", "Бейджи Major / Hold"),
			[=] { _controller->show(Box(OrgVerificationGrantBox, _controller)); },
			&st::menuIconAdmin);
	}
	_state->menu->popup(QCursor::pos());
}

void Screen::setupNavigation() {
	const auto nav = _box->setPinnedToBottomContent(object_ptr<Block>(
		_box,
		C().bg,
		[](int) { return st::flashgramVerifyNavHeight; },
		nullptr));
	const auto labels = std::array{
		Tr("Verification", "Верификация"),
		Tr("Top", "Топ"),
		Tr("Catalog", "Каталог"),
	};
	const auto pill = Ui::CreateChild<Ui::RpWidget>(nav.get());
	pill->show();
	pill->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(pill);
		const auto r = QRectF(pill->rect());
		FillRounded(p, r, r.height() / 2., C().nav);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(QPen(QColor(255, 255, 255, 16), 1.));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(
			r.marginsRemoved({ 0.5, 0.5, 0.5, 0.5 }),
			r.height() / 2.,
			r.height() / 2.);
	}, pill->lifetime());

	auto items = std::vector<not_null<Ui::AbstractButton*>>();
	for (auto i = 0; i != 3; ++i) {
		const auto label = labels[i];
		items.push_back(CreateButton(pill, [=](QPainter &p, QRect r, bool over) {
			const auto active = (_state->page.current() == i);
			const auto inset = st::flashgramVerifyNavInset;
			if (active || over) {
				FillRounded(
					p,
					QRectF(r.marginsRemoved({ inset, inset, inset, inset })),
					(r.height() - 2 * inset) / 2.,
					active ? C().inner : QColor(255, 255, 255, 10));
			}
			const auto color = active ? C().lavender : C().sub;
			const auto &font = st::flashgramVerifyNavFont;
			const auto icon = float64(st::flashgramVerifyNavIcon);
			const auto full = icon + font->height;
			const auto top = (r.height() - full) / 2.;
			const auto center = QPointF(r.width() / 2., top + icon / 2.);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			if (i == kPageVerification) {
				const auto shield = QRectF(
					center.x() - icon * 0.42,
					top,
					icon * 0.84,
					icon);
				p.drawPath(ShieldPath(shield));
				PaintCheckMark(
					p,
					QPointF(center.x(), center.y() + icon * 0.02),
					icon * 0.42,
					active ? C().inner : C().bg,
					icon / 9.);
			} else if (i == kPageTop) {
				p.drawPath(StarPath(center, icon / 2.));
			} else {
				const auto cell = icon * 0.42;
				const auto skip = icon * 0.16;
				for (auto row = 0; row != 2; ++row) {
					for (auto column = 0; column != 2; ++column) {
						p.drawRoundedRect(
							QRectF(
								center.x() - icon / 2. + column * (cell + skip),
								top + row * (cell + skip),
								cell,
								cell),
							cell / 3.,
							cell / 3.);
					}
				}
			}
			DrawText(
				p,
				font,
				color,
				QRect(0, int(top + icon), r.width(), font->height),
				label,
				int(Qt::AlignCenter));
		}, [=] {
			_state->page = i;
		}));
	}

	const auto profile = CreateButton(nav, [=](QPainter &p, QRect r, bool over) {
		const auto active = (_state->page.current() == kPageProfile);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(C().nav);
		p.drawEllipse(r);
		const auto ring = st::flashgramVerifyNavRing;
		if (active || over) {
			p.setPen(QPen(active ? C().lavender : C().sub, ring));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(QRectF(r).marginsRemoved(
				{ ring / 2., ring / 2., ring / 2., ring / 2. }));
		}
		const auto inset = st::flashgramVerifyNavInset;
		paintUserpic(p, r.marginsRemoved({ inset, inset, inset, inset }));
		const auto verification = Server::CachedVerification(userId());
		if (FindApproved(verification)) {
			const auto height = st::flashgramVerifyNavBadgeHeight;
			const auto width = VerificationBadgeWidth(height);
			PaintVerificationBadge(
				p,
				QRectF(
					r.x() + (r.width() - width) / 2.,
					r.bottom() - height + 1,
					width,
					height),
				FindApproved(verification)->level);
		}
	}, [=] {
		_state->page = kPageProfile;
	});

	nav->setLayoutCallback([=](int width, int height) {
		const auto side = st::flashgramVerifySide;
		const auto size = st::flashgramVerifyNavPill;
		const auto top = (height - size) / 2;
		const auto pillWidth = width - 2 * side - size - st::flashgramVerifyNavSkip;
		pill->setGeometry(side, top, pillWidth, size);
		profile->setGeometry(width - side - size, top, size, size);
		const auto item = pillWidth / 3;
		for (auto i = 0; i != 3; ++i) {
			items[i]->setGeometry(
				i * item,
				0,
				(i == 2) ? (pillWidth - 2 * item) : item,
				size);
		}
	});
	_state->changed.events() | rpl::on_next([=] {
		pill->update();
		for (const auto item : items) {
			item->update();
		}
		profile->update();
	}, nav->lifetime());
}

void Screen::addTitle(
		not_null<Ui::VerticalLayout*> page,
		Fn<QString()> title,
		Fn<QString()> subtitle) {
	const auto block = AddBlock(page, [=](int width) {
		const auto padding = st::flashgramVerifyTitlePadding;
		const auto inner = width - padding.left() - padding.right();
		return padding.top()
			+ WrappedHeight(st::flashgramVerifyPanelTitleFont, title(), inner)
			+ st::flashgramVerifyPanelTextSkip
			+ WrappedHeight(st::flashgramVerifyPanelTextFont, subtitle(), inner)
			+ padding.bottom();
	}, [=](QPainter &p, QRect r) {
		const auto padding = st::flashgramVerifyTitlePadding;
		const auto inner = r.width() - padding.left() - padding.right();
		const auto &titleFont = st::flashgramVerifyPanelTitleFont;
		const auto &textFont = st::flashgramVerifyPanelTextFont;
		const auto titleHeight = WrappedHeight(titleFont, title(), inner);
		DrawText(
			p,
			titleFont,
			C().text,
			QRect(padding.left(), padding.top(), inner, titleHeight),
			title(),
			int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
		const auto top = padding.top()
			+ titleHeight
			+ st::flashgramVerifyPanelTextSkip;
		DrawText(
			p,
			textFont,
			C().sub,
			QRect(
				padding.left(),
				top,
				inner,
				WrappedHeight(textFont, subtitle(), inner)),
			subtitle(),
			int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
	});
	refreshOn(block);
}

void Screen::addNote(not_null<Ui::VerticalLayout*> page, Fn<QString()> text) {
	const auto block = AddBlock(page, [=](int width) {
		const auto padding = st::flashgramVerifyNotePadding;
		const auto value = text();
		return value.isEmpty()
			? 0
			: (padding.top()
				+ WrappedHeight(
					st::flashgramVerifyNoteFont,
					value,
					width - padding.left() - padding.right())
				+ padding.bottom());
	}, [=](QPainter &p, QRect r) {
		DrawText(
			p,
			st::flashgramVerifyNoteFont,
			C().sub,
			r.marginsRemoved(st::flashgramVerifyNotePadding),
			text(),
			int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
	});
	refreshOn(block);
}

not_null<Block*> Screen::addRowCard(
		not_null<Ui::VerticalLayout*> page,
		Fn<void(QPainter&, QRect)> icon,
		Fn<QString()> text,
		Fn<bool()> visible,
		Fn<void()> click) {
	const auto block = AddBlock(page, [=](int) {
		return (!visible || visible())
			? (st::flashgramVerifyRowHeight
				+ st::flashgramVerifyCardMargin.top()
				+ st::flashgramVerifyCardMargin.bottom())
			: 0;
	}, nullptr);
	const auto button = CreateButton(block, [=](QPainter &p, QRect r, bool over) {
		PaintCard(p, r, over ? C().cardOver : C().card);
		const auto iconSize = st::flashgramVerifyRowIcon;
		const auto padding = st::flashgramVerifyCardPadding;
		const auto iconRect = QRect(
			padding.left(),
			(r.height() - iconSize) / 2,
			iconSize,
			iconSize);
		icon(p, iconRect);
		const auto left = iconRect.right() + st::flashgramVerifyRowSkip;
		const auto chevron = st::flashgramVerifyChevronSize;
		DrawText(
			p,
			st::flashgramVerifyRowFont,
			C().text,
			QRect(
				left,
				0,
				r.width() - left - padding.right() - chevron,
				r.height()),
			st::flashgramVerifyRowFont->elided(
				text(),
				r.width() - left - padding.right() - 2 * chevron));
		PaintChevronRight(
			p,
			QPointF(r.width() - padding.right(), r.height() / 2.),
			chevron * 1.6,
			C().sub);
	}, std::move(click));
	block->setLayoutCallback([=](int width, int height) {
		button->setVisible(height > 0);
		button->setGeometry(CardRect(QRect(0, 0, width, height)));
	});
	_state->changed.events() | rpl::on_next([=] {
		button->update();
	}, button->lifetime());
	refreshOn(block);
	return block;
}

void Screen::setupVerificationPage(not_null<Ui::VerticalLayout*> page) {
	const auto state = _state;

	const auto hero = AddBlock(page, [](int) {
		return st::flashgramVerifyTabsTop
			+ st::flashgramVerifyTabsHeight
			+ st::flashgramVerifyHeroHeight;
	}, [=](QPainter &p, QRect r) {
		auto hq = PainterHighQualityEnabler(p);
		const auto w = r.width();
		const auto h = r.height();
		auto background = QLinearGradient(0, 0, 0, h);
		background.setColorAt(0., C().heroTop);
		background.setColorAt(1., C().heroBottom);
		p.fillRect(r, background);

		const auto phoneWidth = float64(st::flashgramVerifyPhoneWidth);
		const auto u = phoneWidth / 236.;
		const auto phoneTop = float64(st::flashgramVerifyTabsTop
			+ st::flashgramVerifyTabsHeight
			+ st::flashgramVerifyPhoneTop);
		auto glow = QRadialGradient(
			QPointF(w / 2., phoneTop + 110 * u),
			phoneWidth);
		glow.setColorAt(0., QColor(0x9F, 0xD0, 0xFF, 120));
		glow.setColorAt(1., QColor(0x9F, 0xD0, 0xFF, 0));
		p.fillRect(r, glow);

		const auto radius = float64(st::flashgramVerifyPhoneRadius);
		const auto phone = QRectF(
			(w - phoneWidth) / 2.,
			phoneTop,
			phoneWidth,
			h - phoneTop + radius * 2);
		p.setClipRect(r);
		auto bezel = QLinearGradient(phone.topLeft(), phone.topRight());
		bezel.setColorAt(0., QColor(0xC4, 0xC8, 0xD0));
		bezel.setColorAt(0.5, QColor(0xF5, 0xF6, 0xF8));
		bezel.setColorAt(1., QColor(0xA4, 0xA9, 0xB3));
		FillRounded(p, phone, radius, bezel);
		const auto bz = float64(st::flashgramVerifyPhoneBezel);
		const auto screen = phone.marginsRemoved({ bz, bz, bz, bz });
		auto screenFill = QLinearGradient(screen.topLeft(), screen.bottomLeft());
		screenFill.setColorAt(0., QColor(0x31, 0x33, 0x3A));
		screenFill.setColorAt(0.55, QColor(0x17, 0x18, 0x1D));
		FillRounded(p, screen, radius - bz, screenFill);

		auto screenPath = QPainterPath();
		screenPath.addRoundedRect(screen, radius - bz, radius - bz);
		p.setClipPath(screenPath, Qt::IntersectClip);
		for (auto i = 0; i != 14; ++i) {
			const auto x = screen.x() + ((i * 53 + 17) % 100) / 100. * screen.width();
			const auto y = screen.y() + ((i * 37 + 11) % 60) / 100. * h;
			PaintSparkle(p, QPointF(x, y), 5 * u, QColor(255, 255, 255, 16));
		}

		const auto &statusFont = st::flashgramVerifyPhoneStatusFont;
		DrawText(
			p,
			statusFont,
			C().text,
			QRect(
				int(screen.x() + 24 * u),
				int(screen.y() + 8 * u),
				int(50 * u),
				int(22 * u)),
			u"9:41"_q);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0));
		p.drawRoundedRect(
			QRectF(screen.center().x() - 38 * u, screen.y() + 8 * u, 76 * u, 22 * u),
			11 * u,
			11 * u);
		p.setBrush(C().text);
		for (auto i = 0; i != 4; ++i) {
			const auto barHeight = (4 + i * 2.2) * u;
			p.drawRoundedRect(
				QRectF(
					screen.right() - 54 * u + i * 4.5 * u,
					screen.y() + 23 * u - barHeight,
					3 * u,
					barHeight),
				u,
				u);
		}
		p.setPen(QPen(C().text, u));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(
			QRectF(screen.right() - 32 * u, screen.y() + 12 * u, 20 * u, 10 * u),
			3 * u,
			3 * u);
		p.setPen(Qt::NoPen);
		p.setBrush(C().text);
		p.drawRoundedRect(
			QRectF(screen.right() - 30 * u, screen.y() + 14 * u, 14 * u, 6 * u),
			2 * u,
			2 * u);

		const auto backCenter = QPointF(screen.x() + 30 * u, screen.y() + 58 * u);
		p.setPen(QPen(QColor(255, 255, 255, 110), u));
		p.setBrush(QColor(255, 255, 255, 14));
		p.drawEllipse(backCenter, 14 * u, 14 * u);
		auto hq2 = PainterHighQualityEnabler(p);
		p.setPen(QPen(C().text, 1.6 * u, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(
			QPointF(backCenter.x() + 2.5 * u, backCenter.y() - 5 * u),
			QPointF(backCenter.x() - 2.5 * u, backCenter.y()));
		p.drawLine(
			QPointF(backCenter.x() - 2.5 * u, backCenter.y()),
			QPointF(backCenter.x() + 2.5 * u, backCenter.y() + 5 * u));
		const auto editRect = QRectF(
			screen.right() - 58 * u,
			screen.y() + 44 * u,
			42 * u,
			28 * u);
		p.setPen(QPen(QColor(255, 255, 255, 110), u));
		p.setBrush(QColor(255, 255, 255, 14));
		p.drawRoundedRect(editRect, 14 * u, 14 * u);
		DrawText(
			p,
			st::flashgramVerifyPhoneSubFont,
			C().text,
			editRect.toRect(),
			Tr("Edit", "Изм."),
			int(Qt::AlignCenter));

		const auto current = identity();
		const auto avatarSize = st::flashgramVerifyPhoneAvatar;
		const auto avatarRect = QRect(
			int(screen.center().x() - avatarSize / 2.),
			int(screen.y() + 44 * u),
			avatarSize,
			avatarSize);
		if (current.type == kTypePersonal) {
			paintUserpic(p, avatarRect);
		} else {
			PaintTypeAvatar(p, avatarRect, current.type, current.name);
		}

		const auto &nameFont = st::flashgramVerifyPhoneNameFont;
		const auto nameTop = avatarRect.bottom() + int(10 * u);
		PaintNameWithBadge(
			p,
			QRect(
				int(screen.x() + 14 * u),
				nameTop,
				int(screen.width() - 28 * u),
				nameFont->height),
			nameFont,
			C().text,
			current.name,
			current.level,
			true);
		const auto &subFont = st::flashgramVerifyPhoneSubFont;
		const auto subTop = nameTop + nameFont->height;
		const auto subText = (current.type == kTypeChannel)
			? Tr("channel · Verified by FlashGram", "канал · Verified by FlashGram")
			: (current.type == kTypeBot)
			? Tr("bot · Verified by FlashGram", "бот · Verified by FlashGram")
			: (current.handle.isEmpty()
				? Tr("last seen recently", "был(а) недавно")
				: current.handle);
		DrawText(
			p,
			subFont,
			QColor(255, 255, 255, 170),
			QRect(int(screen.x()), subTop, int(screen.width()), subFont->height),
			subFont->elided(subText, int(screen.width() - 20 * u)),
			int(Qt::AlignCenter));

		const auto tileTop = subTop + subFont->height + 14 * u;
		const auto tileSkip = 6 * u;
		const auto tileSide = 12 * u;
		const auto tileWidth = (screen.width() - 2 * tileSide - 3 * tileSkip) / 4.;
		const auto tileHeight = float64(st::flashgramVerifyPhoneTileHeight);
		const auto tiles = std::array<std::pair<const style::icon*, QString>, 4>{
			std::pair{ (current.type == kTypePersonal)
				? &st::menuIconPhone
				: &st::menuIconChannel,
				(current.type == kTypePersonal)
					? Tr("call", "звонок")
					: Tr("open", "открыть") },
			std::pair{ &st::menuIconMute, Tr("mute", "звук") },
			std::pair{ &st::menuIconSearch, Tr("search", "поиск") },
			std::pair{ &st::menuIconAdmin, u"FG"_q },
		};
		const auto &tileFont = st::flashgramVerifyPhoneTileFont;
		for (auto i = 0; i != 4; ++i) {
			const auto tile = QRectF(
				screen.x() + tileSide + i * (tileWidth + tileSkip),
				tileTop,
				tileWidth,
				tileHeight);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 22));
			p.drawRoundedRect(tile, 11 * u, 11 * u);
			const auto &icon = *tiles[i].first;
			icon.paint(
				p,
				int(tile.center().x() - icon.width() / 2.),
				int(tile.y() + 3 * u),
				int(w),
				C().text);
			DrawText(
				p,
				tileFont,
				QColor(255, 255, 255, 210),
				QRect(
					int(tile.x()),
					int(tile.bottom() - tileFont->height - 4 * u),
					int(tile.width()),
					tileFont->height),
				tiles[i].second,
				int(Qt::AlignCenter));
		}

		const auto infoTop = tileTop + tileHeight + 14 * u;
		const auto info = QRectF(
			screen.x(),
			infoTop,
			screen.width(),
			h - infoTop + radius);
		auto infoFill = QLinearGradient(info.topLeft(), info.bottomLeft());
		infoFill.setColorAt(0., QColor(0xEE, 0xF2, 0xFC));
		infoFill.setColorAt(1., QColor(0xD6, 0xE1, 0xF8));
		FillRounded(p, info, 14 * u, infoFill);
		const auto &infoFont = st::flashgramVerifyPhoneTileFont;
		const auto infoRow = QRect(
			int(info.x() + 16 * u),
			int(info.y() + 10 * u),
			int(info.width() - 32 * u),
			infoFont->height);
		DrawText(
			p,
			infoFont,
			QColor(0x5E, 0x67, 0x7A),
			infoRow,
			TypeName(current.type).toUpper());
		DrawText(
			p,
			infoFont,
			QColor(0x5E, 0x67, 0x7A),
			infoRow,
			VerificationLevelName(current.level),
			int(Qt::AlignRight | Qt::AlignVCenter));
		p.setClipping(false);
	});
	refreshOn(hero);

	const auto tabs = Ui::CreateChild<Ui::RpWidget>(hero.get());
	tabs->show();
	const auto tabLabels = std::array{
		Tr("Channel", "Канал"),
		Tr("Personal", "Личный"),
		Tr("Bot", "Бот"),
	};
	const auto tabsSlide = tabs->lifetime().make_state<Ui::Animations::Simple>();
	state->type.changes() | rpl::on_next([=](int type) {
		const auto from = tabsSlide->value(float64(type));
		tabsSlide->stop();
		tabsSlide->start(
			[=] { tabs->update(); },
			from,
			float64(type),
			kTabsDuration,
			anim::easeOutCubic);
	}, tabs->lifetime());
	tabs->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(tabs);
		const auto r = QRectF(tabs->rect());
		FillRounded(p, r, r.height() / 2., QColor(255, 255, 255, 46));
		const auto inset = st::flashgramVerifyTabsInset;
		const auto third = r.width() / 3.;
		const auto active = state->type.current();
		const auto position = tabsSlide->value(float64(active));
		const auto pill = QRectF(
			position * third + inset,
			inset,
			third - 2 * inset,
			r.height() - 2 * inset);
		FillRounded(p, pill, pill.height() / 2., QColor(255, 255, 255));
		for (auto i = 0; i != 3; ++i) {
			DrawText(
				p,
				st::flashgramVerifyTabsFont,
				(i == active) ? QColor(0x10, 0x12, 0x18) : C().text,
				QRect(int(i * third), 0, int(third), int(r.height())),
				tabLabels[i],
				int(Qt::AlignCenter));
		}
	}, tabs->lifetime());
	for (auto i = 0; i != 3; ++i) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(tabs);
		button->setClickedCallback([=] {
			state->type = i;
		});
		button->show();
		tabs->widthValue() | rpl::on_next([=](int width) {
			const auto third = width / 3;
			button->setGeometry(
				i * third,
				0,
				(i == 2) ? (width - 2 * third) : third,
				st::flashgramVerifyTabsHeight);
		}, button->lifetime());
	}
	hero->setLayoutCallback([=](int width, int) {
		const auto tabsWidth = std::min(
			int(st::flashgramVerifyTabsWidth),
			width - 2 * st::flashgramVerifySide);
		tabs->setGeometry(
			(width - tabsWidth) / 2,
			st::flashgramVerifyTabsTop,
			tabsWidth,
			st::flashgramVerifyTabsHeight);
	});
	state->changed.events() | rpl::on_next([=] {
		tabs->update();
	}, tabs->lifetime());

	addTitle(page, [=] {
		switch (state->type.current()) {
		case kTypeChannel:
			return Tr("Channel verification", "Верификация канала");
		case kTypeBot: return Tr("Bot verification", "Верификация бота");
		}
		return Tr("Personal profile verification", "Верификация личного профиля");
	}, [=] {
		switch (state->type.current()) {
		case kTypeChannel:
			return Tr(
				"Confirm your channel inside FlashGram and get a "
				"FlashGram badge and a place in the Catalog.",
				"Подтвердите свой канал внутри FlashGram и получите "
				"бейдж FlashGram и место в Каталоге.");
		case kTypeBot:
			return Tr(
				"Confirm your bot inside FlashGram and get a FlashGram "
				"badge for developers and a place in the Catalog.",
				"Подтвердите своего бота внутри FlashGram и получите "
				"бейдж FlashGram для разработчиков и место в Каталоге.");
		}
		return Tr(
			"Confirm your profile inside FlashGram and get an extended "
			"profile, a FlashGram badge and extra features.",
			"Подтвердите свой профиль внутри FlashGram и получите "
			"расширенный профиль, бейдж FlashGram и дополнительные "
			"функции.");
	});

	const auto levels = std::array{
		u"verified"_q,
		u"creator"_q,
		u"business"_q,
		u"developer"_q,
	};
	const auto previewNote = Tr(
		"Partner and Support badges are granted only by the FlashGram team.",
		"Бейджи Partner и Support выдаёт только команда FlashGram.");
	const auto previewInner = [=](int width) {
		const auto card = CardRect(QRect(0, 0, width, 0));
		const auto padding = st::flashgramVerifyCardPadding;
		return card.width() - padding.left() - padding.right();
	};
	const auto preview = AddBlock(page, [=](int width) {
		const auto margin = st::flashgramVerifyCardMargin;
		const auto padding = st::flashgramVerifyCardPadding;
		return margin.top()
			+ padding.top()
			+ st::flashgramVerifyCaptionFont->height
			+ st::flashgramVerifyCaptionSkip
			+ IdentityHeight()
			+ st::flashgramVerifyPreviewSkip
			+ st::flashgramVerifyChipHeight
			+ st::flashgramVerifyCaptionSkip
			+ WrappedHeight(
				st::flashgramVerifyNoteFont,
				previewNote,
				previewInner(width))
			+ padding.bottom()
			+ margin.bottom();
	}, [=](QPainter &p, QRect r) {
		const auto card = CardRect(r);
		PaintCard(p, card, C().card);
		const auto inner = card.marginsRemoved(st::flashgramVerifyCardPadding);
		const auto &caption = st::flashgramVerifyCaptionFont;
		DrawText(
			p,
			caption,
			C().sub,
			QRect(inner.x(), inner.y(), inner.width(), caption->height),
			Tr("Badge preview", "Предпросмотр бейджа"));
		const auto rowTop = inner.y() + caption->height + st::flashgramVerifyCaptionSkip;
		PaintIdentity(
			p,
			QRect(inner.x(), rowTop, inner.width(), IdentityHeight()),
			identity(),
			avatarPainter());
		const auto noteTop = rowTop
			+ IdentityHeight()
			+ st::flashgramVerifyPreviewSkip
			+ st::flashgramVerifyChipHeight
			+ st::flashgramVerifyCaptionSkip;
		DrawText(
			p,
			st::flashgramVerifyNoteFont,
			C().sub,
			QRect(inner.x(), noteTop, inner.width(), r.height() - noteTop),
			previewNote,
			int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
	});
	refreshOn(preview);
	auto chips = std::vector<not_null<Ui::AbstractButton*>>();
	for (const auto &level : levels) {
		chips.push_back(CreateButton(preview, [=](QPainter &p, QRect r, bool over) {
			const auto active = (state->level.current() == level);
			if (active) {
				const auto colors = LevelColors(level);
				auto gradient = QLinearGradient(r.topLeft(), r.bottomRight());
				gradient.setColorAt(0., colors.first);
				gradient.setColorAt(1., colors.second);
				FillRounded(p, QRectF(r), r.height() / 2., gradient);
			} else {
				FillRounded(
					p,
					QRectF(r),
					r.height() / 2.,
					over ? C().buttonOver : C().inner);
			}
			DrawText(
				p,
				st::flashgramVerifyChipFont,
				active ? C().text : C().sub,
				r,
				LevelShort(level),
				int(Qt::AlignCenter));
		}, [=] {
			state->level = level;
		}));
	}
	preview->setLayoutCallback([=](int width, int) {
		const auto inner = CardRect(QRect(0, 0, width, 0)).marginsRemoved(
			st::flashgramVerifyCardPadding);
		const auto top = st::flashgramVerifyCardMargin.top()
			+ st::flashgramVerifyCardPadding.top()
			+ st::flashgramVerifyCaptionFont->height
			+ st::flashgramVerifyCaptionSkip
			+ IdentityHeight()
			+ st::flashgramVerifyPreviewSkip;
		const auto skip = st::flashgramVerifyChipSkip;
		const auto count = int(chips.size());
		const auto chipWidth = (inner.width() - (count - 1) * skip) / count;
		for (auto i = 0; i != count; ++i) {
			chips[i]->setGeometry(
				inner.x() + i * (chipWidth + skip),
				top,
				chipWidth,
				st::flashgramVerifyChipHeight);
		}
	});
	state->changed.events() | rpl::on_next([=] {
		for (const auto chip : chips) {
			chip->update();
		}
	}, preview->lifetime());

	addRowCard(page, [=](QPainter &p, QRect r) {
		PaintTypeAvatar(p, r, state->type.current(), QString());
	}, [=] {
		const auto type = state->type.current();
		const auto target = state->targets[type];
		return target.isEmpty()
			? ((type == kTypeChannel)
				? Tr("Set the channel @username", "Укажите @username канала")
				: Tr("Set the bot @username", "Укажите @username бота"))
			: (TypeName(type) + u": @"_q + target);
	}, [=] {
		return state->type.current() != kTypePersonal;
	}, [=] {
		const auto type = state->type.current();
		_controller->show(Box(TargetBox, type, state->targets[type], [=](
				QString value) {
			state->targets[type] = value;
			state->changed.fire({});
		}));
	});

	const auto plan = AddBlock(page, [](int) {
		const auto margin = st::flashgramVerifyCardMargin;
		const auto padding = st::flashgramVerifyCardPadding;
		return margin.top()
			+ padding.top()
			+ st::flashgramVerifyCaptionFont->height
			+ st::flashgramVerifyCaptionSkip
			+ st::flashgramVerifyPlanFont->height
			+ st::flashgramVerifyPlanSubFont->height
			+ st::flashgramVerifyPlanSkip
			+ st::flashgramVerifyButtonHeight
			+ padding.bottom()
			+ margin.bottom();
	}, [=](QPainter &p, QRect r) {
		const auto card = CardRect(r);
		PaintCard(p, card, C().card);
		const auto inner = card.marginsRemoved(st::flashgramVerifyCardPadding);
		const auto &caption = st::flashgramVerifyCaptionFont;
		const auto badgeHeight = st::flashgramVerifyBadgeHeight;
		const auto badgeWidth = VerificationBadgeWidth(badgeHeight);
		PaintVerificationBadge(
			p,
			QRectF(
				inner.x(),
				inner.y() + (caption->height - badgeHeight) / 2.,
				badgeWidth,
				badgeHeight),
			state->level.current());
		DrawText(
			p,
			caption,
			C().lavender,
			QRect(
				inner.x() + badgeWidth + st::flashgramVerifyInlineSkip,
				inner.y(),
				inner.width(),
				caption->height),
			Tr("Special plan", "Специальный план"));
		const auto top = inner.y() + caption->height + st::flashgramVerifyCaptionSkip;
		const auto &big = st::flashgramVerifyPlanFont;
		const auto &sub = st::flashgramVerifyPlanSubFont;
		const auto skip = st::flashgramVerifyInlineSkip;
		const auto half = (inner.width() - skip) / 2;
		const auto right = inner.right() + 1 - half;
		DrawText(
			p,
			big,
			C().text,
			QRect(inner.x(), top, half, big->height),
			big->elided(Tr("1 year", "1 год"), half));
		DrawText(
			p,
			big,
			C().text,
			QRect(right, top, half, big->height),
			big->elided(u"Test Access"_q, half),
			int(Qt::AlignRight | Qt::AlignVCenter));
		DrawText(
			p,
			sub,
			C().sub,
			QRect(inner.x(), top + big->height, half, sub->height),
			sub->elided(VerificationLevelName(state->level.current()), half));
		DrawText(
			p,
			sub,
			C().sub,
			QRect(right, top + big->height, half, sub->height),
			sub->elided(Tr("free while testing", "бесплатно в тесте"), half),
			int(Qt::AlignRight | Qt::AlignVCenter));
	});
	refreshOn(plan);
	const auto action = CreateButton(plan, [=](QPainter &p, QRect r, bool over) {
		const auto kind = ComputeAction(userId(), typeKey());
		const auto enabled = (kind == ActionKind::Submit)
			|| (kind == ActionKind::Resubmit)
			|| (kind == ActionKind::Retry)
			|| (kind == ActionKind::Verified);
		FillRounded(
			p,
			QRectF(r),
			r.height() / 2.,
			(over && enabled) ? C().buttonOver : C().button);
		const auto color = (kind == ActionKind::Verified)
			? C().green
			: enabled
			? C().lavender
			: QColor(0xC9, 0xB6, 0xFF, 120);
		DrawText(
			p,
			st::flashgramVerifyButtonFont,
			color,
			r,
			ActionText(kind),
			int(Qt::AlignCenter));
	}, [=] {
		switch (ComputeAction(userId(), typeKey())) {
		case ActionKind::Retry:
			Server::RefreshHealth();
			Server::RequestVerification(userId(), true);
			break;
		case ActionKind::Submit:
		case ActionKind::Resubmit:
			submit();
			break;
		case ActionKind::Verified:
			state->page = kPageProfile;
			break;
		case ActionKind::Loading:
		case ActionKind::Pending:
			break;
		}
	});
	plan->setLayoutCallback([=](int width, int height) {
		const auto inner = CardRect(QRect(0, 0, width, height)).marginsRemoved(
			st::flashgramVerifyCardPadding);
		action->setGeometry(
			inner.x(),
			inner.bottom() + 1 - st::flashgramVerifyButtonHeight,
			inner.width(),
			st::flashgramVerifyButtonHeight);
	});
	state->changed.events() | rpl::on_next([=] {
		action->update();
	}, action->lifetime());

	addNote(page, [=] {
		auto result = Tr(
			"FlashGram Verification is a FlashGram-only badge. It is not "
			"official Telegram verification: Telegram's verified mark is "
			"not granted and your Telegram profile doesn't change.",
			"Верификация FlashGram — это бейдж только внутри FlashGram. "
			"Это не официальная верификация Telegram: галочка Telegram не "
			"выдаётся, профиль в Telegram не меняется.");
		const auto request = FindRequest(
			Server::CachedVerification(userId()),
			typeKey());
		if (request
			&& (request->status == u"rejected"_q
				|| request->status == u"revoked"_q)) {
			result = StatusName(request->status)
				+ (request->reason.isEmpty()
					? QString()
					: (u": "_q + request->reason))
				+ u"\n\n"_q
				+ result;
		}
		return result;
	});

	struct Requirement {
		QString text;
		std::optional<bool> ok;
	};
	const auto requirements = [=] {
		const auto verification = Server::CachedVerification(userId());
		const auto type = state->type.current();
		const auto filled = !_user->name().trimmed().isEmpty()
			&& ((type == kTypePersonal)
				? !_user->username().isEmpty()
				: !state->targets[type].isEmpty());
		const auto known = [&](bool value) {
			return verification ? std::make_optional(value) : std::nullopt;
		};
		return std::vector<Requirement>{
			{
				(type == kTypePersonal)
					? Tr(
						"Profile filled in: name and username",
						"Профиль заполнен: имя и username")
					: Tr(
						"@username of the channel or bot is set",
						"Указан @username канала или бота"),
				filled,
			},
			(verification && !verification->emailRequired)
				? Requirement{
					Tr("FlashGram ID created", "FlashGram ID создан"),
					known(true),
				}
				: Requirement{
					Tr(
						"FlashGram ID confirmed with email",
						"FlashGram ID подтверждён почтой"),
					known(verification && verification->emailLinked),
				},
			{
				Tr(
					"Account linked to FlashGram Server",
					"Аккаунт связан с сервером FlashGram"),
				known(verification != nullptr),
			},
			{
				Tr(
					"No active FlashGram violations",
					"Нет активных нарушений FlashGram"),
				known(verification && !verification->violations),
			},
			{
				Tr(
					"Profile follows the rules",
					"Профиль соответствует правилам"),
				std::nullopt,
			},
		};
	};
	const auto requirementsBlock = AddBlock(page, [=](int) {
		const auto margin = st::flashgramVerifyCardMargin;
		const auto padding = st::flashgramVerifyCardPadding;
		return margin.top()
			+ padding.top()
			+ st::flashgramVerifySectionFont->height
			+ st::flashgramVerifyCaptionSkip / 2
			+ int(requirements().size()) * st::flashgramVerifyRequirementHeight
			+ padding.bottom()
			+ margin.bottom();
	}, nullptr);
	const auto requirementsButton = CreateButton(requirementsBlock, [=](
			QPainter &p,
			QRect r,
			bool over) {
		PaintCard(p, r, C().card);
		const auto inner = r.marginsRemoved(st::flashgramVerifyCardPadding);
		const auto &section = st::flashgramVerifySectionFont;
		DrawText(
			p,
			section,
			C().text,
			QRect(inner.x(), inner.y(), inner.width(), section->height),
			Tr("Requirements", "Требования"));
		auto top = inner.y() + section->height + st::flashgramVerifyCaptionSkip / 2;
		const auto icon = st::flashgramVerifyRequirementIcon;
		const auto rowHeight = st::flashgramVerifyRequirementHeight;
		for (const auto &requirement : requirements()) {
			const auto circle = QRectF(
				inner.x(),
				top + (rowHeight - icon) / 2.,
				icon,
				icon);
			auto hq = PainterHighQualityEnabler(p);
			if (requirement.ok.value_or(false)) {
				p.setPen(Qt::NoPen);
				p.setBrush(C().green);
				p.drawEllipse(circle);
				PaintCheckMark(p, circle.center(), icon * 0.5, C().card, icon / 9.);
			} else if (requirement.ok.has_value()) {
				p.setPen(QPen(C().red, icon / 10.));
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(circle.marginsRemoved({ 1., 1., 1., 1. }));
			} else {
				p.setPen(QPen(C().accent, icon / 10.));
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(circle.marginsRemoved({ 1., 1., 1., 1. }));
				p.setPen(Qt::NoPen);
				p.setBrush(C().accent);
				p.drawEllipse(circle.center(), icon / 8., icon / 8.);
			}
			const auto left = inner.x() + icon + st::flashgramVerifyRowSkip;
			DrawText(
				p,
				st::flashgramVerifyTextFont,
				requirement.ok.value_or(true) ? C().text : C().sub,
				QRect(left, top, inner.right() - left, rowHeight),
				st::flashgramVerifyTextFont->elided(
					requirement.text,
					inner.right() - left));
			top += rowHeight;
		}
	}, [=] {
		const auto verification = Server::CachedVerification(userId());
		if (verification
			&& verification->emailRequired
			&& !verification->emailLinked) {
			_controller->show(Box(
				RegistrationBox,
				_controller,
				Fn<void()>(),
				Fn<void()>([=] { Server::RequestVerification(userId(), true); })));
		} else {
			_controller->show(Box(RulesBox));
		}
	});
	requirementsBlock->setLayoutCallback([=](int width, int height) {
		requirementsButton->setGeometry(CardRect(QRect(0, 0, width, height)));
	});
	state->changed.events() | rpl::on_next([=] {
		requirementsButton->update();
	}, requirementsButton->lifetime());
	refreshOn(requirementsBlock);

	addRowCard(page, [](QPainter &p, QRect r) {
		FillRounded(p, QRectF(r), r.height() * 0.28, QColor(0x2A, 0xA8, 0xF2));
		const auto &icon = st::menuIconFaq;
		icon.paint(
			p,
			r.x() + (r.width() - icon.width()) / 2,
			r.y() + (r.height() - icon.height()) / 2,
			r.width(),
			QColor(255, 255, 255));
	}, [] {
		return Tr("General rules", "Общие правила");
	}, nullptr, [=] {
		_controller->show(Box(RulesBox));
	});
	AddBlock(page, [](int) { return st::flashgramVerifyNotePadding.bottom(); }, nullptr);
}

void Screen::submit() {
	const auto verification = Server::CachedVerification(userId());
	if (!verification) {
		return;
	}
	const auto type = _state->type.current();
	const auto show = _controller->uiShow();
	if (_user->name().trimmed().isEmpty()
		|| (type == kTypePersonal && _user->username().isEmpty())) {
		show->showToast(Tr(
			"Set your name and username in Telegram settings first.",
			"Сначала укажите имя и username в настройках Telegram."));
		return;
	} else if (type != kTypePersonal && _state->targets[type].isEmpty()) {
		const auto state = _state;
		_controller->show(Box(TargetBox, type, QString(), [=](QString value) {
			state->targets[type] = value;
			state->changed.fire({});
		}));
		return;
	} else if (verification->emailRequired && !verification->emailLinked) {
		show->showToast(ServerErrorText(u"email_required"_q));
		_controller->show(Box(
			RegistrationBox,
			_controller,
			Fn<void()>(),
			Fn<void()>([=] { Server::RequestVerification(userId(), true); })));
		return;
	} else if (verification->violations) {
		show->showToast(ServerErrorText(u"has_violations"_q));
		return;
	}
	const auto current = identity();
	_controller->show(Box(
		SubmitConfirmBox,
		_controller,
		Server::VerificationSubmit{
			.type = TypeKey(type),
			.level = current.level,
			.displayName = (type == kTypePersonal)
				? _user->name()
				: _state->targets[type],
			.username = _user->username(),
			.target = (type == kTypePersonal)
				? QString()
				: _state->targets[type],
			.publicListing = true,
		},
		avatarPainter()));
}

void Screen::setupDirectoryPage(
		not_null<Ui::VerticalLayout*> page,
		bool catalog) {
	const auto state = _state;
	addTitle(page, [=] {
		return catalog ? Tr("Catalog", "Каталог") : Tr("Top", "Топ");
	}, [=] {
		return catalog
			? Tr(
				"FlashGram profiles that turned on public listing.",
				"Профили FlashGram, которые сами включили публичность.")
			: Tr(
				"Public FlashGram Verified profiles. Only people who "
				"allowed it are shown.",
				"Публичные профили FlashGram Verified. Показываются только "
				"те, кто сам это разрешил.");
	});

	const auto categories = std::array{
		std::pair{ u"creators"_q, Tr("Creators", "Авторы") },
		std::pair{ u"businesses"_q, Tr("Businesses", "Бизнес") },
		std::pair{ u"bots"_q, Tr("Bots", "Боты") },
		std::pair{ u"channels"_q, Tr("Channels", "Каналы") },
		std::pair{ u"developers"_q, Tr("Developers", "Разработчики") },
	};
	if (catalog) {
		const auto chipsBlock = AddBlock(page, [=](int width) {
			const auto margin = st::flashgramVerifyCardMargin;
			const auto &font = st::flashgramVerifyChipFont;
			const auto available = width - margin.left() - margin.right();
			auto rows = 1;
			auto left = 0;
			for (const auto &[key, label] : categories) {
				const auto chip = font->width(label)
					+ 2 * st::flashgramVerifyChipPadding;
				if (left > 0 && left + chip > available) {
					++rows;
					left = 0;
				}
				left += chip + st::flashgramVerifyChipSkip;
			}
			return margin.top()
				+ rows * st::flashgramVerifyChipHeight
				+ (rows - 1) * st::flashgramVerifyChipSkip
				+ margin.bottom();
		}, nullptr);
		auto chips = std::vector<not_null<Ui::AbstractButton*>>();
		for (const auto &category : categories) {
			const auto key = category.first;
			const auto label = category.second;
			chips.push_back(CreateButton(chipsBlock, [=](
					QPainter &p,
					QRect r,
					bool over) {
				const auto active = (state->category.current() == key);
				FillRounded(
					p,
					QRectF(r),
					r.height() / 2.,
					active ? C().accent : over ? C().buttonOver : C().card);
				DrawText(
					p,
					st::flashgramVerifyChipFont,
					active ? C().text : C().sub,
					r,
					label,
					int(Qt::AlignCenter));
			}, [=] {
				state->category = key;
			}));
		}
		chipsBlock->setLayoutCallback([=](int width, int) {
			const auto margin = st::flashgramVerifyCardMargin;
			const auto &font = st::flashgramVerifyChipFont;
			const auto available = width - margin.left() - margin.right();
			auto left = 0;
			auto top = margin.top();
			for (auto i = 0; i != int(chips.size()); ++i) {
				const auto chip = font->width(categories[i].second)
					+ 2 * st::flashgramVerifyChipPadding;
				if (left > 0 && left + chip > available) {
					left = 0;
					top += st::flashgramVerifyChipHeight
						+ st::flashgramVerifyChipSkip;
				}
				chips[i]->setGeometry(
					margin.left() + left,
					top,
					chip,
					st::flashgramVerifyChipHeight);
				left += chip + st::flashgramVerifyChipSkip;
			}
		});
		state->category.changes() | rpl::on_next([=] {
			for (const auto chip : chips) {
				chip->update();
			}
		}, chipsBlock->lifetime());
	}

	const auto list = page->add(object_ptr<Ui::VerticalLayout>(page));
	const auto message = list->lifetime().make_state<QString>();
	const auto showMessage = [=](const QString &text) {
		list->clear();
		*message = text;
		const auto block = AddBlock(list, [](int) {
			return st::flashgramVerifyStateHeight;
		}, [=](QPainter &p, QRect r) {
			const auto card = CardRect(r);
			PaintCard(p, card, C().card);
			DrawText(
				p,
				st::flashgramVerifyTextFont,
				C().sub,
				card.marginsRemoved(st::flashgramVerifyCardPadding),
				*message,
				int(Qt::AlignCenter | Qt::TextWordWrap));
		});
		block->resizeToWidth(list->width());
	};
	const auto fill = [=](std::vector<Server::DirectoryEntry> entries) {
		list->clear();
		if (entries.empty()) {
			showMessage(catalog
				? Tr(
					"No public profiles in this category yet.",
					"В этой категории пока нет публичных профилей.")
				: Tr(
					"No public FlashGram Verified profiles yet.",
					"Пока нет публичных профилей FlashGram Verified."));
			return;
		}
		for (auto i = 0; i != int(entries.size()); ++i) {
			const auto entry = entries[i];
			const auto rank = i + 1;
			const auto row = AddBlock(list, [](int) {
				return st::flashgramVerifyListRowHeight
					+ st::flashgramVerifyListMargin.top()
					+ st::flashgramVerifyListMargin.bottom();
			}, [=](QPainter &p, QRect r) {
				const auto card = r.marginsRemoved(st::flashgramVerifyListMargin);
				FillRounded(
					p,
					QRectF(card),
					st::flashgramVerifyCardRadius * 0.75,
					C().card);
				const auto padding = st::flashgramVerifyCardPadding;
				auto left = card.x() + padding.left();
				if (!catalog) {
					const auto rankColor = (rank == 1)
						? QColor(0xF7, 0xC9, 0x4A)
						: (rank == 2)
						? QColor(0xC8, 0xCF, 0xDA)
						: (rank == 3)
						? QColor(0xE0, 0x9A, 0x62)
						: C().sub;
					DrawText(
						p,
						st::flashgramVerifyRankFont,
						rankColor,
						QRect(left, card.y(), st::flashgramVerifyRankWidth, card.height()),
						QString::number(rank));
					left += st::flashgramVerifyRankWidth;
				}
				const auto avatar = st::flashgramVerifyListAvatar;
				const auto type = TypeIndex(entry.type);
				PaintTypeAvatar(
					p,
					QRect(left, card.y() + (card.height() - avatar) / 2, avatar, avatar),
					type,
					entry.displayName);
				left += avatar + st::flashgramVerifyRowSkip;
				const auto width = card.right() - padding.right() - left;
				const auto &nameFont = st::flashgramVerifyNameFont;
				const auto &textFont = st::flashgramVerifyTextFont;
				const auto top = card.y()
					+ (card.height() - nameFont->height - textFont->height) / 2;
				PaintNameWithBadge(
					p,
					QRect(left, top, width, nameFont->height),
					nameFont,
					C().text,
					entry.displayName,
					entry.level,
					false);
				const auto handle = Handle(entry.username);
				DrawText(
					p,
					textFont,
					C().sub,
					QRect(left, top + nameFont->height, width, textFont->height),
					textFont->elided(
						(handle.isEmpty() ? QString() : (handle + u" · "_q))
							+ TypeName(type)
							+ u" · "_q
							+ LevelShort(entry.level),
						width));
			});
			row->resizeToWidth(list->width());
		}
	};
	const auto load = [=] {
		showMessage(Tr("Loading...", "Загрузка..."));
		Server::RequestDirectory(
			catalog ? state->category.current() : QString(),
			crl::guard(list, [=](
					QString error,
					std::vector<Server::DirectoryEntry> entries) {
				if (!error.isEmpty()) {
					showMessage(ServerErrorText(error));
				} else {
					fill(std::move(entries));
				}
			}));
	};
	const auto pageIndex = catalog ? kPageCatalog : kPageTop;
	rpl::merge(
		state->page.value() | rpl::filter([=](int page) {
			return (page == pageIndex);
		}) | rpl::to_empty,
		state->category.changes() | rpl::filter([=](const QString &) {
			return catalog;
		}) | rpl::to_empty
	) | rpl::on_next([=] {
		load();
	}, list->lifetime());
}

void Screen::setupProfilePage(not_null<Ui::VerticalLayout*> page) {
	const auto state = _state;
	const auto card = AddBlock(page, [=](int) {
		const auto verification = Server::CachedVerification(userId());
		const auto badges = verification && !verification->badges.empty();
		return st::flashgramVerifyProfileHeight
			- (badges
				? 0
				: (st::flashgramVerifyChipHeight
					+ st::flashgramVerifyCaptionSkip));
	}, [=](QPainter &p, QRect r) {
		const auto rect = CardRect(r);
		auto fill = QLinearGradient(rect.topLeft(), rect.bottomLeft());
		fill.setColorAt(0., QColor(0x22, 0x3A, 0x6E));
		fill.setColorAt(1., C().card);
		PaintCard(p, rect, C().card);
		FillRounded(p, QRectF(rect), st::flashgramVerifyCardRadius, fill);

		const auto verification = Server::CachedVerification(userId());
		const auto approved = FindApproved(verification);
		const auto inner = rect.marginsRemoved(st::flashgramVerifyCardPadding);
		const auto avatar = st::flashgramVerifyProfileAvatar;
		const auto avatarRect = QRect(
			inner.x() + (inner.width() - avatar) / 2,
			inner.y(),
			avatar,
			avatar);
		paintUserpic(p, avatarRect);
		auto top = avatarRect.bottom() + st::flashgramVerifyCaptionSkip;
		const auto &nameFont = st::flashgramVerifyPanelTitleFont;
		PaintNameWithBadge(
			p,
			QRect(inner.x(), top, inner.width(), nameFont->height),
			nameFont,
			C().text,
			_user->name(),
			approved ? approved->level : QString(),
			true);
		top += nameFont->height;

		auto status = QString();
		auto statusColor = C().sub;
		if (!verification) {
			status = Server::VerificationError(userId()).isEmpty()
				? Tr("Loading...", "Загрузка...")
				: ServerErrorText(Server::VerificationError(userId()));
		} else if (approved) {
			status = VerificationLevelName(approved->level)
				+ u" · "_q
				+ Tr("since %1", "с %1").arg(FormatDate(approved->reviewedAt));
			statusColor = C().green;
		} else if (const auto pending = ranges::find(
				verification->requests,
				u"pending"_q,
				&Server::VerificationRequest::status)
			; pending != end(verification->requests)) {
			status = Tr("Request under review", "Заявка на рассмотрении");
			statusColor = C().lavender;
		} else {
			status = Tr("Not verified in FlashGram", "Не верифицирован в FlashGram");
		}
		DrawText(
			p,
			st::flashgramVerifyTextFont,
			statusColor,
			QRect(inner.x(), top, inner.width(), st::flashgramVerifyTextFont->height),
			status,
			int(Qt::AlignCenter));
		top += st::flashgramVerifyTextFont->height + st::flashgramVerifyCaptionSkip;

		if (verification && !verification->badges.empty()) {
			const auto height = st::flashgramVerifyChipHeight;
			const auto &font = st::flashgramVerifyChipFont;
			const auto badgeHeight = st::flashgramVerifyBadgeHeight;
			const auto badgeWidth = VerificationBadgeWidth(badgeHeight);
			auto widths = std::vector<int>();
			auto full = 0;
			for (const auto &badge : verification->badges) {
				const auto width = st::flashgramVerifyChipPadding
					+ badgeWidth
					+ st::flashgramVerifyInlineSkip
					+ font->width(LevelShort(badge.type))
					+ st::flashgramVerifyChipPadding;
				widths.push_back(width);
				full += width + st::flashgramVerifyChipSkip;
			}
			full -= st::flashgramVerifyChipSkip;
			auto left = inner.x() + std::max((inner.width() - full) / 2, 0);
			for (auto i = 0; i != int(widths.size()); ++i) {
				const auto chip = QRect(left, top, widths[i], height);
				FillRounded(p, QRectF(chip), height / 2., C().inner);
				PaintVerificationBadge(
					p,
					QRectF(
						chip.x() + st::flashgramVerifyChipPadding,
						chip.y() + (height - badgeHeight) / 2.,
						badgeWidth,
						badgeHeight),
					verification->badges[i].type);
				DrawText(
					p,
					font,
					C().text,
					QRect(
						chip.x() + st::flashgramVerifyChipPadding
							+ badgeWidth + st::flashgramVerifyInlineSkip,
						chip.y(),
						chip.width(),
						height),
					LevelShort(verification->badges[i].type));
				left += widths[i] + st::flashgramVerifyChipSkip;
			}
		}
	});
	refreshOn(card);

	const auto listingBlock = addRowCard(page, [=](QPainter &p, QRect r) {
		const auto approved = FindApproved(Server::CachedVerification(userId()));
		const auto on = approved && approved->publicListing;
		const auto size = st::flashgramVerifyToggleSize;
		const auto track = QRectF(
			r.x(),
			r.y() + (r.height() - size.height()) / 2.,
			size.width(),
			size.height());
		FillRounded(p, track, track.height() / 2., on ? C().green : C().inner);
		const auto knob = track.height() - 4.;
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255));
		p.drawEllipse(QRectF(
			on ? (track.right() - knob - 2.) : (track.x() + 2.),
			track.y() + 2.,
			knob,
			knob));
	}, [] {
		return Tr("Show in Top and Catalog", "Показывать в Топе и Каталоге");
	}, [=] {
		return FindApproved(Server::CachedVerification(userId())) != nullptr;
	}, [=] {
		const auto approved = FindApproved(Server::CachedVerification(userId()));
		if (!approved) {
			return;
		}
		Server::SetVerificationListing(
			userId(),
			approved->type,
			!approved->publicListing,
			crl::guard(_box, [=](QString error) {
				if (!error.isEmpty()) {
					_controller->uiShow()->showToast(ServerErrorText(error));
				}
			}));
	});

	addRowCard(page, [](QPainter &p, QRect r) {
		FillRounded(p, QRectF(r), r.height() * 0.28, QColor(0xF2, 0x8C, 0x3B));
		const auto &icon = st::menuIconAdmin;
		icon.paint(
			p,
			r.x() + (r.width() - icon.width()) / 2,
			r.y() + (r.height() - icon.height()) / 2,
			r.width(),
			QColor(255, 255, 255));
	}, [] {
		return Tr("Verification Requests", "Заявки на верификацию");
	}, [=] {
		const auto verification = Server::CachedVerification(userId());
		return verification && verification->admin;
	}, [=] {
		_controller->show(Box(VerificationRequestsBox, _controller));
	});

	addRowCard(page, [](QPainter &p, QRect r) {
		FillRounded(p, QRectF(r), r.height() * 0.28, QColor(0x5B, 0x6C, 0xF0));
		const auto &icon = st::menuIconInfo;
		icon.paint(
			p,
			r.x() + (r.width() - icon.width()) / 2,
			r.y() + (r.height() - icon.height()) / 2,
			r.width(),
			QColor(255, 255, 255));
	}, [] {
		return Tr("Refresh status", "Обновить статус");
	}, nullptr, [=] {
		Server::RequestVerification(userId(), true);
	});

	addNote(page, [] {
		return Tr(
			"Badges are FlashGram badges only. They are shown inside "
			"FlashGram and are not Telegram verification.",
			"Все бейджи — это бейджи FlashGram. Они видны внутри "
			"FlashGram и не являются верификацией Telegram.");
	});
	listingBlock->refresh();
}

[[nodiscard]] rpl::event_stream<> &QueueChanges() {
	static auto result = rpl::event_stream<>();
	return result;
}

void ReasonBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		QString requestId,
		QString action,
		Fn<void()> done) {
	const auto userId = peerToUser(controller->session().user()->id).bare;
	box->setTitle((action == u"revoke"_q)
		? TrValue("Revoke verification", "Отозвать верификацию")
		: TrValue("Reject request", "Отклонить заявку"));
	box->setWidth(st::boxWideWidth);
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		TrValue("Reason (shown to the user)", "Причина (увидит пользователь)"),
		QString()));
	field->setMaxLength(300);
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto sending = box->lifetime().make_state<bool>(false);
	box->addButton(
		(action == u"revoke"_q)
			? TrValue("Revoke", "Отозвать")
			: TrValue("Reject", "Отклонить"),
		[=] {
			if (*sending) {
				return;
			}
			*sending = true;
			Server::ReviewVerification(
				userId,
				requestId,
				action,
				field->getLastText().trimmed(),
				crl::guard(box, [=](QString error) {
					*sending = false;
					if (!error.isEmpty()) {
						controller->uiShow()->showToast(ServerErrorText(error));
						return;
					}
					QueueChanges().fire({});
					done();
					box->closeBox();
				}));
		});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void AddDetailsField(
		not_null<Ui::GenericBox*> box,
		const QString &name,
		const QString &value) {
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
}

void RequestDetailsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Server::VerificationRequest request) {
	const auto userId = peerToUser(controller->session().user()->id).bare;
	const auto type = TypeIndex(request.type);
	box->setTitle(TrValue("Verification request", "Заявка на верификацию"));
	box->setWidth(st::boxWideWidth);

	const auto identity = Identity{
		.type = type,
		.level = request.level,
		.name = request.displayName,
		.handle = Handle((type == kTypePersonal)
			? request.username
			: request.target),
	};
	box->addRow(
		object_ptr<Block>(
			box,
			st::boxBg->c,
			[](int) {
				return st::flashgramVerifyCardMargin.top()
					+ st::flashgramVerifyCardPadding.top()
					+ IdentityHeight()
					+ st::flashgramVerifyCardPadding.bottom()
					+ st::flashgramVerifyCardMargin.bottom();
			},
			[=](QPainter &p, QRect r) {
				const auto card = CardRect(r);
				PaintCard(p, card, C().card);
				PaintIdentity(
					p,
					card.marginsRemoved(st::flashgramVerifyCardPadding),
					identity,
					nullptr);
			}),
		QMargins());
	Ui::AddSkip(box->verticalLayout());
	AddDetailsField(box, u"FlashGram ID"_q, request.flashgramId);
	AddDetailsField(box, Tr("Type", "Тип"), TypeName(type));
	AddDetailsField(box, Tr("Badge", "Бейдж"), VerificationLevelName(request.level));
	AddDetailsField(box, u"Username"_q, Handle(request.username));
	if (type != kTypePersonal) {
		AddDetailsField(box, TypeName(type), Handle(request.target));
	}
	AddDetailsField(box, Tr("Submitted", "Подана"), FormatDate(request.createdAt));
	AddDetailsField(box, Tr("Status", "Статус"), StatusName(request.status));
	AddDetailsField(box, Tr("Reviewed", "Рассмотрена"), FormatDate(request.reviewedAt));
	AddDetailsField(box, Tr("Reason", "Причина"), request.reason);
	AddDetailsField(
		box,
		Tr("Public listing", "Публичность"),
		request.publicListing ? Tr("On", "Включена") : Tr("Off", "Выключена"));

	const auto close = [=] {
		box->closeBox();
	};
	if (request.status == u"pending"_q) {
		const auto sending = box->lifetime().make_state<bool>(false);
		box->addButton(TrValue("Approve", "Одобрить"), [=] {
			if (*sending) {
				return;
			}
			*sending = true;
			Server::ReviewVerification(
				userId,
				request.id,
				u"approve"_q,
				QString(),
				crl::guard(box, [=](QString error) {
					*sending = false;
					if (!error.isEmpty()) {
						controller->uiShow()->showToast(ServerErrorText(error));
						return;
					}
					controller->uiShow()->showToast(Tr(
						"Approved. FlashGram badge granted.",
						"Одобрено. Бейдж FlashGram выдан."));
					QueueChanges().fire({});
					box->closeBox();
				}));
		});
		box->addButton(TrValue("Reject", "Отклонить"), [=] {
			controller->show(Box(
				ReasonBox,
				controller,
				request.id,
				u"reject"_q,
				crl::guard(box, close)));
		});
	} else if (request.status == u"approved"_q) {
		box->addButton(TrValue("Revoke", "Отозвать"), [=] {
			controller->show(Box(
				ReasonBox,
				controller,
				request.id,
				u"revoke"_q,
				crl::guard(box, close)));
		});
	}
	box->addButton(tr::lng_close(), close);
}

} // namespace

void PaintVerificationBadge(
		QPainter &p,
		const QRectF &rect,
		const QString &level) {
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = LevelColors(level);
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., colors.first);
	gradient.setColorAt(1., colors.second);
	const auto h = rect.height();
	const auto radius = h * 0.32;
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	p.fillPath(path, gradient);
	p.setPen(QPen(QColor(255, 255, 255, 80), std::max(h / 16., 1.)));
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);

	auto font = st::semiboldFont->f;
	font.setPixelSize(std::max(int(h * 0.58), 1));
	font.setBold(true);
	p.setFont(font);
	p.setPen(QColor(255, 255, 255));
	p.drawText(
		QRectF(rect.x() + h * 0.2, rect.y(), rect.width() * 0.6, h),
		int(Qt::AlignLeft | Qt::AlignVCenter),
		u"FG"_q);
	PaintCheckMark(
		p,
		QPointF(rect.right() - h * 0.47, rect.center().y()),
		h * 0.42,
		QColor(255, 255, 255),
		std::max(h / 8., 1.3));
}

int VerificationBadgeWidth(int height) {
	return int(std::round(height * 1.95));
}

QString VerificationLevelName(const QString &level) {
	return u"FlashGram "_q + LevelShort(level);
}

void VerificationBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->lifetime().make_state<Screen>(box, controller);
}

void VerificationRequestsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto userId = peerToUser(controller->session().user()->id).bare;
	box->setStyle(ScreenBoxStyle());
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::flashgramVerifyBoxHeight);

	const auto status = box->lifetime().make_state<rpl::variable<int>>(0);
	const auto statuses = std::array{
		std::pair{ u"pending"_q, Tr("Pending", "Ожидают") },
		std::pair{ u"approved"_q, Tr("Approved", "Одобрены") },
		std::pair{ u"rejected"_q, Tr("Rejected", "Отклонены") },
	};

	const auto header = box->setPinnedToTopContent(object_ptr<Block>(
		box,
		C().bg,
		[](int) {
			return st::flashgramVerifyHeaderHeight
				+ st::flashgramVerifyTabsHeight
				+ st::flashgramVerifyCardMargin.bottom();
		},
		[=](QPainter &p, QRect r) {
			const auto &font = st::flashgramVerifyTitleFont;
			const auto &subFont = st::flashgramVerifyHeaderSubFont;
			const auto left = st::flashgramVerifySide
				+ st::flashgramVerifyIconButton
				+ st::flashgramVerifyTitleSkip;
			const auto top = (st::flashgramVerifyHeaderHeight
				- font->height
				- subFont->height) / 2;
			DrawText(
				p,
				font,
				C().text,
				QRect(left, top, r.width() - left, font->height),
				u"Verification Requests"_q);
			DrawText(
				p,
				subFont,
				C().sub,
				QRect(left, top + font->height, r.width() - left, subFont->height),
				Tr("FlashGram admin panel", "Панель администратора FlashGram"));
		}));
	const auto back = CreateButton(header, [=](QPainter &p, QRect r, bool over) {
		if (over) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 26));
			p.drawEllipse(r);
		}
		PaintBackArrow(
			p,
			QRectF(r).center(),
			st::flashgramVerifyIconSize,
			C().text);
	}, [=] {
		box->closeBox();
	});
	const auto tabs = Ui::CreateChild<Ui::RpWidget>(header.get());
	tabs->show();
	tabs->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(tabs);
		const auto r = QRectF(tabs->rect());
		FillRounded(p, r, r.height() / 2., C().card);
		const auto inset = st::flashgramVerifyTabsInset;
		const auto third = r.width() / 3.;
		const auto active = status->current();
		FillRounded(
			p,
			QRectF(active * third + inset, inset, third - 2 * inset, r.height() - 2 * inset),
			(r.height() - 2 * inset) / 2.,
			C().inner);
		for (auto i = 0; i != 3; ++i) {
			DrawText(
				p,
				st::flashgramVerifyTabsFont,
				(i == active) ? C().text : C().sub,
				QRect(int(i * third), 0, int(third), int(r.height())),
				statuses[i].second,
				int(Qt::AlignCenter));
		}
	}, tabs->lifetime());
	for (auto i = 0; i != 3; ++i) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(tabs);
		button->setClickedCallback([=] {
			*status = i;
			tabs->update();
		});
		button->show();
		tabs->widthValue() | rpl::on_next([=](int width) {
			const auto third = width / 3;
			button->setGeometry(
				i * third,
				0,
				(i == 2) ? (width - 2 * third) : third,
				st::flashgramVerifyTabsHeight);
		}, button->lifetime());
	}
	header->setLayoutCallback([=](int width, int) {
		const auto button = st::flashgramVerifyIconButton;
		const auto side = st::flashgramVerifySide;
		back->setGeometry(
			side,
			(st::flashgramVerifyHeaderHeight - button) / 2,
			button,
			button);
		tabs->setGeometry(
			st::flashgramVerifyCardMargin.left(),
			st::flashgramVerifyHeaderHeight,
			width - st::flashgramVerifyCardMargin.left()
				- st::flashgramVerifyCardMargin.right(),
			st::flashgramVerifyTabsHeight);
	});

	const auto list = box->verticalLayout();
	const auto message = list->lifetime().make_state<QString>();
	const auto showMessage = [=](const QString &text) {
		list->clear();
		*message = text;
		AddBlock(list, [](int) {
			return st::flashgramVerifyStateHeight;
		}, [=](QPainter &p, QRect r) {
			const auto card = CardRect(r);
			PaintCard(p, card, C().card);
			DrawText(
				p,
				st::flashgramVerifyTextFont,
				C().sub,
				card.marginsRemoved(st::flashgramVerifyCardPadding),
				*message,
				int(Qt::AlignCenter | Qt::TextWordWrap));
		})->resizeToWidth(list->width());
	};
	const auto fill = [=](std::vector<Server::VerificationRequest> requests) {
		list->clear();
		if (requests.empty()) {
			showMessage(Tr("No requests here.", "Здесь пока нет заявок."));
			return;
		}
		for (const auto &request : requests) {
			const auto row = AddBlock(list, [](int) {
				return st::flashgramVerifyListRowHeight
					+ st::flashgramVerifyListMargin.top()
					+ st::flashgramVerifyListMargin.bottom();
			}, nullptr);
			const auto button = CreateButton(row, [=](
					QPainter &p,
					QRect r,
					bool over) {
				FillRounded(
					p,
					QRectF(r),
					st::flashgramVerifyCardRadius * 0.75,
					over ? C().cardOver : C().card);
				const auto padding = st::flashgramVerifyCardPadding;
				const auto type = TypeIndex(request.type);
				const auto avatar = st::flashgramVerifyListAvatar;
				PaintTypeAvatar(
					p,
					QRect(padding.left(), (r.height() - avatar) / 2, avatar, avatar),
					type,
					request.displayName);
				const auto left = padding.left() + avatar + st::flashgramVerifyRowSkip;
				const auto &nameFont = st::flashgramVerifyNameFont;
				const auto &textFont = st::flashgramVerifyTextFont;
				const auto date = FormatDate(request.createdAt);
				const auto dateWidth = textFont->width(date);
				const auto width = r.width() - left - padding.right();
				const auto top = (r.height() - nameFont->height - textFont->height) / 2;
				PaintNameWithBadge(
					p,
					QRect(
						left,
						top,
						width - dateWidth - st::flashgramVerifyInlineSkip,
						nameFont->height),
					nameFont,
					C().text,
					request.displayName,
					request.level,
					false);
				DrawText(
					p,
					textFont,
					C().sub,
					QRect(left, top, width, nameFont->height),
					date,
					int(Qt::AlignRight | Qt::AlignVCenter));
				DrawText(
					p,
					textFont,
					(request.status == u"pending"_q) ? C().sub : StatusColor(request.status),
					QRect(left, top + nameFont->height, width, textFont->height),
					textFont->elided(
						request.flashgramId
							+ u" · "_q
							+ TypeName(type)
							+ u" · "_q
							+ ((request.status == u"pending"_q)
								? LevelShort(request.level)
								: StatusName(request.status)),
						width));
			}, [=] {
				controller->show(Box(RequestDetailsBox, controller, request));
			});
			row->setLayoutCallback([=](int width, int height) {
				button->setGeometry(QRect(0, 0, width, height).marginsRemoved(
					st::flashgramVerifyListMargin));
			});
			row->resizeToWidth(list->width());
		}
	};
	const auto load = [=] {
		showMessage(Tr("Loading...", "Загрузка..."));
		Server::RequestVerificationQueue(
			userId,
			statuses[status->current()].first,
			crl::guard(list, [=](
					QString error,
					std::vector<Server::VerificationRequest> requests) {
				if (!error.isEmpty()) {
					showMessage(ServerErrorText(error));
				} else {
					fill(std::move(requests));
				}
			}));
	};
	rpl::merge(
		status->value() | rpl::to_empty,
		QueueChanges().events()
	) | rpl::on_next([=] {
		load();
	}, list->lifetime());
	AddBlock(
		box->verticalLayout(),
		[](int) { return 0; },
		nullptr);
}

} // namespace FlashGram
