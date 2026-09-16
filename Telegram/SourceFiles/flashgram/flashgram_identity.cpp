/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_identity.h"

#include "base/unixtime.h"
#include "boxes/star_gift_box.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "flashgram/flashgram_server.h"
#include "flashgram/flashgram_state.h"
#include "flashgram/flashgram_ui.h"
#include "flashgram/flashgram_verification.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainterPath>

namespace FlashGram {
namespace {

using namespace Design;

constexpr auto kUsernameLinkPrefix = "flashgram-username:"_cs;
constexpr auto kTelegramLinkPrefix = "https://t.me/"_cs;
constexpr auto kFragmentUrl = "https://fragment.com"_cs;

struct ThemeColors {
	QColor card;
	QColor cardOver;
	QColor text;
	QColor sub;
	QColor accent;
};

[[nodiscard]] ThemeColors T() {
	return {
		.card = st::windowBgOver->c,
		.cardOver = st::windowBgRipple->c,
		.text = st::windowFg->c,
		.sub = st::windowSubTextFg->c,
		.accent = st::windowActiveTextFg->c,
	};
}

[[nodiscard]] QString FormatDate(TimeId date) {
	return date
		? base::unixtime::parse(date).toString(u"dd.MM.yyyy"_q)
		: QString();
}

[[nodiscard]] bool IsOrgType(const QString &type) {
	return (type == u"major"_q) || (type == u"hold"_q);
}

[[nodiscard]] std::pair<QColor, QColor> OrgColors(const QString &type) {
	return (type == u"major"_q)
		? std::pair{ QColor(0xFF, 0x6B, 0x5B), QColor(0xC2, 0x1F, 0x5B) }
		: std::pair{ QColor(0x33, 0xD6, 0xA6), QColor(0x14, 0x7C, 0xD6) };
}

[[nodiscard]] QString OrgTitle(const Server::VerificationBadge &badge) {
	return !badge.title.isEmpty()
		? badge.title
		: (badge.type == u"major"_q)
		? u"Major"_q
		: u"Hold"_q;
}

[[nodiscard]] QString OrgStatement(const Server::VerificationBadge &badge) {
	return Tr(
		"This user was verified in FlashGram by the organization '%1'.",
		"Этот пользователь верифицирован в FlashGram организацией «%1».").arg(
			OrgTitle(badge));
}

[[nodiscard]] QString OrgExtra(const Server::VerificationBadge &badge) {
	const auto defaultText = u"This user was verified in FlashGram by the "
		"organization '%1'."_q.arg(OrgTitle(badge));
	return (badge.description.isEmpty() || badge.description == defaultText)
		? QString()
		: badge.description;
}

[[nodiscard]] QString OrgMeta(const Server::VerificationBadge &badge) {
	auto parts = QStringList();
	if (badge.grantedAt) {
		parts.push_back(Tr("Issued %1", "Выдан %1").arg(
			FormatDate(badge.grantedAt)));
	}
	parts.push_back(badge.expiresAt
		? Tr("until %1", "до %1").arg(FormatDate(badge.expiresAt))
		: Tr("no expiry", "бессрочно"));
	if (!badge.issuer.isEmpty()) {
		parts.push_back(Tr("issuer: %1", "выдал: %1").arg(badge.issuer));
	}
	return parts.join(u" · "_q);
}

void PaintOrgEmblem(QPainter &p, const QRect &rect, const QString &type) {
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = OrgColors(type);
	auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
	fill.setColorAt(0., colors.first);
	fill.setColorAt(1., colors.second);
	const auto radius = rect.height() * 0.3;
	FillRounded(p, QRectF(rect), radius, fill);
	FillRounded(
		p,
		QRectF(rect.x(), rect.y(), rect.width(), rect.height() / 2.),
		radius,
		QColor(255, 255, 255, 26));

	auto font = st::semiboldFont->f;
	font.setPixelSize(std::max(int(rect.height() * 0.46), 1));
	font.setBold(true);
	p.setFont(font);
	p.setPen(QColor(255, 255, 255));
	p.drawText(rect, Qt::AlignCenter, (type == u"major"_q) ? u"M"_q : u"H"_q);

	const auto tagHeight = rect.height() * 0.32;
	const auto tag = QRectF(
		rect.right() - tagHeight * 1.5,
		rect.bottom() - tagHeight + 1,
		tagHeight * 1.6,
		tagHeight);
	FillRounded(p, tag, tagHeight / 2., QColor(0, 0, 0, 120));
	auto tagFont = st::semiboldFont->f;
	tagFont.setPixelSize(std::max(int(tagHeight * 0.62), 1));
	p.setFont(tagFont);
	p.setPen(QColor(255, 255, 255, 230));
	p.drawText(tag, Qt::AlignCenter, u"FG"_q);
}

[[nodiscard]] int OrgTextWidth(int cardWidth) {
	const auto padding = st::flashgramIdentityCardPadding;
	return cardWidth
		- padding.left()
		- st::flashgramIdentityEmblemSize
		- st::flashgramIdentitySkip
		- padding.right();
}

[[nodiscard]] int OrgCardHeight(
		const Server::VerificationBadge &badge,
		int width) {
	const auto margin = st::flashgramIdentityCardMargin;
	const auto padding = st::flashgramIdentityCardPadding;
	const auto inner = OrgTextWidth(width - margin.left() - margin.right());
	const auto lineSkip = st::flashgramIdentityLineSkip;
	const auto extra = OrgExtra(badge);
	const auto text = st::flashgramIdentityTitleFont->height
		+ lineSkip
		+ WrappedHeight(st::flashgramIdentityTextFont, OrgStatement(badge), inner)
		+ (extra.isEmpty()
			? 0
			: (lineSkip
				+ WrappedHeight(st::flashgramIdentityTextFont, extra, inner)))
		+ lineSkip
		+ WrappedHeight(st::flashgramIdentityMetaFont, OrgMeta(badge), inner);
	return margin.top()
		+ padding.top()
		+ std::max(text, int(st::flashgramIdentityEmblemSize))
		+ padding.bottom()
		+ margin.bottom();
}

void PaintSourceChip(
		QPainter &p,
		const QRect &row,
		const QString &text,
		const QColor &color) {
	const auto &font = st::flashgramIdentitySourceFont;
	const auto height = st::flashgramIdentitySourceHeight;
	const auto width = font->width(text) + 2 * st::flashgramIdentitySourcePadding;
	const auto chip = QRectF(
		row.right() + 1 - width,
		row.y() + (row.height() - height) / 2.,
		width,
		height);
	auto hq = PainterHighQualityEnabler(p);
	auto border = color;
	border.setAlpha(110);
	p.setPen(QPen(border, 1.));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(chip.marginsRemoved({ 0.5, 0.5, 0.5, 0.5 }), height / 2., height / 2.);
	DrawText(p, font, color, chip.toRect(), text, int(Qt::AlignCenter));
}

void OrgInfoBox(
		not_null<Ui::GenericBox*> box,
		Server::VerificationBadge badge) {
	box->setTitle(rpl::single(u"FlashGram · "_q + OrgTitle(badge)));
	box->setWidth(st::boxWideWidth);
	auto text = OrgStatement(badge);
	if (const auto extra = OrgExtra(badge); !extra.isEmpty()) {
		text += u"\n\n"_q + extra;
	}
	text += u"\n\n"_q + OrgMeta(badge);
	box->addRow(object_ptr<Ui::FlatLabel>(box, text, st::boxLabel));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			Tr(
				"This is a FlashGram badge issued on the FlashGram server. "
				"It is not Telegram verification and is visible only in "
				"FlashGram.",
				"Это бейдж FlashGram, выданный на сервере FlashGram. Это не "
				"верификация Telegram, и он виден только в FlashGram."),
			st::boxDividerLabel),
		st::flashgramSecondaryPadding);
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

class ChipsFlow final : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	void add(not_null<Ui::AbstractButton*> button, int width) {
		_chips.push_back({ button, width });
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto height = st::flashgramIdentityChipHeight;
		const auto skip = st::flashgramIdentityChipSkip;
		auto left = 0;
		auto top = 0;
		for (const auto &[chipButton, chipWidth] : _chips) {
			const auto chip = std::min(chipWidth, newWidth);
			if (left > 0 && left + chip > newWidth) {
				left = 0;
				top += height + skip;
			}
			chipButton->setGeometry(left, top, chip, height);
			left += chip + skip;
		}
		return _chips.empty() ? 0 : (top + height);
	}

private:
	std::vector<std::pair<not_null<Ui::AbstractButton*>, int>> _chips;

};

