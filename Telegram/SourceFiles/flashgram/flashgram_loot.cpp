/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_loot.h"

#include "base/call_delayed.h"
#include "base/random.h"
#include "base/unique_qptr.h"
#include "data/data_user.h"
#include "flashgram/flashgram_gift_view.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QPainterPath>

namespace FlashGram {
namespace {

constexpr auto kSpinCostCents = int64(2500);
constexpr auto kRouletteItems = 40;
constexpr auto kRouletteWinnerIndex = 32;
constexpr auto kSpinDuration = crl::time(5200);
constexpr auto kRevealDuration = crl::time(520);
constexpr auto kRevealDelay = crl::time(350);
constexpr auto kPreviewCount = 3;
constexpr auto kParticlesCount = 40;
constexpr auto kRaysCount = 18;

[[nodiscard]] rpl::producer<QString> BalanceValue(not_null<UserData*> user) {
	return rpl::single(rpl::empty) | rpl::then(Changes()) | rpl::map([=] {
		return FormatBalance(LoadProfile(user).balance);
	});
}

[[nodiscard]] QString BalanceNumber(BalanceAmount amount) {
	auto result = FormatBalance(amount);
	result.chop(3);
	return result;
}

[[nodiscard]] QStringList CollectibleIds() {
	auto result = QStringList();
	for (const auto &gift : GiftsCatalog()) {
		if (gift.kind == GiftKind::Collectible) {
			result.push_back(gift.id);
		}
	}
	return result;
}

void FillRounded(
		QPainter &p,
		const QRectF &rect,
		float64 radius,
		const QBrush &brush) {
	auto hq = PainterHighQualityEnabler(p);
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	p.fillPath(path, brush);
}

void PaintSparkle(QPainter &p, QPointF c, float64 r, const QColor &color) {
	auto path = QPainterPath();
	path.moveTo(c.x(), c.y() - r);
	path.quadTo(c, QPointF(c.x() + r, c.y()));
	path.quadTo(c, QPointF(c.x(), c.y() + r));
	path.quadTo(c, QPointF(c.x() - r, c.y()));
	path.quadTo(c, QPointF(c.x(), c.y() - r));
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(path);
}

void PaintDiamond(QPainter &p, const QRectF &rect, const QColor &color) {
	auto hq = PainterHighQualityEnabler(p);
	const auto w = rect.width();
	const auto h = rect.height();
	auto path = QPainterPath();
	path.moveTo(rect.x() + w * 0.25, rect.y() + h * 0.08);
	path.lineTo(rect.x() + w * 0.75, rect.y() + h * 0.08);
	path.lineTo(rect.x() + w, rect.y() + h * 0.38);
	path.lineTo(rect.x() + w * 0.5, rect.y() + h * 0.95);
	path.lineTo(rect.x(), rect.y() + h * 0.38);
	path.closeSubpath();
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(path);
	auto inner = color.darker(300);
	inner.setAlpha(210);
	PaintSparkle(
		p,
		QPointF(rect.x() + w * 0.5, rect.y() + h * 0.42),
		w * 0.18,
		inner);
}

void PaintBackArrow(QPainter &p, QPointF left, float64 size, QColor color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(color, st::flashgramCapsuleStroke, Qt::SolidLine, Qt::RoundCap));
	const auto tip = left;
	const auto end = QPointF(left.x() + size, left.y());
	p.drawLine(tip, end);
	p.drawLine(tip, QPointF(tip.x() + size * 0.45, tip.y() - size * 0.45));
	p.drawLine(tip, QPointF(tip.x() + size * 0.45, tip.y() + size * 0.45));
}

void PaintChevronDown(QPainter &p, QPointF center, float64 size, QColor color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(color, st::flashgramCapsuleStroke, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(
		QPointF(center.x() - size / 2., center.y() - size / 4.),
		QPointF(center.x(), center.y() + size / 4.));
	p.drawLine(
		QPointF(center.x(), center.y() + size / 4.),
		QPointF(center.x() + size / 2., center.y() - size / 4.));
}

void PaintDots(QPainter &p, QPointF center, float64 size, QColor color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto r = size / 9.;
	for (auto i = -1; i != 2; ++i) {
		p.drawEllipse(QPointF(center.x(), center.y() + i * size / 3.), r, r);
	}
}

struct TopBarColors {
	QColor bg;
	QColor bgOver;
	QColor fg;
};

struct TopBar {
	not_null<Ui::AbstractButton*> back;
	not_null<Ui::AbstractButton*> collapse;
	not_null<Ui::AbstractButton*> more;
};

[[nodiscard]] QString BackText() {
	return Tr("Back", "Назад");
}

[[nodiscard]] int BackWidth() {
	return st::flashgramCapsulePadding * 2
		+ st::flashgramCapsuleIcon
		+ st::flashgramCapsulePadding / 2
		+ st::flashgramCapsuleFont->width(BackText());
}

[[nodiscard]] TopBar CreateTopBar(
		not_null<QWidget*> parent,
		TopBarColors colors,
		Fn<void()> back,
		Fn<void()> more) {
	const auto result = TopBar{
		.back = Ui::CreateChild<Ui::AbstractButton>(parent.get()),
		.collapse = Ui::CreateChild<Ui::AbstractButton>(parent.get()),
		.more = Ui::CreateChild<Ui::AbstractButton>(parent.get()),
	};
	const auto backButton = result.back;
	backButton->setClickedCallback(back);
	backButton->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(backButton);
		const auto r = backButton->rect();
		FillRounded(
			p,
			QRectF(r),
			r.height() / 2.,
			backButton->isOver() ? colors.bgOver : colors.bg);
		const auto icon = st::flashgramCapsuleIcon;
		const auto left = st::flashgramCapsulePadding;
		PaintBackArrow(p, QPointF(left, r.height() / 2.), icon, colors.fg);
		p.setPen(colors.fg);
		p.setFont(st::flashgramCapsuleFont->f);
		p.drawText(
			QRect(
				left + icon + left / 2,
				0,
				r.width(),
				r.height()),
			int(Qt::AlignLeft | Qt::AlignVCenter),
			BackText());
	}, backButton->lifetime());

