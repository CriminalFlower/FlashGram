/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_themes.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_row.h"
#include "flashgram/flashgram_liquid.h"
#include "flashgram/flashgram_state.h"
#include "flashgram/flashgram_ui.h"
#include "history/history.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/userpic_view.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainterPath>

#include <cmath>

namespace FlashGram {
namespace {

using namespace Design;

[[nodiscard]] QString PaletteName(const LiquidPalette &palette) {
	return Tr(palette.nameEn, palette.nameRu);
}

// A small live copy of the theme wallpaper: four soft color blobs
// drifting over the dark base, like the flowing chat background.
void PaintThemePreview(
		QPainter &p,
		const QRectF &rect,
		const LiquidPalette &palette,
		float64 t) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = float64(st::flashgramThemePreviewRadius);
	auto clip = QPainterPath();
	clip.addRoundedRect(rect, radius, radius);
	p.save();
	p.setClipPath(clip);
	p.fillRect(rect, palette.wallpaper[0]);

	const auto w = rect.width();
	const auto h = rect.height();
	struct Blob {
		int color;
		float64 x, y, dx, dy, speed, size;
	};
	const auto blobs = std::array<Blob, 4>{ {
		{ 1, 0.25, 0.30, 0.22, 0.18, 0.90, 0.85 },
		{ 3, 0.75, 0.70, 0.20, 0.22, 1.15, 0.80 },
		{ 2, 0.70, 0.20, 0.18, 0.15, 0.70, 0.70 },
		{ 1, 0.30, 0.80, 0.15, 0.20, 1.35, 0.60 },
	} };
	for (const auto &blob : blobs) {
		const auto phase = t * blob.speed;
		const auto center = QPointF(
			rect.x() + w * (blob.x + blob.dx * std::sin(phase)),
			rect.y() + h * (blob.y + blob.dy * std::cos(phase * 1.3)));
		const auto size = std::max(w, h) * blob.size;
		auto gradient = QRadialGradient(center, size / 2.);
		auto color = palette.wallpaper[blob.color];
		gradient.setColorAt(0., color);
		color.setAlpha(0);
		gradient.setColorAt(1., color);
		p.fillRect(rect, gradient);
	}

	// Glass sheen sweeping across.
	const auto period = 3.2;
	const auto phase = std::fmod(t, period) / period;
	const auto band = w * 0.5;
	const auto x = rect.x() - band + (w + 2 * band) * phase;
	auto sheen = QLinearGradient(
		QPointF(x, rect.top()),
		QPointF(x + band, rect.bottom()));
	sheen.setColorAt(0., QColor(255, 255, 255, 0));
	sheen.setColorAt(0.5, QColor(255, 255, 255, 46));
	sheen.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillRect(rect, sheen);
	p.restore();