void OpenUsername(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user,
		const QString &username) {
	const auto show = controller->uiShow();
	if (!user->isUsernameEditable(username)) {
		controller->resolveCollectible(
			user->id,
			username,
			crl::guard(controller, [=](QString) {
				show->showToast(Tr(
					"Telegram didn't return collectible details for this "
					"username.",
					"Telegram не вернул данные о коллекционном username."));
			}));
		return;
	}
	QGuiApplication::clipboard()->setText(
		kTelegramLinkPrefix.utf16() + username);
	show->showToast(Tr("Link copied.", "Ссылка скопирована."));
}

void FillUsernames(
		not_null<Ui::VerticalLayout*> card,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	card->clear();
	const auto usernames = user->usernames();
	if (usernames.empty()) {
		return;
	}
	const auto padding = st::flashgramIdentityCardPadding;
	const auto header = card->add(
		object_ptr<Ui::RpWidget>(card),
		QMargins(padding.left(), padding.top(), padding.right(), 0));
	header->resize(header->width(), st::flashgramIdentitySourceHeight);
	header->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(header);
		const auto r = header->rect();
		DrawText(
			p,
			st::flashgramIdentityCaptionFont,
			T().sub,
			r,
			Tr("USERNAMES", "ЮЗЕРНЕЙМЫ"));
		PaintSourceChip(p, r, Tr("Telegram data", "Данные Telegram"), T().sub);
	}, header->lifetime());

	const auto link = [&](const QString &username) {
		return tr::link(
			u"@"_q + username,
			kUsernameLinkPrefix.utf16() + username);
	};
	const auto filter = [=](const ClickHandlerPtr &handler, Qt::MouseButton) {
		const auto url = handler ? handler->url() : QString();
		if (url.startsWith(kUsernameLinkPrefix.utf16())) {
			OpenUsername(
				controller,
				user,
				url.mid(kUsernameLinkPrefix.utf16().size()));
		}
		return false;
	};
	const auto main = card->add(
		object_ptr<Ui::FlatLabel>(
			card,
			rpl::single(link(usernames.front())),
			st::flashgramIdentityMainLabel),
		QMargins(
			padding.left(),
			st::flashgramIdentityLabelSkip,
			padding.right(),
			0));
	main->setClickHandlerFilter(filter);

	if (usernames.size() > 1) {
		auto aliases = tr::marked(Tr("also ", "также "));
		for (auto i = 1; i != int(usernames.size()); ++i) {
			if (i > 1) {
				aliases.append(u", "_q);
			}
			aliases.append(link(usernames[i]));
		}
		const auto label = card->add(
			object_ptr<Ui::FlatLabel>(
				card,
				rpl::single(aliases),
				st::flashgramIdentityAliasLabel),
			QMargins(
				padding.left(),
				st::flashgramIdentityLabelSkip,
				padding.right(),
				0));
		label->setClickHandlerFilter(filter);
	}

	auto collectible = std::vector<QString>();
	for (const auto &username : usernames) {
		if (!user->isUsernameEditable(username)) {
			collectible.push_back(username);
		}
	}
	if (!collectible.empty()) {
		const auto flow = card->add(
			object_ptr<ChipsFlow>(card),
			QMargins(
				padding.left(),
				st::flashgramIdentitySkip,
				padding.right(),
				0));
		const auto &font = st::flashgramIdentityChipFont;
		const auto gem = st::flashgramIdentityDiamondSize;
		const auto chipPadding = st::flashgramIdentityChipPadding;
		const auto nft = u"NFT"_q;
		for (const auto &username : collectible) {
			const auto text = u"@"_q + username;
			const auto width = chipPadding
				+ gem
				+ st::flashgramIdentityChipSkip
				+ font->width(text)
				+ st::flashgramIdentityChipSkip
				+ st::flashgramIdentitySourceFont->width(nft)
				+ chipPadding;
			flow->add(CreateButton(flow, [=](QPainter &p, QRect r, bool over) {
				auto fill = QLinearGradient(r.topLeft(), r.topRight());
				fill.setColorAt(0., QColor(0x4F, 0x8B, 0xFF, over ? 70 : 44));
				fill.setColorAt(1., QColor(0x9B, 0x6B, 0xFF, over ? 70 : 44));
				FillRounded(p, QRectF(r), r.height() / 2., fill);
				auto left = r.x() + chipPadding;
				PaintGem(
					p,
					QRectF(left, r.y() + (r.height() - gem) / 2., gem, gem),
					QColor(0x8E, 0xE3, 0xFF),
					QColor(0x4F, 0x6B, 0xFF));
				left += gem + st::flashgramIdentityChipSkip;
				const auto textWidth = font->width(text);
				DrawText(
					p,
					font,
					T().text,
					QRect(left, r.y(), textWidth + 1, r.height()),
					text);
				left += textWidth + st::flashgramIdentityChipSkip;
				DrawText(
					p,
					st::flashgramIdentitySourceFont,
					T().accent,
					QRect(left, r.y(), r.right() - left, r.height()),
					nft);
			}, [=] {
				OpenUsername(controller, user, username);
			}), width);
		}
	}

	card->add(
		object_ptr<Ui::FlatLabel>(
			card,
			collectible.empty()
				? Tr(
					"Tap a username to copy its link.",
					"Нажмите на username, чтобы скопировать ссылку.")
				: Tr(
					"Collectible details — owner, purchase date and price — "
					"come from Telegram and Fragment.",
					"Данные коллекционных username — владелец, дата покупки "
					"и цена — приходят от Telegram и Fragment."),
			st::boxDividerLabel),
		QMargins(
			padding.left(),
			st::flashgramIdentitySkip,
			padding.right(),
			padding.bottom()));
}