	const auto paintHalf = [=](
			not_null<Ui::AbstractButton*> button,
			bool left) {
		button->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(button);
			const auto r = button->rect();
			const auto radius = r.height() / 2.;
			auto path = QPainterPath();
			path.addRoundedRect(
				QRectF(left ? 0 : -radius, 0, r.width() + radius, r.height()),
				radius,
				radius);
			auto hq = PainterHighQualityEnabler(p);
			p.fillPath(path, button->isOver() ? colors.bgOver : colors.bg);
			const auto center = QPointF(r.width() / 2., r.height() / 2.);
			if (left) {
				PaintChevronDown(p, center, st::flashgramCapsuleIcon, colors.fg);
			} else {
				PaintDots(p, center, st::flashgramCapsuleIcon, colors.fg);
			}
		}, button->lifetime());
	};
	result.collapse->setClickedCallback(back);
	result.more->setClickedCallback(more);
	paintHalf(result.collapse, true);
	paintHalf(result.more, false);
	result.back->show();
	result.collapse->show();
	result.more->show();
	return result;
}

void LayoutTopBar(const TopBar &bar, int width, int top) {
	const auto side = st::flashgramTopBarSide;
	const auto height = st::flashgramCapsuleHeight;
	bar.back->setGeometry(side, top, BackWidth(), height);
	const auto half = st::flashgramCapsuleSideWidth / 2;
	bar.more->setGeometry(width - side - half, top, half, height);
	bar.collapse->setGeometry(width - side - 2 * half, top, half, height);
}

void AddTopBarRow(
		not_null<Ui::VerticalLayout*> layout,
		Fn<void()> back,
		Fn<void()> more) {
	const auto row = layout->add(object_ptr<Ui::RpWidget>(layout));
	row->resize(row->width(), st::flashgramTopBarHeight);
	auto bg = st::windowBgOver->c;
	auto bgOver = st::windowBgRipple->c;
	const auto bar = CreateTopBar(
		row,
		{ .bg = bg, .bgOver = bgOver, .fg = st::windowFg->c },
		std::move(back),
		std::move(more));
	row->widthValue() | rpl::on_next([=](int width) {
		LayoutTopBar(
			bar,
			width,
			(st::flashgramTopBarHeight - st::flashgramCapsuleHeight) / 2);
	}, row->lifetime());
}

[[nodiscard]] not_null<Ui::AbstractButton*> AddHomeCard(
		not_null<Ui::VerticalLayout*> layout,
		int height,
		Fn<void(QPainter&, QRect, bool)> paint) {
	const auto card = layout->add(
		object_ptr<Ui::AbstractButton>(layout),
		st::flashgramHomeCardMargin);
	card->resize(card->width(), height);
	card->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(card);
		paint(p, card->rect(), card->isOver());
	}, card->lifetime());
	return card;
}

void PaintCardGradient(
		QPainter &p,
		QRect rect,
		QColor from,
		QColor to,
		bool over) {
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., from);
	gradient.setColorAt(1., to);
	FillRounded(p, QRectF(rect), st::flashgramHomeRadius, gradient);
	if (over) {
		FillRounded(
			p,
			QRectF(rect),
			st::flashgramHomeRadius,
			QColor(255, 255, 255, 18));
	}
}

void PaintCardTexts(
		QPainter &p,
		QRect rect,
		const QString &title,
		const QString &subtitle,
		const style::font &titleFont,
		bool bottom) {
	const auto padding = st::flashgramHomePadding;
	const auto &subFont = st::flashgramHomeSubtitleFont;
	const auto textHeight = titleFont->height
		+ (subtitle.isEmpty() ? 0 : subFont->height);
	const auto top = bottom
		? (rect.height() - padding.bottom() - textHeight)
		: (rect.height() - textHeight) / 2;
	p.setPen(QColor(255, 255, 255));
	p.setFont(titleFont->f);
	p.drawText(
		QRect(padding.left(), top, rect.width(), titleFont->height),
		int(Qt::AlignLeft | Qt::AlignVCenter),
		title);
	if (!subtitle.isEmpty()) {
		p.setPen(QColor(255, 255, 255, 200));
		p.setFont(subFont->f);
		p.drawText(
			QRect(
				padding.left(),
				top + titleFont->height,
				rect.width(),
				subFont->height),
			int(Qt::AlignLeft | Qt::AlignVCenter),
			subtitle);
	}
	p.setPen(QColor(255, 255, 255, 220));
	p.setFont(st::flashgramHomeArrowFont->f);
	p.drawText(
		QRect(0, top, rect.width() - padding.right(), textHeight),
		int(Qt::AlignRight | Qt::AlignVCenter),
		QString(QChar(0x203A)));
}

void AddCardStickers(
		not_null<QWidget*> card,
		not_null<Main::Session*> session,
		const QStringList &giftIds) {
	auto views = std::vector<not_null<GiftStickerView*>>();
	for (const auto &id : giftIds) {
		if (int(views.size()) >= kPreviewCount) {
			break;
		} else if (const auto gift = FindGift(id)) {
			const auto view = Ui::CreateChild<GiftStickerView>(
				card.get(),
				session,
				*gift);
			view->show();
			views.push_back(view);
		}
	}
	const auto raw = card.get();
	const auto widget = static_cast<Ui::RpWidget*>(raw);
	widget->sizeValue() | rpl::on_next([=](QSize size) {
		const auto preview = st::flashgramHomePreviewSize;
		const auto count = int(views.size());
		if (!count) {
			return;
		}
		const auto padding = st::flashgramHomePadding;
		const auto available = size.width() - padding.left() - padding.right();
		const auto step = (count > 1)
			? (available - preview) / (count - 1)
			: 0;
		for (auto i = 0; i != count; ++i) {
			views[i]->setGeometry(
				(count > 1)
					? (padding.left() + i * step)
					: (size.width() - preview) / 2,
				st::flashgramHomePreviewTop,
				preview,
				preview);
		}
	}, widget->lifetime());
}

class RouletteStrip final : public Ui::RpWidget {
public:
	RouletteStrip(
		QWidget *parent,
		not_null<Main::Session*> session,
		QColor fade,
		QColor marker);

	void fill(const QStringList &pool, const Gift *winner);
	void setOffset(float64 offset);
	[[nodiscard]] float64 winnerOffset() const;
	[[nodiscard]] int itemStep() const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void layoutItems();

	const not_null<Main::Session*> _session;
	std::vector<base::unique_qptr<LocalGiftView>> _items;
	base::unique_qptr<Ui::RpWidget> _overlay;
	float64 _offset = 0.;

};

RouletteStrip::RouletteStrip(
	QWidget *parent,
	not_null<Main::Session*> session,
	QColor fade,
	QColor marker)