	p.setPen(QPen(QColor(255, 255, 255, 36), 1.));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

[[nodiscard]] QChar GroupLetter(const QString &name) {
	for (const auto &ch : name) {
		if (ch.isLetter()) {
			return ch.toUpper();
		} else if (!ch.isSpace()) {
			break;
		}
	}
	return QChar('#');
}

[[nodiscard]] QString Digits(const QString &text) {
	auto result = QString();
	for (const auto &ch : text) {
		if (ch.isDigit()) {
			result.append(ch);
		}
	}
	return result;
}

struct ContactGroup {
	QChar letter;
	std::vector<not_null<UserData*>> users;
};

[[nodiscard]] std::vector<not_null<UserData*>> CollectContacts(
		not_null<Main::Session*> session) {
	auto result = std::vector<not_null<UserData*>>();
	for (const auto &row : session->data().contactsList()->all()) {
		if (const auto history = row->history()) {
			if (const auto user = history->peer->asUser()) {
				if (!user->isSelf() && !user->isInaccessible()) {
					result.push_back(user);
				}
			}
		}
	}
	ranges::sort(result, [](not_null<UserData*> a, not_null<UserData*> b) {
		return QString::localeAwareCompare(a->name(), b->name()) < 0;
	});
	return result;
}

[[nodiscard]] bool Matches(not_null<UserData*> user, const QString &query) {
	if (query.isEmpty()) {
		return true;
	}
	if (user->name().contains(query, Qt::CaseInsensitive)
		|| user->username().contains(query, Qt::CaseInsensitive)) {
		return true;
	}
	const auto digits = Digits(query);
	return !digits.isEmpty() && user->phone().contains(digits);
}

[[nodiscard]] std::vector<ContactGroup> GroupContacts(
		const std::vector<not_null<UserData*>> &users,
		const QString &query) {
	auto result = std::vector<ContactGroup>();
	for (const auto &user : users) {
		if (!Matches(user, query)) {
			continue;
		}
		const auto letter = GroupLetter(user->name());
		const auto i = ranges::find(result, letter, &ContactGroup::letter);
		if (i != end(result)) {
			i->users.push_back(user);
		} else {
			result.push_back({ letter, { user } });
		}
	}
	ranges::stable_sort(result, [](
			const ContactGroup &a,
			const ContactGroup &b) {
		if ((a.letter == '#') != (b.letter == '#')) {
			return (b.letter == '#');
		}
		return QString::localeAwareCompare(
			QString(a.letter),
			QString(b.letter)) < 0;
	});
	return result;
}

void AddContactsSummary(
		not_null<Ui::VerticalLayout*> layout,
		const std::vector<not_null<UserData*>> &users) {
	const auto now = base::unixtime::now();
	auto withPhone = 0;
	auto online = 0;
	for (const auto &user : users) {
		if (!user->phone().isEmpty()) {
			++withPhone;
		}
		if (Data::OnlineTextActive(user, now)) {
			++online;
		}
	}
	const auto stats = std::array<std::pair<int, QString>, 3>{ {
		{ int(users.size()), Tr("contacts", "контактов") },
		{ withPhone, Tr("with number", "с номером") },
		{ online, Tr("online", "в сети") },
	} };
	AddBlock(layout, [](int) {
		return st::flashgramContactsSummaryHeight;
	}, [=](QPainter &p, QRect r) {
		const auto card = CardRect(r);
		PaintCard(p, card, C().card);
		const auto part = card.width() / int(stats.size());
		const auto &number = st::flashgramContactsStatNumberFont;
		const auto &label = st::flashgramContactsStatLabelFont;
		const auto top = card.y()
			+ (card.height() - number->height - label->height) / 2;
		for (auto i = 0; i != int(stats.size()); ++i) {
			const auto left = card.x() + part * i;
			DrawText(
				p,
				number,
				(i == 2) ? C().green : C().text,
				QRect(left, top, part, number->height),
				QString::number(stats[i].first),
				Qt::AlignHCenter | Qt::AlignVCenter);
			DrawText(
				p,
				label,
				C().sub,
				QRect(left, top + number->height, part, label->height),
				stats[i].second,
				Qt::AlignHCenter | Qt::AlignVCenter);
		}
	});
}

void AddContactsGroup(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Window::SessionController*> controller,
		ContactGroup group) {
	struct State {
		base::flat_map<not_null<UserData*>, Ui::PeerUserpicView> userpics;
		int over = -1;
	};
	const auto count = int(group.users.size());
	const auto state = std::make_shared<State>();
	const auto users = std::make_shared<std::vector<not_null<UserData*>>>(
		std::move(group.users));
	const auto letter = QString(group.letter);
	const auto side = st::flashgramVerifySide;
	const auto paint = [=](QPainter &p, QRect r) {
		auto hq = PainterHighQualityEnabler(p);
		const auto card = QRect(
			side,
			0,
			r.width() - 2 * side,
			r.height() - st::flashgramThemeCardSkip);
		PaintCard(p, card, C().card);

		const auto &palette = LiquidThemePalette(CurrentLiquidTheme());
		const auto padding = st::flashgramCardPadding;
		const auto &letterFont = st::flashgramContactsLetterFont;
		const auto &countFont = st::flashgramContactsCountFont;
		const auto header = st::flashgramContactsBlockHeader;
		DrawText(
			p,
			letterFont,
			palette.accent,
			QRect(card.x() + padding.left(), 0, card.width() / 2, header),
			letter);
		const auto badge = QString::number(count);
		const auto badgeWidth = countFont->width(badge) + countFont->height;
		const auto badgeRect = QRect(
			card.x() + card.width() - padding.right() - badgeWidth,
			(header - countFont->height - 4) / 2,
			badgeWidth,
			countFont->height + 4);
		FillRounded(p, badgeRect, badgeRect.height() / 2., C().inner);
		DrawText(
			p,
			countFont,
			C().sub,
			badgeRect,
			badge,
			Qt::AlignCenter);

		const auto now = base::unixtime::now();
		const auto size = st::flashgramContactsUserpic;
		const auto &nameFont = st::flashgramContactsNameFont;
		const auto &phoneFont = st::flashgramContactsPhoneFont;
		for (auto i = 0; i != count; ++i) {
			const auto user = (*users)[i];
			const auto top = header + i * st::flashgramContactsRowHeight;
			const auto row = QRect(
				card.x(),
				top,
				card.width(),
				st::flashgramContactsRowHeight);
			if (state->over == i) {
				p.fillRect(row, C().cardOver);
			}
			if (i > 0) {
				p.fillRect(
					QRect(
						row.x() + padding.left() + size + padding.left(),
						top,
						row.width() - 2 * padding.left() - size,
						1),
					QColor(255, 255, 255, 14));
			}
			const auto left = row.x() + padding.left();
			user->paintUserpic(
				p,
				state->userpics[user],
				left,
				top + (row.height() - size) / 2,
				size);

			const auto textLeft = left + size + padding.left();
			const auto textRight = row.x() + row.width() - padding.right();
			const auto textWidth = std::max(textRight - textLeft, 0);
			const auto lines = nameFont->height + phoneFont->height + 2;
			const auto textTop = top + (row.height() - lines) / 2;
			DrawText(
				p,
				nameFont,
				C().text,
				QRect(textLeft, textTop, textWidth, nameFont->height),
				nameFont->elided(user->name(), textWidth));

			const auto phone = user->phone();
			const auto active = Data::OnlineTextActive(user, now);
			const auto status = Data::OnlineText(user, now);
			const auto second = phone.isEmpty()
				? Tr("number hidden", "номер скрыт")
				: Ui::FormatPhone(phone);
			const auto phoneTop = textTop + nameFont->height + 2;
			const auto secondWidth = phoneFont->width(second);
			DrawText(
				p,
				phoneFont,
				phone.isEmpty() ? C().sub : palette.accent,
				QRect(textLeft, phoneTop, textWidth, phoneFont->height),
				phoneFont->elided(second, textWidth));
			const auto statusLeft = textLeft
				+ secondWidth
				+ phoneFont->spacew * 3;
			if (statusLeft < textRight) {
				DrawText(
					p,
					phoneFont,
					active ? C().green : C().sub,
					QRect(
						statusLeft,
						phoneTop,
						textRight - statusLeft,
						phoneFont->height),
					phoneFont->elided(
						u"· "_q + status,
						textRight - statusLeft));
			}
		}
	};
	const auto block = AddBlock(layout, [=](int) {
		return st::flashgramContactsBlockHeader
			+ count * st::flashgramContactsRowHeight
			+ st::flashgramContactsBlockBottom
			+ st::flashgramThemeCardSkip;
	}, paint);

	const auto rowAt = [=](QPoint point) {
		const auto y = point.y() - st::flashgramContactsBlockHeader;
		if (y < 0 || point.x() < side || point.x() > block->width() - side) {
			return -1;
		}
		const auto index = y / st::flashgramContactsRowHeight;
		return (index < count) ? index : -1;
	};
	block->setMouseTracking(true);
	block->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto over = rowAt(static_cast<QMouseEvent*>(e.get())->pos());
			if (state->over != over) {
				state->over = over;
				block->setCursor((over >= 0)
					? style::cur_pointer
					: style::cur_default);
				block->update();
			}
		} else if (type == QEvent::Leave) {
			state->over = -1;
			block->update();
		} else if (type == QEvent::MouseButtonRelease) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			const auto index = rowAt(mouse->pos());
			if (mouse->button() == Qt::LeftButton && index >= 0) {
				controller->showPeerHistory((*users)[index]);
			}
		}
	}, block->lifetime());

	controller->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		block->update();
	}, block->lifetime());

}