void SetupPhoneOption(
		not_null<Ui::GenericBox*> box,
		not_null<rpl::variable<PhoneDisplay>*> current,
		not_null<UserData*> user,
		PhoneDisplay mode,
		QString title,
		QString preview,
		bool flashgram) {
	const auto height = st::flashgramPhoneOptionHeight;
	const auto margin = st::flashgramPhoneOptionMargin;
	const auto block = box->addRow(object_ptr<Block>(
		box,
		QColor(),
		[=](int) { return margin.top() + height + margin.bottom(); },
		nullptr));
	const auto button = CreateButton(block, [=](QPainter &p, QRect r, bool over) {
		const auto active = (current->current() == mode);
		const auto colors = T();
		FillRounded(
			p,
			QRectF(r),
			st::flashgramIdentityCardRadius,
			over ? colors.cardOver : colors.card);
		auto hq = PainterHighQualityEnabler(p);
		if (active) {
			auto border = colors.accent;
			border.setAlpha(170);
			p.setPen(QPen(border, 1.5));
			p.setBrush(Qt::NoBrush);
			const auto radius = float64(st::flashgramIdentityCardRadius);
			p.drawRoundedRect(
				QRectF(r).marginsRemoved({ 0.75, 0.75, 0.75, 0.75 }),
				radius,
				radius);
		}
		const auto padding = st::flashgramIdentityCardPadding;
		const auto radio = st::flashgramPhoneOptionRadio;
		const auto circle = QRectF(
			r.x() + padding.left(),
			r.y() + (r.height() - radio) / 2.,
			radio,
			radio);
		p.setPen(QPen(active ? colors.accent : colors.sub, radio / 10.));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(circle.marginsRemoved({ 1., 1., 1., 1. }));
		if (active) {
			p.setPen(Qt::NoPen);
			p.setBrush(colors.accent);
			p.drawEllipse(circle.center(), radio / 4., radio / 4.);
		}
		const auto left = int(circle.right()) + st::flashgramIdentitySkip;
		const auto &titleFont = st::flashgramIdentityTitleFont;
		const auto &textFont = st::flashgramIdentityTextFont;
		const auto top = r.y() + (r.height() - titleFont->height - textFont->height) / 2;
		const auto width = r.right() - padding.right() - left;
		const auto chipWidth = flashgram
			? (st::flashgramIdentitySourceFont->width(u"FlashGram"_q)
				+ 2 * st::flashgramIdentitySourcePadding
				+ st::flashgramIdentityChipSkip)
			: 0;
		DrawText(
			p,
			titleFont,
			colors.text,
			QRect(left, top, width - chipWidth, titleFont->height),
			titleFont->elided(title, width - chipWidth));
		DrawText(
			p,
			textFont,
			colors.sub,
			QRect(left, top + titleFont->height, width, textFont->height),
			textFont->elided(preview, width));
		if (flashgram) {
			PaintSourceChip(
				p,
				QRect(left, top, width, titleFont->height),
				u"FlashGram"_q,
				colors.accent);
		}
	}, [=] {
		*current = mode;
		SavePhoneDisplay(user, mode);
	});
	block->setLayoutCallback([=](int width, int blockHeight) {
		button->setGeometry(QRect(0, 0, width, blockHeight).marginsRemoved(margin));
	});
	current->changes() | rpl::on_next([=] {
		button->update();
	}, button->lifetime());
}