: RpWidget(parent)
, _session(session)
, _overlay(base::make_unique_q<Ui::RpWidget>(this)) {
	_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto overlay = _overlay.get();
	_overlay->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(overlay);
		auto hq = PainterHighQualityEnabler(p);
		const auto width = overlay->width();
		const auto height = overlay->height();
		if (fade.isValid()) {
			const auto size = st::flashgramRouletteFade;
			auto transparent = fade;
			transparent.setAlpha(0);
			auto left = QLinearGradient(0, 0, size, 0);
			left.setColorAt(0., fade);
			left.setColorAt(1., transparent);
			p.fillRect(0, 0, size, height, left);
			auto right = QLinearGradient(width - size, 0, width, 0);
			right.setColorAt(0., transparent);
			right.setColorAt(1., fade);
			p.fillRect(width - size, 0, size, height, right);
		}
		const auto size = st::flashgramRouletteMarker;
		const auto center = width / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(marker);
		auto top = QPainterPath();
		top.moveTo(center - size, 0);
		top.lineTo(center + size, 0);
		top.lineTo(center, size * 1.2);
		top.closeSubpath();
		p.drawPath(top);
		auto bottom = QPainterPath();
		bottom.moveTo(center - size, height);
		bottom.lineTo(center + size, height);
		bottom.lineTo(center, height - size * 1.2);
		bottom.closeSubpath();
		p.drawPath(bottom);
	}, _overlay->lifetime());
}

void RouletteStrip::fill(const QStringList &pool, const Gift *winner) {
	_items.clear();
	_items.reserve(kRouletteItems);
	for (auto i = 0; i != kRouletteItems; ++i) {
		const auto gift = (winner && i == kRouletteWinnerIndex)
			? winner
			: RollGift(pool);
		if (!gift) {
			continue;
		}
		auto item = base::make_unique_q<LocalGiftView>(
			this,
			_session,
			*gift,
			gift->number,
			false);
		item->setTransparentForMouse();
		item->show();
		_items.push_back(std::move(item));
	}
	_offset = 0.;
	_overlay->raise();
	layoutItems();
}

void RouletteStrip::setOffset(float64 offset) {
	_offset = offset;
	layoutItems();
}

int RouletteStrip::itemStep() const {
	return st::flashgramRouletteItemWidth + st::flashgramRouletteSkip;
}

float64 RouletteStrip::winnerOffset() const {
	return float64(kRouletteWinnerIndex * itemStep());
}

void RouletteStrip::resizeEvent(QResizeEvent *e) {
	_overlay->setGeometry(rect());
	layoutItems();
}

int RouletteStrip::resizeGetHeight(int newWidth) {
	return st::flashgramRouletteHeight;
}

void RouletteStrip::layoutItems() {
	const auto itemWidth = st::flashgramRouletteItemWidth;
	const auto itemHeight = st::flashgramRouletteItemHeight;
	const auto first = width() / 2. - itemWidth / 2.;
	const auto top = (height() - itemHeight) / 2;
	for (auto i = 0; i != int(_items.size()); ++i) {
		_items[i]->setGeometry(
			int(std::round(first + i * itemStep() - _offset)),
			top,
			itemWidth,
			itemHeight);
	}
}

[[nodiscard]] float64 SpinTarget(not_null<RouletteStrip*> strip) {
	const auto spread = std::max(strip->itemStep() / 2, 1);
	const auto jitter = int(base::RandomValue<uint32>() % uint32(spread))
		- spread / 2;
	return strip->winnerOffset() + jitter;
}

class RouletteScreen final : public Ui::RpWidget {
public:
	RouletteScreen(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> close);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	enum class State {
		Idle,
		Spinning,
		Result,
	};

	void setHero(const Gift &gift, int number);
	void spin();
	void reveal();
	void layoutChildren(int width);
	[[nodiscard]] QRect heroRect(int width) const;

	const not_null<Window::SessionController*> _controller;
	const not_null<UserData*> _user;
	const QStringList _pool;
	const TopBar _bar;
	const not_null<Ui::AbstractButton*> _spin;
	const not_null<Ui::AbstractButton*> _link;
	base::unique_qptr<GiftStickerView> _hero;
	base::unique_qptr<RouletteStrip> _strip;
	Ui::Animations::Simple _spinAnimation;
	Ui::Animations::Simple _revealAnimation;
	State _state = State::Idle;
	QString _heroTitle;
	QString _balance;
	OwnedGift _won;

};

RouletteScreen::RouletteScreen(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> close)
: RpWidget(parent)
, _controller(controller)
, _user(controller->session().user())
, _pool(CollectibleIds())
, _bar(CreateTopBar(
	this,
	{
		.bg = QColor(0x10, 0x2A, 0x1E, 110),
		.bgOver = QColor(0x10, 0x2A, 0x1E, 160),
		.fg = QColor(255, 255, 255),
	},
	close,
	[=] { controller->show(Box(LootBox, controller)); }))