void FillContacts(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Window::SessionController*> controller,
		const QString &query) {
	layout->clear();
	const auto session = &controller->session();
	const auto users = CollectContacts(session);
	if (query.isEmpty()) {
		AddContactsSummary(layout, users);
	}
	const auto groups = GroupContacts(users, query);
	if (groups.empty()) {
		const auto text = !session->data().contactsLoaded().current()
			? Tr("Loading contacts...", "Загружаем контакты...")
			: query.isEmpty()
			? Tr("No contacts yet.", "Контактов пока нет.")
			: Tr("Nothing found.", "Ничего не найдено.");
		AddBlock(layout, [](int) {
			return st::flashgramContactsSummaryHeight;
		}, [=](QPainter &p, QRect r) {
			DrawText(
				p,
				st::flashgramThemeSubFont,
				C().sub,
				r,
				text,
				Qt::AlignCenter);
		});
	}
	for (auto group : groups) {
		AddContactsGroup(layout, controller, std::move(group));
	}
	layout->resizeToWidth(layout->width());
}

} // namespace

void ThemesBox(not_null<Ui::GenericBox*> box) {
	SetupScreenBox(box);
	SetupScreenHeader(
		box,
		Tr("FlashGram themes", "Темы FlashGram"),
		Tr("Animated, with a flowing wallpaper", "Анимированные, с живыми обоями"));

	const auto layout = box->verticalLayout();
	const auto started = crl::now();
	const auto buttons = box->lifetime().make_state<
		std::vector<QPointer<QWidget>>>();
	const auto ticker = box->lifetime().make_state<Ui::Animations::Basic>(
		[=] {
			for (const auto &button : *buttons) {
				if (button) {
					button->update();
				}
			}
		});

	for (const auto &palette : LiquidThemes()) {
		const auto id = palette.id;
		const auto block = AddBlock(layout, [](int) {
			return st::flashgramThemeCardHeight + st::flashgramThemeCardSkip;
		}, nullptr);
		const auto button = CreateButton(block, [=](
				QPainter &p,
				QRect r,
				bool over) {
			const auto &palette = LiquidThemePalette(id);
			const auto active = (CurrentLiquidTheme() == id);
			auto hq = PainterHighQualityEnabler(p);
			PaintCard(p, r, over ? C().cardOver : C().card);
			if (active) {
				p.setPen(QPen(palette.accent, 2.));
				p.setBrush(Qt::NoBrush);
				const auto radius = float64(st::flashgramVerifyCardRadius);
				p.drawRoundedRect(
					QRectF(r).adjusted(1., 1., -1., -1.),
					radius,
					radius);
			}
			const auto padding = st::flashgramCardPadding;
			const auto preview = st::flashgramThemePreview;
			const auto previewRect = QRectF(
				r.x() + padding.left(),
				r.y() + (r.height() - preview.height()) / 2.,
				preview.width(),
				preview.height());
			const auto t = float64(crl::now() - started) / 1000.;
			PaintThemePreview(p, previewRect, palette, t);

			const auto textLeft = int(previewRect.right()) + padding.left();
			const auto markSize = 26;
			const auto textRight = r.right() - padding.right() - markSize - 8;
			const auto textWidth = std::max(textRight - textLeft, 0);
			const auto &nameFont = st::flashgramThemeNameFont;
			const auto &subFont = st::flashgramThemeSubFont;
			const auto top = r.y()
				+ (r.height() - nameFont->height - subFont->height - 4) / 2;
			DrawText(
				p,
				nameFont,
				C().text,
				QRect(textLeft, top, textWidth, nameFont->height),
				nameFont->elided(PaletteName(palette), textWidth));
			DrawText(
				p,
				subFont,
				active ? palette.accent : C().sub,
				QRect(
					textLeft,
					top + nameFont->height + 4,
					textWidth,
					subFont->height),
				subFont->elided(
					active
						? Tr("Active", "Активна")
						: Tr("Click to apply", "Нажмите, чтобы включить"),
					textWidth));

			const auto mark = QRectF(
				r.right() - padding.right() - markSize,
				r.y() + (r.height() - markSize) / 2.,
				markSize,
				markSize);
			p.setPen(Qt::NoPen);
			p.setBrush(active ? palette.accent : C().inner);
			p.drawEllipse(mark);
			if (active) {
				PaintCheckMark(p, mark.center(), markSize * 0.45, C().bg, 2.2);
			}
		}, [=] {
			if (CurrentLiquidTheme() != id) {
				ApplyLiquidTheme(id);
			}
			for (const auto &button : *buttons) {
				if (button) {
					button->update();
				}
			}
		});
		buttons->push_back(button.get());
		block->setLayoutCallback([=](int width, int height) {
			const auto side = st::flashgramVerifySide;
			button->setGeometry(
				side,
				0,
				width - 2 * side,
				st::flashgramThemeCardHeight);
		});
	}
	ticker->start();
}

void ContactsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	SetupScreenBox(box);
	SetupScreenHeader(
		box,
		Tr("Contacts", "Контакты"),
		Tr("Your contacts in blocks", "Ваши контакты по блокам"));

	const auto session = &controller->session();
	const auto layout = box->verticalLayout();
	const auto search = layout->add(
		object_ptr<Ui::InputField>(
			layout,
			st::defaultInputField,
			TrValue("Search by name or number", "Поиск по имени или номеру"),
			QString()),
		st::flashgramContactsSearchPadding);
	const auto content = layout->add(object_ptr<Ui::VerticalLayout>(layout));

	const auto refill = [=] {
		FillContacts(content, controller, search->getLastText().trimmed());
	};
	search->changes() | rpl::on_next(refill, search->lifetime());
	session->data().contactsLoaded().value(
	) | rpl::on_next([=](bool) {
		refill();
	}, content->lifetime());
	if (!session->data().contactsLoaded().current()) {
		session->api().requestContacts();
	}
	box->setFocusCallback([=] {
		search->setFocusFast();
	});
}

} // namespace FlashGram