[[nodiscard]] std::pair<QColor, QColor> RarityColors(const QString &rarity) {
	const auto lower = rarity.toLower();
	if (lower.contains(u"legend"_q)) {
		return { QColor(0xF7, 0xC2, 0x4A), QColor(0xE0, 0x76, 0x2F) };
	} else if (lower.contains(u"epic"_q)) {
		return { QColor(0xB7, 0x7B, 0xFF), QColor(0x6A, 0x43, 0xE8) };
	} else if (lower.contains(u"rare"_q)) {
		return { QColor(0x4F, 0xB2, 0xFF), QColor(0x25, 0x63, 0xEB) };
	} else if (lower.contains(u"uncommon"_q)) {
		return { QColor(0x4E, 0xD3, 0x9A), QColor(0x1F, 0x9D, 0x74) };
	}
	return { QColor(0x8A, 0x93, 0xA6), QColor(0x4B, 0x52, 0x63) };
}

void PaintStoreButton(
		QPainter &p,
		const QRect &r,
		bool over,
		bool enabled,
		const QString &text) {
	if (enabled) {
		auto fill = QLinearGradient(r.topLeft(), r.topRight());
		fill.setColorAt(0., over ? QColor(0x5A, 0xA0, 0xFF) : C().accent);
		fill.setColorAt(1., over ? QColor(0x8A, 0x6C, 0xFF) : QColor(0x6B, 0x5C, 0xFF));
		FillRounded(p, QRectF(r), r.height() / 2., fill);
	} else {
		FillRounded(p, QRectF(r), r.height() / 2., C().button);
	}
	DrawText(
		p,
		st::flashgramInventoryButtonFont,
		enabled ? C().text : C().sub,
		r,
		text,
		int(Qt::AlignCenter));
}

} // namespace

void AddUsernamesCard(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container),
			st::flashgramIdentityCardMargin));
	const auto card = wrap->entity();
	card->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(card);
		FillRounded(
			p,
			QRectF(card->rect()),
			st::flashgramIdentityCardRadius,
			T().card);
	}, card->lifetime());
	user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::Username | Data::PeerUpdate::Flag::Usernames
	) | rpl::on_next([=] {
		FillUsernames(card, controller, user);
		card->resizeToWidth(card->width());
		wrap->toggle(!user->usernames().empty(), anim::type::instant);
	}, card->lifetime());
}