, _spin(Ui::CreateChild<Ui::AbstractButton>(this))
, _link(Ui::CreateChild<Ui::AbstractButton>(this)) {
	RequestGiftStickers(&controller->session());

	BalanceValue(_user) | rpl::on_next([=](const QString &balance) {
		_balance = balance;
		update();
	}, lifetime());

	_spin->setClickedCallback([=] {
		if (_state == State::Result) {
			_state = State::Idle;
			_spin->update();
			_link->update();
			update();
			_controller->show(Box(MyGiftsBox, _controller));
		} else {
			spin();
		}
	});
	_spin->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_spin);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = QRectF(_spin->rect()).marginsRemoved(
			{ 1., 1., 1., 1. });
		const auto curve = r.height() * 0.16;
		const auto radius = r.height() * 0.42;
		auto path = QPainterPath();
		path.moveTo(r.left() + radius, r.top() + curve);
		path.quadTo(
			QPointF(r.center().x(), r.top() - curve),
			QPointF(r.right() - radius, r.top() + curve));
		path.quadTo(
			QPointF(r.right(), r.top() + curve * 1.4),
			QPointF(r.right(), r.center().y() + curve));
		path.quadTo(
			QPointF(r.right(), r.bottom()),
			QPointF(r.right() - radius, r.bottom()));
		path.quadTo(
			QPointF(r.center().x(), r.bottom() - curve * 2.2),
			QPointF(r.left() + radius, r.bottom()));
		path.quadTo(
			QPointF(r.left(), r.bottom()),
			QPointF(r.left(), r.center().y() + curve));
		path.quadTo(
			QPointF(r.left(), r.top() + curve * 1.4),
			QPointF(r.left() + radius, r.top() + curve));
		path.closeSubpath();
		p.setPen(QPen(QColor(255, 255, 255, 80), 1.2));
		p.setBrush(QColor(255, 255, 255, _spin->isOver() ? 70 : 50));
		p.drawPath(path);
		p.setPen(QColor(255, 255, 255, (_state == State::Spinning)
			? 140
			: 235));
		p.setFont(st::flashgramRouletteGlassFont->f);
		p.drawText(
			_spin->rect(),
			Qt::AlignCenter,
			(_state == State::Result)
				? Tr("To collection", "В коллекцию")
				: (_state == State::Spinning)
				? Tr("Spinning...", "Крутится...")
				: Tr("Spin the roulette", "Прокрутить рулетку"));
	}, _spin->lifetime());

	_link->setClickedCallback([=] {
		if (_state == State::Result) {
			_controller->show(
				Box(LocalGiftDetailsBox, _controller, _won, false));
		} else {
			_controller->show(Box(MyGiftsBox, _controller));
		}
	});
	_link->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_link);
		const auto r = _link->rect();
		const auto &font = st::flashgramRouletteLinkFont;
		const auto &icon = st::menuIconGiftPremium;
		const auto text = (_state == State::Result)
			? Tr("Open gift", "Открыть подарок")
			: Tr("My Gifts", "Мои подарки");
		const auto full = icon.width()
			+ st::flashgramRouletteLinkIconSkip
			+ font->width(text);
		const auto left = (r.width() - full) / 2;
		icon.paint(
			p,
			left,
			(r.height() - icon.height()) / 2,
			r.width(),
			QColor(255, 255, 255));
		p.setFont(font->f);
		p.setPen(QColor(255, 255, 255, _link->isOver() ? 255 : 235));
		p.drawText(
			QRect(
				left + icon.width() + st::flashgramRouletteLinkIconSkip,
				0,
				r.width(),
				r.height()),
			int(Qt::AlignLeft | Qt::AlignVCenter),
			text);
	}, _link->lifetime());

	const auto profile = LoadProfile(_user);
	const auto owned = ranges::find_if(profile.gifts, [](
			const OwnedGift &gift) {
		const auto data = FindGift(gift.giftId);
		return data && (data->kind == GiftKind::Collectible);
	});
	if (owned != end(profile.gifts)) {
		setHero(*FindGift(owned->giftId), owned->number);
	} else if (const auto gift = RollGift(_pool)) {
		setHero(*gift, gift->number);
	}
}

int RouletteScreen::resizeGetHeight(int newWidth) {
	layoutChildren(newWidth);
	return st::flashgramRouletteFullHeight;
}

QRect RouletteScreen::heroRect(int width) const {
	const auto progress = (_state == State::Result)
		? _revealAnimation.value(1.)
		: 1.;
	const auto full = st::flashgramRouletteHeroSize;
	const auto size = int(std::round(full * (0.55 + 0.45 * progress)));
	const auto centerY = st::flashgramRouletteHeroTop + full / 2;
	return QRect((width - size) / 2, centerY - size / 2, size, size);
}

void RouletteScreen::layoutChildren(int width) {
	LayoutTopBar(_bar, width, st::flashgramRouletteTop);
	if (_hero) {
		_hero->setGeometry(heroRect(width));
	}
	if (_strip) {
		_strip->setGeometry(
			0,
			st::flashgramRouletteStripY,
			width,
			st::flashgramRouletteHeight);
	}
	const auto glassSide = st::flashgramRouletteGlassSide;
	_spin->setGeometry(
		glassSide,
		st::flashgramRouletteGlassTop,
		width - 2 * glassSide,
		st::flashgramRouletteGlassHeight);
	_link->setGeometry(
		st::flashgramRouletteSide,
		st::flashgramRouletteLinkY,
		width - 2 * st::flashgramRouletteSide,
		st::flashgramRouletteLinkHeight);
}

void RouletteScreen::setHero(const Gift &gift, int number) {
	_hero = base::make_unique_q<GiftStickerView>(
		this,
		&_controller->session(),
		gift);
	_hero->show();
	_heroTitle = GiftTitle(gift, number);
	layoutChildren(width());
	update();
}

void RouletteScreen::spin() {
	if (_state != State::Idle) {
		return;
	}
	const auto winner = RollGift(_pool);
	if (!winner) {
		return;
	} else if (!SpendBalance(_user, { .cents = kSpinCostCents })) {
		_controller->uiShow()->showToast(Tr(
			"Not enough FlashGram Balance.",
			"Недостаточно FlashGram Balance."));
		return;
	}
	_state = State::Spinning;
	_won = OwnedGift{
		.giftId = winner->id,
		.number = RollNumber(*winner),
	};
	if (_hero) {
		_hero->hide();
	}
	if (!_strip) {
		_strip = base::make_unique_q<RouletteStrip>(
			this,
			&_controller->session(),
			QColor(),
			QColor(255, 255, 255));
	}
	_strip->show();
	layoutChildren(width());
	_strip->fill(_pool, winner);
	const auto strip = _strip.get();
	const auto target = SpinTarget(strip);
	_spin->update();
	update();
	_spinAnimation.start([=](float64 value) {
		strip->setOffset(value);
		if (!_spinAnimation.animating() && _state == State::Spinning) {
			_won = AddInventoryGift(_user, _won);
			base::call_delayed(kRevealDelay, this, [=] {
				reveal();
			});
		}
	}, 0., target, kSpinDuration, anim::easeOutCubic);
}

void RouletteScreen::reveal() {
	_state = State::Result;
	if (_strip) {
		_strip->hide();
	}
	if (const auto gift = FindGift(_won.giftId)) {
		setHero(*gift, _won.number);
	}
	_revealAnimation.stop();
	_revealAnimation.start([=] {
		layoutChildren(width());
		update();
	}, 0., 1., kRevealDuration, anim::easeOutBack);
	_spin->update();
	_link->update();
	update();
}

void RouletteScreen::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto w = width();
	const auto h = height();

	auto background = QLinearGradient(0, 0, 0, h);
	background.setColorAt(0., QColor(0x3B, 0x72, 0x58));
	background.setColorAt(0.45, QColor(0x6E, 0xAE, 0x86));
	background.setColorAt(0.75, QColor(0x5C, 0x9C, 0x76));
	background.setColorAt(1., QColor(0x35, 0x6A, 0x50));
	p.fillRect(rect(), background);

	const auto heroSize = st::flashgramRouletteHeroSize;
	const auto center = QPointF(
		w / 2.,
		st::flashgramRouletteHeroTop + heroSize / 2.);
	const auto result = (_state == State::Result);
	const auto progress = result ? _revealAnimation.value(1.) : 0.;

	for (auto i = 0; i != 12; ++i) {
		const auto x = ((i * 67 + 23) % 100) / 100. * w;
		const auto y = (0.18 + ((i * 41 + 7) % 78) / 100.) * h;
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0x2E, 0x5E, 0x46, 60));
		p.save();
		p.translate(x, y);
		p.rotate((i * 37) % 90 - 45);
		p.drawEllipse(QRectF(-7, -4, 14, 8));
		p.drawEllipse(QRectF(-2, -8, 4, 16));
		p.restore();
	}

	if (_state != State::Spinning) {
		p.save();
		p.translate(center);
		auto rays = QColor(0xF6, 0xE3, 0x8A, result ? 34 : 20);
		p.setPen(Qt::NoPen);
		p.setBrush(rays);
		const auto length = heroSize * 0.85;
		for (auto i = 0; i != kRaysCount; ++i) {
			p.rotate(360. / kRaysCount);
			auto ray = QPainterPath();
			ray.moveTo(0, 0);
			ray.lineTo(-length * 0.07, -length);
			ray.lineTo(length * 0.07, -length);
			ray.closeSubpath();
			p.drawPath(ray);
		}
		p.restore();

		auto glow = QRadialGradient(center, heroSize * 0.75);
		glow.setColorAt(0., QColor(0xF4, 0xDC, 0x6C, result
			? int(120 + 80 * progress)
			: 130));
		glow.setColorAt(0.55, QColor(0xF4, 0xDC, 0x6C, 40));
		glow.setColorAt(1., QColor(0xF4, 0xDC, 0x6C, 0));
		p.fillRect(rect(), glow);

		auto shadow = QRadialGradient(
			QPointF(center.x(), center.y() + heroSize * 0.46),
			heroSize * 0.36);
		shadow.setColorAt(0., QColor(0x1E, 0x40, 0x30, 70));
		shadow.setColorAt(1., QColor(0x1E, 0x40, 0x30, 0));
		p.fillRect(rect(), shadow);
	}

	const auto colors = std::array{
		QColor(0xF6, 0xC9, 0x3C, 230),
		QColor(0xFF, 0xF3, 0xB0, 200),
		QColor(0xF0, 0x8A, 0x3C, 220),
		QColor(255, 255, 255, 170),
	};
	const auto ring = heroSize * (0.62 + 0.12 * progress);
	for (auto i = 0; i != kParticlesCount; ++i) {
		const auto angle = (i * 360. / kParticlesCount + (i % 3) * 9.)
			* M_PI / 180.;
		const auto distance = ring * (0.78 + ((i * 29) % 40) / 100.);
		const auto position = QPointF(
			center.x() + std::cos(angle) * distance,
			center.y() + std::sin(angle) * distance * 0.9);
		if (position.y() < st::flashgramRouletteHeaderTop + 40
			|| position.y() > st::flashgramRouletteGlassTop - 50) {
			continue;
		}
		const auto &color = colors[i % colors.size()];
		switch (i % 4) {
		case 0: {
			p.setPen(QPen(color, 3., Qt::SolidLine, Qt::RoundCap));
			const auto dx = std::cos(angle) * 9.;
			const auto dy = std::sin(angle) * 9.;
			p.drawLine(position, position + QPointF(dx, dy));
		} break;
		case 1:
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawEllipse(position, 2.6, 2.6);
			break;
		case 2:
			p.save();
			p.translate(position);
			p.rotate(i * 23.);
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawRoundedRect(QRectF(-4., -4., 8., 8.), 2., 2.);
			p.restore();
			break;
		default:
			PaintSparkle(p, position, 6., QColor(255, 255, 255, 220));
			break;
		}
	}

	const auto side = st::flashgramRouletteSide;
	const auto &titleFont = st::flashgramRouletteBigTitleFont;
	const auto titleRect = QRect(
		side,
		st::flashgramRouletteHeaderTop,
		w - 2 * side,
		titleFont->height);
	{
		const auto titleWidth = titleFont->width(Tr("Roulette", "Рулетка"));
		const auto titleCenter = QPointF(
			side + titleWidth / 2.,
			titleRect.y() + titleRect.height() / 2.);
		auto titleGlow = QRadialGradient(titleCenter, titleWidth * 0.75);
		titleGlow.setColorAt(0., QColor(0xB0, 0x7C, 0xF0, 70));
		titleGlow.setColorAt(0.5, QColor(0x5C, 0xB8, 0xF0, 35));
		titleGlow.setColorAt(1., QColor(0x5C, 0xB8, 0xF0, 0));
		p.fillRect(
			QRectF(
				titleCenter.x() - titleWidth,
				titleCenter.y() - titleRect.height() * 1.5,
				titleWidth * 2.,
				titleRect.height() * 3.),
			titleGlow);
	}
	p.setPen(QColor(255, 255, 255));
	p.setFont(titleFont->f);
	p.drawText(
		titleRect,
		int(Qt::AlignLeft | Qt::AlignVCenter),
		Tr("Roulette", "Рулетка"));

	const auto &balanceFont = st::flashgramRouletteBigBalanceFont;
	const auto number = _balance.endsWith(u" FG"_q)
		? _balance.left(_balance.size() - 3)
		: _balance;
	const auto numberWidth = balanceFont->width(number);
	const auto diamond = st::flashgramRouletteDiamondSize;
	PaintDiamond(
		p,
		QRectF(
			w - side - numberWidth - diamond - side / 2.,
			titleRect.y() + (titleRect.height() - diamond) / 2.,
			diamond,
			diamond),
		QColor(255, 255, 255));
	p.setFont(balanceFont->f);
	p.setPen(QColor(255, 255, 255));
	p.drawText(titleRect, int(Qt::AlignRight | Qt::AlignVCenter), number);

	if (result) {
		auto color = QColor(255, 255, 255);
		color.setAlphaF(std::clamp(progress, 0., 1.));
		p.setPen(color);
		p.setFont(st::flashgramRouletteCongratsFont->f);
		p.drawText(
			QRect(
				0,
				st::flashgramRouletteResultTop,
				w,
				st::flashgramRouletteCongratsFont->height),
			Qt::AlignCenter,
			Tr("CONGRATULATIONS", "ПОЗДРАВЛЯЕМ"));
	}
	if (_state != State::Spinning && !_heroTitle.isEmpty()) {
		const auto &nameFont = st::flashgramRouletteHeroNameFont;
		const auto nameRect = QRect(
			0,
			st::flashgramRouletteHeroNameTop,
			w,
			nameFont->height);
		p.setFont(nameFont->f);
		p.setPen(QColor(0x1E, 0x40, 0x30, 70));
		p.drawText(nameRect.translated(0, 2), Qt::AlignCenter, _heroTitle);
		p.setPen(QColor(0xF7, 0xF3, 0xEA));
		p.drawText(nameRect, Qt::AlignCenter, _heroTitle);
	}

	PaintChevronDown(
		p,
		QPointF(w / 2., st::flashgramRouletteChevronY),
		st::flashgramRouletteChevronSize * 2,
		QColor(255, 255, 255, 230));
}