void AddOrgVerificationCard(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	wrap->hide(anim::type::instant);
	const auto list = wrap->entity();
	const auto fill = [=](std::vector<Server::VerificationBadge> badges) {
		list->clear();
		badges.erase(ranges::remove_if(badges, [](const auto &badge) {
			return !IsOrgType(badge.type);
		}), end(badges));
		for (const auto &badge : badges) {
			const auto block = list->add(object_ptr<Block>(
				list,
				QColor(),
				[=](int width) { return OrgCardHeight(badge, width); },
				nullptr));
			const auto button = CreateButton(block, [=](
					QPainter &p,
					QRect r,
					bool over) {
				const auto colors = T();
				FillRounded(
					p,
					QRectF(r),
					st::flashgramIdentityCardRadius,
					over ? colors.cardOver : colors.card);
				auto hq = PainterHighQualityEnabler(p);
				auto border = OrgColors(badge.type).first;
				border.setAlpha(over ? 150 : 90);
				p.setPen(QPen(border, 1.2));
				p.setBrush(Qt::NoBrush);
				const auto radius = float64(st::flashgramIdentityCardRadius);
				p.drawRoundedRect(
					QRectF(r).marginsRemoved({ 0.6, 0.6, 0.6, 0.6 }),
					radius,
					radius);

				const auto padding = st::flashgramIdentityCardPadding;
				const auto emblem = st::flashgramIdentityEmblemSize;
				PaintOrgEmblem(
					p,
					QRect(r.x() + padding.left(), r.y() + padding.top(), emblem, emblem),
					badge.type);
				const auto left = r.x() + padding.left() + emblem + st::flashgramIdentitySkip;
				const auto width = OrgTextWidth(r.width());
				const auto lineSkip = st::flashgramIdentityLineSkip;
				const auto &titleFont = st::flashgramIdentityTitleFont;
				const auto &textFont = st::flashgramIdentityTextFont;
				const auto &metaFont = st::flashgramIdentityMetaFont;
				auto top = r.y() + padding.top();
				const auto chipText = Tr("FlashGram badge", "Бейдж FlashGram");
				const auto chipWidth = st::flashgramIdentitySourceFont->width(chipText)
					+ 2 * st::flashgramIdentitySourcePadding
					+ st::flashgramIdentityChipSkip;
				DrawText(
					p,
					titleFont,
					colors.text,
					QRect(left, top, width - chipWidth, titleFont->height),
					titleFont->elided(OrgTitle(badge), width - chipWidth));
				PaintSourceChip(
					p,
					QRect(left, top, width, titleFont->height),
					chipText,
					colors.accent);
				top += titleFont->height + lineSkip;
				const auto statement = OrgStatement(badge);
				const auto statementHeight = WrappedHeight(textFont, statement, width);
				DrawText(
					p,
					textFont,
					colors.text,
					QRect(left, top, width, statementHeight),
					statement,
					int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
				top += statementHeight + lineSkip;
				if (const auto extra = OrgExtra(badge); !extra.isEmpty()) {
					const auto extraHeight = WrappedHeight(textFont, extra, width);
					DrawText(
						p,
						textFont,
						colors.sub,
						QRect(left, top, width, extraHeight),
						extra,
						int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
					top += extraHeight + lineSkip;
				}
				const auto meta = OrgMeta(badge);
				DrawText(
					p,
					metaFont,
					colors.sub,
					QRect(left, top, width, WrappedHeight(metaFont, meta, width)),
					meta,
					int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
			}, [=] {
				controller->show(Box(OrgInfoBox, badge));
			});
			block->setLayoutCallback([=](int width, int height) {
				button->setGeometry(QRect(0, 0, width, height).marginsRemoved(
					st::flashgramIdentityCardMargin));
			});
		}
		list->resizeToWidth(container->width());
		wrap->toggle(!badges.empty(), anim::type::instant);
	};

	const auto telegramUserId = peerToUser(user->id).bare;
	if (user->isSelf()) {
		Server::RequestVerification(telegramUserId);
		rpl::single(
			rpl::empty
		) | rpl::then(
			Server::VerificationUpdates()
		) | rpl::on_next([=] {
			const auto verification = Server::CachedVerification(telegramUserId);
			fill(verification
				? verification->badges
				: std::vector<Server::VerificationBadge>());
		}, list->lifetime());
	} else {
		Server::RequestOrgVerifications(
			telegramUserId,
			crl::guard(list, fill));
	}
}

void PhoneDisplayBox(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> user) {
	box->setTitle(TrValue("Phone number in FlashGram", "Номер в FlashGram"));
	box->setWidth(st::boxWideWidth);
	const auto current = box->lifetime().make_state<
		rpl::variable<PhoneDisplay>>(LoadPhoneDisplay(user));
	const auto phone = user->phone();
	const auto flashgramId = LoadProfile(user).flashgramId;
	SetupPhoneOption(
		box,
		current,
		user,
		PhoneDisplay::Real,
		Tr("Show real phone", "Показывать реальный номер"),
		phone.isEmpty()
			? Tr("Hidden by Telegram", "Скрыт в Telegram")
			: Ui::FormatPhone(phone),
		false);
	SetupPhoneOption(
		box,
		current,
		user,
		PhoneDisplay::Masked,
		Tr("Show anonymous phone", "Показывать анонимный номер"),
		phone.isEmpty() ? flashgramId : MaskedPhone(phone),
		true);
	SetupPhoneOption(
		box,
		current,
		user,
		PhoneDisplay::FlashGramId,
		Tr("Show FlashGram ID", "Показывать FlashGram ID"),
		flashgramId,
		true);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			Tr(
				"Only changes what FlashGram shows on this device. Your "
				"Telegram account number stays the same and other people "
				"still see what Telegram privacy settings allow.",
				"Меняет только то, что FlashGram показывает на этом "
				"устройстве. Номер Telegram-аккаунта не меняется, другие "
				"видят то, что разрешают настройки приватности Telegram."),
			st::boxDividerLabel),
		st::flashgramSecondaryPadding);
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void NftStoreBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	SetupScreenBox(box);
	SetupScreenHeader(
		box,
		Tr("NFT Gifts", "NFT-подарки"),
		Tr("Official stores only", "Только официальные магазины"));
	Server::Start();
	const auto layout = box->verticalLayout();

	const auto hero = AddBlock(layout, [](int) {
		return st::flashgramVerifyCardMargin.top()
			+ st::flashgramStoreHeroHeight
			+ st::flashgramVerifyCardMargin.bottom();
	}, [](QPainter &p, QRect r) {
		const auto card = CardRect(r);
		auto fill = QLinearGradient(card.topLeft(), card.bottomRight());
		fill.setColorAt(0., QColor(0x3B, 0x1F, 0x93));
		fill.setColorAt(1., QColor(0x16, 0x6C, 0xE8));
		FillRounded(p, QRectF(card), st::flashgramVerifyCardRadius, fill);
		auto glow = QRadialGradient(
			QPointF(card.right() - card.height() * 0.45, card.y() + card.height() * 0.42),
			card.height() * 0.7);
		glow.setColorAt(0., QColor(0xC8, 0xB8, 0xFF, 110));
		glow.setColorAt(1., QColor(0xC8, 0xB8, 0xFF, 0));
		auto path = QPainterPath();
		path.addRoundedRect(QRectF(card), st::flashgramVerifyCardRadius, st::flashgramVerifyCardRadius);
		p.save();
		p.setClipPath(path);
		p.fillRect(card, glow);
		const auto gem = card.height() * 0.27;
		const auto gemCenter = QPointF(card.right() - card.height() * 0.3, card.y() + card.height() * 0.3);
		PaintGem(
			p,
			QRectF(gemCenter.x() - gem / 2., gemCenter.y() - gem / 2., gem, gem),
			QColor(0xB8, 0xF0, 0xFF),
			QColor(0x5B, 0x6C, 0xFF));
		PaintGem(
			p,
			QRectF(gemCenter.x() - gem * 1.05, gemCenter.y() + gem * 0.1, gem * 0.5, gem * 0.5),
			QColor(0xFF, 0xD1, 0xF0),
			QColor(0xB0, 0x5C, 0xFF));
		PaintGem(
			p,
			QRectF(gemCenter.x() + gem * 0.55, gemCenter.y() + gem * 0.2, gem * 0.42, gem * 0.42),
			QColor(0xFF, 0xE8, 0x9E),
			QColor(0xF0, 0x8A, 0x3C));
		PaintSparkle(p, QPointF(gemCenter.x() + gem * 0.7, gemCenter.y() - gem * 0.55), gem * 0.12, QColor(255, 255, 255, 200));
		p.restore();

		const auto padding = st::flashgramVerifyCardPadding;
		const auto left = card.x() + padding.left();
		auto top = card.y() + padding.top();
		const auto &chipFont = st::flashgramIdentitySourceFont;
		const auto chipText = Tr("OFFICIAL · TELEGRAM", "ОФИЦИАЛЬНО · TELEGRAM");
		const auto chipHeight = st::flashgramIdentitySourceHeight;
		const auto chip = QRect(
			left,
			top,
			chipFont->width(chipText) + 2 * st::flashgramIdentitySourcePadding,
			chipHeight);
		FillRounded(p, QRectF(chip), chipHeight / 2., QColor(255, 255, 255, 40));
		DrawText(p, chipFont, C().text, chip, chipText, int(Qt::AlignCenter));
		top += chipHeight + st::flashgramVerifyCaptionSkip;
		const auto textWidth = card.width()
			- padding.left()
			- int(card.height() * 0.55);
		const auto &titleFont = st::flashgramStoreHeroTitleFont;
		DrawText(
			p,
			titleFont,
			C().text,
			QRect(left, top, textWidth, titleFont->height),
			titleFont->elided(
				Tr("Telegram Gifts", "Подарки Telegram"),
				textWidth));
		top += titleFont->height;
		const auto &subFont = st::flashgramVerifyTextFont;
		const auto sub = Tr(
			"Gifts and collectibles sold by Telegram itself.",
			"Подарки и коллекционные NFT от самого Telegram.");
		DrawText(
			p,
			subFont,
			QColor(255, 255, 255, 200),
			QRect(left, top, textWidth, WrappedHeight(subFont, sub, textWidth)),
			sub,
			int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
	});
	const auto official = CreateButton(hero, [](QPainter &p, QRect r, bool over) {
		FillRounded(p, QRectF(r), r.height() / 2., over ? QColor(0xF2, 0xF3, 0xFF) : QColor(255, 255, 255));
		DrawText(
			p,
			st::flashgramVerifyNameFont,
			QColor(0x1D, 0x1F, 0x3A),
			r,
			Tr("Open Official Store", "Открыть официальный магазин"),
			int(Qt::AlignCenter));
	}, [=] {
		Ui::ShowStarGiftBox(controller, controller->session().user());
	});
	hero->setLayoutCallback([=](int width, int height) {
		const auto card = CardRect(QRect(0, 0, width, height));
		const auto padding = st::flashgramVerifyCardPadding;
		official->setGeometry(
			card.x() + padding.left(),
			card.bottom() + 1 - padding.bottom() - st::flashgramStoreHeroButtonHeight,
			card.width() - padding.left() - padding.right(),
			st::flashgramStoreHeroButtonHeight);
	});

	const auto fragment = AddBlock(layout, [](int) {
		return st::flashgramVerifyCardMargin.top()
			+ st::flashgramStoreSmallHeroHeight
			+ st::flashgramVerifyCardMargin.bottom();
	}, [](QPainter &p, QRect r) {
		const auto card = CardRect(r);
		PaintCard(p, card, C().card);
		const auto padding = st::flashgramVerifyCardPadding;
		const auto left = card.x() + padding.left();
		const auto &titleFont = st::flashgramVerifySectionFont;
		const auto titleRow = QRect(
			left,
			card.y() + padding.top(),
			card.width() - padding.left() - padding.right(),
			titleFont->height);
		DrawText(p, titleFont, C().text, titleRow, u"Fragment"_q);
		PaintSourceChip(p, titleRow, Tr("OFFICIAL · FRAGMENT", "ОФИЦИАЛЬНО · FRAGMENT"), C().sub);
		DrawText(
			p,
			st::flashgramVerifyTextFont,
			C().sub,
			QRect(left, titleRow.bottom() + 1, titleRow.width(), st::flashgramVerifyTextFont->height),
			Tr(
				"Collectible usernames, numbers and gifts.",
				"Коллекционные username, номера и подарки."));
	});
	const auto fragmentButton = CreateButton(fragment, [](QPainter &p, QRect r, bool over) {
		PaintStoreButton(p, r, over, true, Tr("Open on Fragment", "Открыть на Fragment"));
	}, [] {
		UrlClickHandler::Open(kFragmentUrl.utf16());
	});
	fragment->setLayoutCallback([=](int width, int height) {
		const auto card = CardRect(QRect(0, 0, width, height));
		const auto padding = st::flashgramVerifyCardPadding;
		fragmentButton->setGeometry(
			card.x() + padding.left(),
			card.bottom() + 1 - padding.bottom() - st::flashgramStoreButtonHeight,
			card.width() - padding.left() - padding.right(),
			st::flashgramStoreButtonHeight);
	});

	const auto curatedTitle = Tr("Curated by FlashGram", "Подборка FlashGram");
	const auto curatedText = Tr(
		"Links to real listings in official stores. Prices are confirmed "
		"in the store before you pay.",
		"Ссылки на реальные лоты в официальных магазинах. Цена "
		"подтверждается в магазине перед оплатой.");
	AddBlock(layout, [=](int width) {
		const auto padding = st::flashgramVerifyTitlePadding;
		return padding.top()
			+ st::flashgramVerifySectionFont->height
			+ st::flashgramVerifyPanelTextSkip
			+ WrappedHeight(
				st::flashgramVerifyTextFont,
				curatedText,
				width - padding.left() - padding.right())
			+ padding.bottom();
	}, [=](QPainter &p, QRect r) {
		const auto padding = st::flashgramVerifyTitlePadding;
		const auto width = r.width() - padding.left() - padding.right();
		DrawText(
			p,
			st::flashgramVerifySectionFont,
			C().text,
			QRect(padding.left(), padding.top(), width, st::flashgramVerifySectionFont->height),
			curatedTitle);
		const auto top = padding.top()
			+ st::flashgramVerifySectionFont->height
			+ st::flashgramVerifyPanelTextSkip;
		DrawText(
			p,
			st::flashgramVerifyTextFont,
			C().sub,
			QRect(padding.left(), top, width, WrappedHeight(st::flashgramVerifyTextFont, curatedText, width)),
			curatedText,
			int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
	});

	const auto grid = layout->add(object_ptr<Ui::VerticalLayout>(layout));
	const auto fillGrid = [=] {
		grid->clear();
		const auto entries = Server::NftStoreEntries();
		if (entries.empty()) {
			const auto empty = AddBlock(grid, [](int) {
				return st::flashgramVerifyCardMargin.top()
					+ st::flashgramStoreSmallHeroHeight
					+ st::flashgramVerifyCardMargin.bottom();
			}, [](QPainter &p, QRect r) {
				const auto card = CardRect(r);
				PaintCard(p, card, C().card);
				const auto padding = st::flashgramVerifyCardPadding;
				DrawText(
					p,
					st::flashgramVerifySectionFont,
					C().text,
					QRect(card.x() + padding.left(), card.y() + padding.top(), card.width(), st::flashgramVerifySectionFont->height),
					Tr("Curated drops", "Подборка лотов"));
				DrawText(
					p,
					st::flashgramVerifyTextFont,
					C().sub,
					QRect(card.x() + padding.left(), card.y() + padding.top() + st::flashgramVerifySectionFont->height, card.width(), st::flashgramVerifyTextFont->height),
					Tr("No official listings added yet.", "Официальные лоты пока не добавлены."));
			});
			const auto soon = CreateButton(empty, [](QPainter &p, QRect r, bool) {
				PaintStoreButton(p, r, false, false, Tr("Coming soon", "Скоро"));
			}, nullptr);
			soon->setAttribute(Qt::WA_TransparentForMouseEvents);
			empty->setLayoutCallback([=](int width, int height) {
				const auto card = CardRect(QRect(0, 0, width, height));
				const auto padding = st::flashgramVerifyCardPadding;
				soon->setGeometry(
					card.x() + padding.left(),
					card.bottom() + 1 - padding.bottom() - st::flashgramStoreButtonHeight,
					card.width() - padding.left() - padding.right(),
					st::flashgramStoreButtonHeight);
			});
			empty->resizeToWidth(grid->width());
			return;
		}
		for (auto i = 0; i < int(entries.size()); i += 2) {
			const auto row = AddBlock(grid, [](int) {
				return st::flashgramStoreCardHeight + st::flashgramStoreGridSkip;
			}, nullptr);
			auto cards = std::vector<not_null<Ui::RpWidget*>>();
			auto buttons = std::vector<not_null<Ui::AbstractButton*>>();
			for (auto j = i; j != std::min(i + 2, int(entries.size())); ++j) {
				const auto entry = entries[j];
				const auto trusted = Server::IsTrustedStoreUrl(entry.url);
				const auto card = Ui::CreateChild<Ui::RpWidget>(row.get());
				card->show();
				card->paintRequest() | rpl::on_next([=] {
					auto p = QPainter(card);
					const auto r = card->rect();
					PaintCard(p, r, C().card);
					const auto padding = st::flashgramInventoryCardPadding;
					const auto preview = QRect(
						padding,
						padding,
						r.width() - 2 * padding,
						st::flashgramStorePreviewHeight);
					const auto colors = RarityColors(entry.rarity);
					auto glow = QRadialGradient(QPointF(preview.center()), preview.width() * 0.7);
					glow.setColorAt(0., colors.first);
					glow.setColorAt(1., colors.second.darker(160));
					FillRounded(p, QRectF(preview), st::flashgramInventoryCardRadius * 0.7, glow);
					const auto gem = preview.height() * 0.46;
					PaintGem(
						p,
						QRectF(preview.center().x() - gem / 2., preview.center().y() - gem / 2., gem, gem),
						QColor(255, 255, 255, 235),
						colors.first);
					if (!entry.rarity.isEmpty()) {
						const auto &font = st::flashgramIdentitySourceFont;
						const auto height = st::flashgramStoreRarityHeight;
						const auto chip = QRect(
							preview.x() + padding / 2 + 2,
							preview.y() + padding / 2 + 2,
							font->width(entry.rarity) + 2 * st::flashgramIdentitySourcePadding,
							height);
						FillRounded(p, QRectF(chip), height / 2., QColor(0, 0, 0, 90));
						DrawText(p, font, C().text, chip, entry.rarity, int(Qt::AlignCenter));
					}
					auto top = preview.bottom() + 1 + padding;
					const auto &nameFont = st::flashgramInventoryNameFont;
					const auto width = r.width() - 2 * padding;
					DrawText(
						p,
						nameFont,
						C().text,
						QRect(padding, top, width, nameFont->height),
						nameFont->elided(entry.title, width));
					top += nameFont->height;
					const auto &subFont = st::flashgramInventorySubFont;
					DrawText(
						p,
						subFont,
						entry.priceText.isEmpty() ? C().sub : C().lavender,
						QRect(padding, top, width, subFont->height),
						subFont->elided(
							entry.priceText.isEmpty()
								? Tr("Price in official store", "Цена в официальном магазине")
								: entry.priceText,
							width));
				}, card->lifetime());
				const auto buy = CreateButton(card, [=](QPainter &p, QRect r, bool over) {
					PaintStoreButton(
						p,
						r,
						over && trusted,
						trusted,
						trusted
							? Tr("Buy via Official Store", "Купить в магазине")
							: Tr("Coming soon", "Скоро"));
				}, [=] {
					if (trusted) {
						UrlClickHandler::Open(entry.url);
					}
				});
				if (!trusted) {
					buy->setAttribute(Qt::WA_TransparentForMouseEvents);
				}
				cards.push_back(card);
				buttons.push_back(buy);
			}
			row->setLayoutCallback([=](int width, int) {
				const auto margin = st::flashgramVerifyCardMargin;
				const auto skip = st::flashgramStoreGridSkip;
				const auto cardWidth = (width - margin.left() - margin.right() - skip) / 2;
				const auto padding = st::flashgramInventoryCardPadding;
				for (auto k = 0; k != int(cards.size()); ++k) {
					cards[k]->setGeometry(
						margin.left() + k * (cardWidth + skip),
						0,
						cardWidth,
						st::flashgramStoreCardHeight);
					buttons[k]->setGeometry(
						padding,
						st::flashgramStoreCardHeight - padding - st::flashgramStoreButtonHeight,
						cardWidth - 2 * padding,
						st::flashgramStoreButtonHeight);
				}
			});
			row->resizeToWidth(grid->width());
		}
	};
	fillGrid();
	Server::ConfigUpdates(
	) | rpl::on_next([=] {
		fillGrid();
	}, grid->lifetime());

	const auto note = Tr(
		"FlashGram doesn't sell gifts and doesn't process payments. Every "
		"purchase happens in the official Telegram or Fragment store.",
		"FlashGram не продаёт подарки и не принимает платежи. Любая "
		"покупка происходит в официальном магазине Telegram или Fragment.");
	AddBlock(layout, [=](int width) {
		const auto padding = st::flashgramVerifyNotePadding;
		return padding.top()
			+ WrappedHeight(st::flashgramVerifyNoteFont, note, width - padding.left() - padding.right())
			+ padding.bottom();
	}, [=](QPainter &p, QRect r) {
		DrawText(
			p,
			st::flashgramVerifyNoteFont,
			C().sub,
			r.marginsRemoved(st::flashgramVerifyNotePadding),
			note,
			int(Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap));
	});
}

void OrgVerificationGrantBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto userId = peerToUser(controller->session().user()->id).bare;
	box->setTitle(TrValue("Major / Hold badges", "Бейджи Major / Hold"));
	box->setWidth(st::boxWideWidth);

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		TrValue("FlashGram ID", "FlashGram ID"),
		QString()));
	field->setMaxLength(16);

	Ui::AddSubsectionTitle(box->verticalLayout(), TrValue("Organization", "Организация"));
	const auto type = std::make_shared<Ui::RadiobuttonGroup>(0);
	box->addRow(object_ptr<Ui::Radiobutton>(box, type, 0, u"Major"_q, st::defaultBoxCheckbox));
	box->addRow(object_ptr<Ui::Radiobutton>(box, type, 1, u"Hold"_q, st::defaultBoxCheckbox));

	Ui::AddSubsectionTitle(box->verticalLayout(), TrValue("Valid for", "Срок действия"));
	const auto days = std::make_shared<Ui::RadiobuttonGroup>(0);
	const auto options = std::array{
		std::pair{ 0, Tr("No expiry", "Бессрочно") },
		std::pair{ 30, Tr("30 days", "30 дней") },
		std::pair{ 90, Tr("90 days", "90 дней") },
		std::pair{ 365, Tr("1 year", "1 год") },
	};
	for (const auto &[value, label] : options) {
		box->addRow(object_ptr<Ui::Radiobutton>(box, days, value, label, st::defaultBoxCheckbox));
	}

	const auto description = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		TrValue("Custom description (optional)", "Своё описание (необязательно)"),
		QString()));
	description->setMaxLength(300);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			Tr(
				"Major and Hold are FlashGram badges. They are granted on the "
				"FlashGram server, shown only in FlashGram and are not "
				"Telegram verification. Check who owns the FlashGram ID "
				"before granting.",
				"Major и Hold — бейджи FlashGram. Они выдаются на сервере "
				"FlashGram, видны только в FlashGram и не являются "
				"верификацией Telegram. Перед выдачей проверьте, кому "
				"принадлежит FlashGram ID."),
			st::boxDividerLabel),
		st::flashgramSecondaryPadding);
	box->setFocusCallback([=] {
		field->setFocusFast();
	});

	const auto sending = box->lifetime().make_state<bool>(false);
	const auto typeKey = [=] {
		return (type->current() == 1) ? u"hold"_q : u"major"_q;
	};
	const auto flashgramId = [=]() -> std::optional<QString> {
		const auto value = field->getLastText().trimmed().toUpper();
		if (value.isEmpty()) {
			field->showError();
			return std::nullopt;
		}
		return value;
	};
	const auto finish = [=](const QString &error, const QString &success) {
		*sending = false;
		controller->uiShow()->showToast(error.isEmpty()
			? success
			: ServerErrorText(error));
	};
	box->addButton(TrValue("Grant", "Выдать"), [=] {
		const auto id = flashgramId();
		if (!id || *sending) {
			return;
		}
		*sending = true;
		Server::GrantOrgVerification(
			userId,
			*id,
			typeKey(),
			description->getLastText().trimmed(),
			days->current(),
			crl::guard(box, [=](QString error) {
				finish(error, Tr("Badge granted.", "Бейдж выдан."));
			}));
	});
	box->addButton(TrValue("Revoke", "Отозвать"), [=] {
		const auto id = flashgramId();
		if (!id || *sending) {
			return;
		}
		*sending = true;
		Server::RevokeOrgVerification(
			userId,
			*id,
			typeKey(),
			crl::guard(box, [=](QString error) {
				finish(error, Tr("Badge revoked.", "Бейдж отозван."));
			}));
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace FlashGram