void AddGiftHeader(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const Gift &gift,
		int number) {
	const auto layout = box->verticalLayout();
	const auto row = box->addRow(object_ptr<Ui::RpWidget>(box), QMargins());
	const auto size = st::flashgramPreviewLarge;
	row->resize(row->width(), size + st::flashgramPreviewTitleHeight);
	const auto view = Ui::CreateChild<GiftStickerView>(row, session, gift);
	view->show();
	const auto title = GiftTitle(gift, number);
	const auto rarity = RarityName(gift.rarity);
	row->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(row);
		auto hq = PainterHighQualityEnabler(p);
		const auto center = QPointF(row->width() / 2., size / 2.);
		auto glow = QRadialGradient(center, size * 0.6);
		auto color = RarityColor(gift.rarity);
		color.setAlpha(70);
		glow.setColorAt(0., color);
		color.setAlpha(0);
		glow.setColorAt(1., color);
		p.fillRect(QRect(0, 0, row->width(), size), glow);
		const auto &font = st::flashgramPreviewTitleFont;
		p.setPen(st::windowFg->c);
		p.setFont(font->f);
		p.drawText(
			QRect(0, size, row->width(), font->height),
			Qt::AlignCenter,
			title);
		const auto &small = st::flashgramInventorySubFont;
		p.setFont(small->f);
		p.setPen(RarityColor(gift.rarity));
		p.drawText(
			QRect(0, size + font->height, row->width(), small->height),
			Qt::AlignCenter,
			rarity);
	}, row->lifetime());
	row->widthValue() | rpl::on_next([=](int width) {
		view->setGeometry((width - size) / 2, 0, size, size);
	}, view->lifetime());
	Ui::AddSkip(layout);
}

void AddField(
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

void AddSecondary(not_null<Ui::GenericBox*> box, const QString &text) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(box, text, st::boxDividerLabel),
		st::flashgramSecondaryPadding);
}

void AddBackendDisabledButton(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> text) {
	const auto button = box->addButton(std::move(text), [] {});
	if (button) {
		button->setDisabled(!GiftBackendAvailable());
		button->setAttribute(Qt::WA_TransparentForMouseEvents);
		button->setTextFgOverride(st::windowSubTextFg->c);
	}
}

} // namespace

void LootBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto user = session->user();
	box->setStyle(st::flashgramScreenBox);
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	RequestGiftStickers(session);
	const auto layout = box->verticalLayout();

	AddTopBarRow(layout, [=] { box->closeBox(); }, [=] {
		controller->show(Box(MyGiftsBox, controller));
	});

	const auto balance = layout->lifetime().make_state<QString>();
	const auto balanceCard = AddHomeCard(
		layout,
		st::flashgramHomeBalanceHeight,
		[=](QPainter &p, QRect r, bool over) {
			FillRounded(p, QRectF(r), st::flashgramHomeRadius, st::windowBgOver->c);
			const auto padding = st::flashgramHomePadding;
			const auto &caption = st::flashgramHomeCaptionFont;
			const auto &big = st::flashgramHomeBalanceFont;
			p.setPen(st::windowSubTextFg->c);
			p.setFont(caption->f);
			p.drawText(
				QRect(padding.left(), padding.top(), r.width(), caption->height),
				int(Qt::AlignLeft | Qt::AlignVCenter),
				Tr("Your balance", "Твой баланс"));
			const auto top = padding.top() + caption->height;
			p.setPen(st::windowFg->c);
			p.setFont(big->f);
			p.drawText(
				QRect(padding.left(), top, r.width(), big->height),
				int(Qt::AlignLeft | Qt::AlignVCenter),
				*balance);
			const auto diamond = st::flashgramHomeDiamond;
			PaintDiamond(
				p,
				QRectF(
					padding.left() + big->width(*balance) + diamond / 3.,
					top + (big->height - diamond) / 2.,
					diamond,
					diamond),
				st::windowFg->c);
			p.setPen(st::windowSubTextFg->c);
			p.setFont(st::flashgramHomeArrowFont->f);
			p.drawText(
				QRect(0, 0, r.width() - padding.right(), r.height()),
				int(Qt::AlignRight | Qt::AlignVCenter),
				QString(QChar(0x203A)));
		});
	BalanceValue(user) | rpl::on_next([=](const QString &value) {
		*balance = BalanceNumber(LoadProfile(user).balance);
		balanceCard->update();
	}, balanceCard->lifetime());

	AddHomeCard(
		layout,
		st::flashgramHomeNewsHeight,
		[=](QPainter &p, QRect r, bool over) {
			PaintCardGradient(
				p,
				r,
				QColor(0x2C, 0xC9, 0xB5),
				QColor(0x3B, 0x74, 0xEE),
				over);
			PaintCardTexts(
				p,
				r,
				Tr("New drop!", "Новый розыгрыш!"),
				Tr("The drop is being prepared", "Розыгрыш готовится"),
				st::flashgramHomeTitleFont,
				false);
		});

	const auto roulette = AddHomeCard(
		layout,
		st::flashgramHomeBigHeight,
		[=](QPainter &p, QRect r, bool over) {
			PaintCardGradient(
				p,
				r,
				QColor(0x7A, 0x4C, 0xEC),
				QColor(0xE5, 0x35, 0xB0),
				over);
			PaintCardTexts(
				p,
				r,
				Tr("Roulette", "Рулетка"),
				Tr("A big win in one spin", "Крупный выигрыш в одном спине"),
				st::flashgramHomeBigTitleFont,
				true);
		});
	AddCardStickers(roulette, session, CollectibleIds());
	roulette->setClickedCallback([=] {
		controller->show(Box(RouletteBox, controller));
	});

	auto casePreviews = QStringList();
	for (const auto &entry : Cases()) {
		if (!entry.giftIds.isEmpty()) {
			casePreviews.push_back(entry.giftIds.back());
		}
	}
	const auto cases = AddHomeCard(
		layout,
		st::flashgramHomeBigHeight,
		[=](QPainter &p, QRect r, bool over) {
			PaintCardGradient(
				p,
				r,
				QColor(0xF3, 0x9A, 0x2E),
				QColor(0xE8, 0x3A, 0x86),
				over);
			PaintCardTexts(
				p,
				r,
				Tr("Cases", "Кейсы"),
				Tr("Open and collect gifts", "Открывай и забирай подарки"),
				st::flashgramHomeBigTitleFont,
				true);
		});
	AddCardStickers(cases, session, casePreviews);
	cases->setClickedCallback([=] {
		controller->show(Box(CasesBox, controller));
	});

	const auto giftIcon = GiftsCatalog().empty()
		? nullptr
		: &GiftsCatalog().back();
	const auto myGifts = AddHomeCard(
		layout,
		st::flashgramHomeGiftsHeight,
		[=](QPainter &p, QRect r, bool over) {
			PaintCardGradient(
				p,
				r,
				QColor(0x8C, 0x2A, 0x3A),
				QColor(0x1E, 0x14, 0x18),
				over);
			const auto &font = st::flashgramHomeTitleFont;
			const auto text = Tr("My Gifts", "Мои подарки");
			const auto icon = st::flashgramHomeGiftIcon;
			const auto full = icon + st::flashgramRouletteLinkIconSkip
				+ font->width(text);
			p.setPen(QColor(255, 255, 255));
			p.setFont(font->f);
			p.drawText(
				QRect(
					(r.width() - full) / 2 + icon
						+ st::flashgramRouletteLinkIconSkip,
					0,
					r.width(),
					r.height()),
				int(Qt::AlignLeft | Qt::AlignVCenter),
				text);
		});
	if (giftIcon) {
		const auto view = Ui::CreateChild<GiftStickerView>(
			myGifts.get(),
			session,
			*giftIcon);
		view->show();
		myGifts->sizeValue() | rpl::on_next([=](QSize size) {
			const auto &font = st::flashgramHomeTitleFont;
			const auto icon = st::flashgramHomeGiftIcon;
			const auto full = icon + st::flashgramRouletteLinkIconSkip
				+ font->width(Tr("My Gifts", "Мои подарки"));
			view->setGeometry(
				(size.width() - full) / 2,
				(size.height() - icon) / 2,
				icon,
				icon);
		}, view->lifetime());
	}
	myGifts->setClickedCallback([=] {
		controller->show(Box(MyGiftsBox, controller));
	});
	Ui::AddSkip(layout);
}

void RouletteBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setStyle(st::flashgramScreenBox);
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	box->addRow(
		object_ptr<RouletteScreen>(box, controller, [=] {
			box->closeBox();
		}),
		QMargins());
}

void CasesBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	box->setStyle(st::flashgramScreenBox);
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	RequestGiftStickers(session);
	const auto layout = box->verticalLayout();
	AddTopBarRow(layout, [=] { box->closeBox(); }, [=] {
		controller->show(Box(MyGiftsBox, controller));
	});
	for (const auto &entry : Cases()) {
		const auto id = entry.id;
		const auto top = entry.top.isValid() ? entry.top : QColor(0x4F, 0x8F, 0xE8);
		const auto bottom = entry.bottom.isValid()
			? entry.bottom
			: QColor(0x2A, 0x4F, 0xA3);
		const auto title = entry.name;
		const auto subtitle = entry.description
			+ u" · "_q
			+ FormatBalance(entry.price);
		const auto card = AddHomeCard(
			layout,
			st::flashgramHomeBigHeight,
			[=](QPainter &p, QRect r, bool over) {
				PaintCardGradient(p, r, top, bottom, over);
				PaintCardTexts(
					p,
					r,
					title,
					subtitle,
					st::flashgramHomeBigTitleFont,
					true);
			});
		AddCardStickers(card, session, entry.giftIds);
		card->setClickedCallback([=] {
			controller->show(Box(CaseBox, controller, id));
		});
	}
	Ui::AddSkip(layout);
}

void CaseBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		QString caseId) {
	const auto session = &controller->session();
	const auto user = session->user();
	box->setWidth(st::boxWideWidth);
	RequestGiftStickers(session);

	const auto found = FindCase(caseId);
	if (!found) {
		box->setTitle(TrValue("Cases", "Кейсы"));
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
		return;
	}
	const auto entry = *found;
	box->setTitle(rpl::single(entry.name));

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		BalanceValue(user) | rpl::map([](const QString &balance) {
			return Tr("Balance: ", "Баланс: ") + balance;
		}),
		st::boxLabel));
	Ui::AddSkip(box->verticalLayout());

	const auto strip = box->addRow(
		object_ptr<RouletteStrip>(
			box,
			session,
			st::boxBg->c,
			st::activeButtonBg->c),
		QMargins());
	strip->fill(entry.giftIds, nullptr);

	Ui::AddSubsectionTitle(
		box->verticalLayout(),
		TrValue("Possible gifts", "Возможные подарки"));
	auto list = std::vector<OwnedGift>();
	for (const auto &id : entry.giftIds) {
		if (const auto gift = FindGift(id)) {
			list.push_back({ .giftId = id, .number = gift->number });
		}
	}
	box->addRow(
		object_ptr<LocalGiftsGrid>(
			box,
			session,
			std::move(list),
			[=](OwnedGift owned) {
				controller->show(
					Box(LocalGiftDetailsBox, controller, owned, false));
			}),
		st::flashgramBoxGiftsPadding);

	struct State {
		Ui::Animations::Simple animation;
		bool opening = false;
	};
	const auto state = box->lifetime().make_state<State>();
	box->addButton(
		rpl::single(Tr("Open · %1", "Открыть · %1").arg(
			FormatBalance(entry.price))),
		[=] {
			if (state->opening) {
				return;
			}
			const auto winner = RollGift(entry.giftIds);
			if (!winner) {
				return;
			} else if (!SpendBalance(user, entry.price)) {
				controller->uiShow()->showToast(Tr(
					"Not enough FlashGram Balance.",
					"Недостаточно FlashGram Balance."));
				return;
			}
			state->opening = true;
			strip->fill(entry.giftIds, winner);
			const auto owned = OwnedGift{
				.giftId = winner->id,
				.number = RollNumber(*winner),
			};
			state->animation.start([=](float64 value) {
				strip->setOffset(value);
				if (!state->animation.animating() && state->opening) {
					state->opening = false;
					const auto added = AddInventoryGift(user, owned);
					controller->show(
						Box(LocalGiftDetailsBox, controller, added, true));
				}
			}, 0., SpinTarget(strip), kSpinDuration, anim::easeOutCubic);
		});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void LocalGiftDetailsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		OwnedGift owned,
		bool justWon) {
	const auto session = &controller->session();
	const auto user = session->user();
	box->setWidth(st::boxWideWidth);

	const auto found = FindGift(owned.giftId);
	if (!found) {
		box->setTitle(TrValue("Gift", "Подарок"));
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
		return;
	}
	const auto gift = *found;
	const auto number = owned.number ? owned.number : gift.number;
	box->setTitle(justWon
		? TrValue("Congratulations!", "Поздравляем!")
		: TrValue("Gift", "Подарок"));

	AddGiftHeader(box, session, gift, number);

	const auto uid = owned.uid;
	const auto current = [=]() -> std::optional<OwnedGift> {
		if (uid.isEmpty()) {
			return std::nullopt;
		}
		const auto profile = LoadProfile(user);
		const auto i = ranges::find(profile.gifts, uid, &OwnedGift::uid);
		return (i != end(profile.gifts))
			? std::make_optional(*i)
			: std::nullopt;
	};
	const auto profile = LoadProfile(user);
	const auto mine = current();
	AddField(
		box,
		Tr("Number", "Номер"),
		u"#"_q + FormatCount(number) + ((gift.amount > 0)
			? (u" / "_q + FormatCount(gift.amount))
			: QString()));
	AddField(box, tr::lng_gift_unique_model(tr::now), gift.model);
	AddField(box, tr::lng_gift_unique_backdrop(tr::now), gift.backdrop);
	AddField(box, tr::lng_gift_unique_symbol(tr::now), gift.symbol);
	AddField(
		box,
		Tr("Owner", "Владелец"),
		!mine
			? Tr("Not in your collection", "Нет в вашей коллекции")
			: profile.owner
			? (profile.displayName + u" ("_q + profile.flashgramId + ')')
			: profile.displayName);
	if (gift.value.cents > 0) {
		AddField(box, Tr("Value", "Ценность"), FormatBalance(gift.value));
	}
	AddField(
		box,
		Tr("Source", "Источник"),
		QString::fromLatin1(kLocalSource));

	if (mine) {
		const auto layout = box->verticalLayout();
		Ui::AddSkip(layout);
		Ui::AddDivider(layout);
		Ui::AddSkip(layout);
		const auto flagText = [=](
				GiftFlag flag,
				const char *onEn,
				const char *onRu,
				const char *offEn,
				const char *offRu) {
			return rpl::single(rpl::empty) | rpl::then(
				Changes()
			) | rpl::map([=] {
				const auto now = current();
				const auto on = now && ((flag == GiftFlag::Pinned)
					? now->pinned
					: now->inProfile);
				return on ? Tr(onEn, onRu) : Tr(offEn, offRu);
			});
		};
		Settings::AddButtonWithIcon(
			layout,
			flagText(
				GiftFlag::InProfile,
				"Remove from profile",
				"Убрать из профиля",
				"Show in profile",
				"В профиль"),
			st::settingsButton,
			{ .icon = &st::menuIconProfile }
		)->setClickedCallback([=] {
			if (const auto now = current()) {
				SetGiftFlag(
					user,
					now->uid,
					GiftFlag::InProfile,
					!now->inProfile);
			}
		});
		Settings::AddButtonWithIcon(
			layout,
			flagText(GiftFlag::Pinned, "Unpin", "Открепить", "Pin", "Закрепить"),
			st::settingsButton,
			{ .icon = &st::menuIconPin }
		)->setClickedCallback([=] {
			if (const auto now = current()) {
				SetGiftFlag(user, now->uid, GiftFlag::Pinned, !now->pinned);
			}
		});
		Settings::AddButtonWithIcon(
			layout,
			TrValue("Transfer", "Передать"),
			st::settingsButton,
			{ .icon = &st::menuIconSend }
		)->setClickedCallback([=] {
			if (const auto now = current()) {
				controller->show(Box(TransferGiftBox, controller, *now));
			}
		});
		Settings::AddButtonWithIcon(
			layout,
			TrValue("Sell", "Продать"),
			st::settingsButton,
			{ .icon = &st::menuIconEarn }
		)->setClickedCallback([=] {
			if (const auto now = current()) {
				controller->show(Box(SellGiftBox, controller, *now));
			}
		});
		Ui::AddSkip(layout);
	}

	if (justWon) {
		box->addButton(TrValue("To collection", "В коллекцию"), [=] {
			box->closeBox();
			controller->show(Box(MyGiftsBox, controller));
		});
	}
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void SellGiftBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		OwnedGift owned) {
	const auto session = &controller->session();
	box->setTitle(TrValue("Sell gift", "Продать подарок"));
	box->setWidth(st::boxWideWidth);
	const auto gift = FindGift(owned.giftId);
	if (gift) {
		AddGiftHeader(
			box,
			session,
			*gift,
			owned.number ? owned.number : gift->number);
	}
	const auto suggested = gift ? (gift->value.cents / 100) : 0;
	box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		TrValue("Price in FG", "Цена в FG"),
		QString::number(suggested)));
	AddSecondary(
		box,
		Tr(
			"Selling to another user needs the FlashGram server. Soon.",
			"Для продажи другому пользователю нужен сервер FlashGram. "
			"Скоро."));
	AddBackendDisabledButton(box, TrValue("List for sale", "Выставить"));
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void TransferGiftBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		OwnedGift owned) {
	const auto session = &controller->session();
	box->setTitle(TrValue("Transfer gift", "Передать подарок"));
	box->setWidth(st::boxWideWidth);
	if (const auto gift = FindGift(owned.giftId)) {
		AddGiftHeader(
			box,
			session,
			*gift,
			owned.number ? owned.number : gift->number);
	}
	box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		TrValue("Recipient FlashGram ID", "FlashGram ID получателя"),
		QString()));
	AddSecondary(
		box,
		Tr(
			"Transfers will be available once the FlashGram Server is "
			"connected.",
			"Передача станет доступна после подключения FlashGram Server."));
	AddBackendDisabledButton(box, TrValue("Transfer", "Передать"));
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace FlashGram
