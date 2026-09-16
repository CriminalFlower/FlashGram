/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_ui.h"

#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"

#include <QtGui/QPainterPath>

namespace FlashGram::Design {
namespace {

constexpr auto kTextHeightLimit = 100000;

} // namespace

const Colors &C() {
	static const auto result = Colors{
		.bg = QColor(0x13, 0x14, 0x19),
		.card = QColor(0x1F, 0x20, 0x27),
		.cardOver = QColor(0x26, 0x27, 0x2F),
		.inner = QColor(0x2B, 0x2C, 0x35),
		.text = QColor(0xFF, 0xFF, 0xFF),
		.sub = QColor(0x9A, 0x9C, 0xA8),
		.accent = QColor(0x4A, 0x93, 0xFF),
		.heroTop = QColor(0x2B, 0x80, 0xFC),
		.heroBottom = QColor(0x10, 0x58, 0xDE),
		.lavender = QColor(0xC9, 0xB6, 0xFF),
		.button = QColor(0x30, 0x31, 0x3B),
		.buttonOver = QColor(0x38, 0x39, 0x45),
		.green = QColor(0x4C, 0xD0, 0x8A),
		.red = QColor(0xFF, 0x7A, 0x7A),
		.nav = QColor(0x1C, 0x1D, 0x24),
	};
	return result;
}

const style::Box &ScreenBoxStyle() {
	static const auto background = style::internal::OwnedColor(C().bg);
	static const auto result = [] {
		auto copy = st::flashgramScreenBox;
		copy.bg = background.color();
		return copy;
	}();
	return result;
}

int WrappedHeight(const style::font &font, const QString &text, int width) {
	if (text.isEmpty() || width <= 0) {
		return 0;
	}
	return int(std::ceil(font->metrics().boundingRect(
		QRectF(0, 0, width, kTextHeightLimit),
		Qt::TextWordWrap,
		text).height()));
}

void DrawText(
		QPainter &p,
		const style::font &font,
		const QColor &color,
		const QRect &rect,
		const QString &text,
		int flags) {
	p.setFont(font->f);
	p.setPen(color);
	p.drawText(rect, flags, text);
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

void PaintCard(QPainter &p, const QRect &rect, const QColor &color) {
	FillRounded(p, QRectF(rect), st::flashgramVerifyCardRadius, color);
}

QRect CardRect(const QRect &rect) {
	return rect.marginsRemoved(st::flashgramVerifyCardMargin);
}

void PaintChevronRight(
		QPainter &p,
		QPointF center,
		float64 size,
		const QColor &color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(color, st::flashgramCapsuleStroke, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(
		QPointF(center.x() - size / 4., center.y() - size / 2.),
		QPointF(center.x() + size / 4., center.y()));
	p.drawLine(
		QPointF(center.x() + size / 4., center.y()),
		QPointF(center.x() - size / 4., center.y() + size / 2.));
}

void PaintChevronDown(
		QPainter &p,
		QPointF center,
		float64 size,
		const QColor &color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(color, st::flashgramCapsuleStroke, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(
		QPointF(center.x() - size / 2., center.y() - size / 4.),
		QPointF(center.x(), center.y() + size / 4.));
	p.drawLine(
		QPointF(center.x(), center.y() + size / 4.),
		QPointF(center.x() + size / 2., center.y() - size / 4.));
}

void PaintBackArrow(
		QPainter &p,
		QPointF center,
		float64 size,
		const QColor &color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(color, st::flashgramCapsuleStroke, Qt::SolidLine, Qt::RoundCap));
	const auto left = QPointF(center.x() - size / 2., center.y());
	p.drawLine(left, QPointF(center.x() + size / 2., center.y()));
	p.drawLine(left, QPointF(left.x() + size * 0.45, left.y() - size * 0.45));
	p.drawLine(left, QPointF(left.x() + size * 0.45, left.y() + size * 0.45));
}

void PaintDots(QPainter &p, QPointF center, float64 size, const QColor &color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto r = size / 8.;
	for (auto i = -1; i != 2; ++i) {
		p.drawEllipse(QPointF(center.x(), center.y() + i * size / 2.6), r, r);
	}
}

void PaintCheckMark(
		QPainter &p,
		QPointF center,
		float64 size,
		const QColor &color,
		float64 width) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	auto path = QPainterPath();
	path.moveTo(center.x() - size * 0.5, center.y());
	path.lineTo(center.x() - size * 0.12, center.y() + size * 0.38);
	path.lineTo(center.x() + size * 0.52, center.y() - size * 0.36);
	p.drawPath(path);
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

void PaintGem(
		QPainter &p,
		const QRectF &rect,
		const QColor &top,
		const QColor &bottom) {
	auto hq = PainterHighQualityEnabler(p);
	const auto x = rect.x();
	const auto y = rect.y();
	const auto w = rect.width();
	const auto h = rect.height();
	auto body = QPainterPath();
	body.moveTo(x + w * 0.22, y + h * 0.08);
	body.lineTo(x + w * 0.78, y + h * 0.08);
	body.lineTo(x + w, y + h * 0.36);
	body.lineTo(x + w * 0.5, y + h * 0.96);
	body.lineTo(x, y + h * 0.36);
	body.closeSubpath();
	auto fill = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	fill.setColorAt(0., top);
	fill.setColorAt(1., bottom);
	p.setPen(Qt::NoPen);
	p.fillPath(body, fill);

	auto crown = QPainterPath();
	crown.moveTo(x + w * 0.22, y + h * 0.08);
	crown.lineTo(x + w * 0.78, y + h * 0.08);
	crown.lineTo(x + w, y + h * 0.36);
	crown.lineTo(x, y + h * 0.36);
	crown.closeSubpath();
	p.fillPath(crown, QColor(255, 255, 255, 70));

	p.setPen(QPen(QColor(255, 255, 255, 110), std::max(w / 26., 0.8)));
	p.drawLine(QPointF(x, y + h * 0.36), QPointF(x + w, y + h * 0.36));
	p.drawLine(QPointF(x + w * 0.36, y + h * 0.36), QPointF(x + w * 0.5, y + h * 0.96));
	p.drawLine(QPointF(x + w * 0.64, y + h * 0.36), QPointF(x + w * 0.5, y + h * 0.96));
	p.drawLine(QPointF(x + w * 0.36, y + h * 0.36), QPointF(x + w * 0.5, y + h * 0.08));
	p.drawLine(QPointF(x + w * 0.64, y + h * 0.36), QPointF(x + w * 0.5, y + h * 0.08));
}

Block::Block(
	QWidget *parent,
	QColor background,
	Fn<int(int)> height,
	Fn<void(QPainter&, QRect)> paint)
: RpWidget(parent)
, _background(background)
, _height(std::move(height))
, _paint(std::move(paint)) {
}

void Block::setLayoutCallback(Fn<void(int, int)> callback) {
	_layout = std::move(callback);
	if (_layout && width() > 0) {
		_layout(width(), height());
	}
}

void Block::refresh() {
	resizeToWidth(width());
	if (_layout) {
		_layout(width(), height());
	}
	update();
}

int Block::resizeGetHeight(int newWidth) {
	return _height(newWidth);
}

void Block::resizeEvent(QResizeEvent *e) {
	if (_layout) {
		_layout(width(), height());
	}
}

void Block::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_background.isValid()) {
		p.fillRect(rect(), _background);
	}
	if (_paint) {
		_paint(p, rect());
	}
}

not_null<Block*> AddBlock(
		not_null<Ui::VerticalLayout*> layout,
		Fn<int(int)> height,
		Fn<void(QPainter&, QRect)> paint,
		QColor background) {
	return layout->add(
		object_ptr<Block>(
			layout,
			background.isValid() ? background : C().bg,
			std::move(height),
			std::move(paint)),
		QMargins());
}

not_null<Ui::AbstractButton*> CreateButton(
		not_null<QWidget*> parent,
		Fn<void(QPainter&, QRect, bool)> paint,
		Fn<void()> click) {
	struct Feedback {
		Ui::Animations::Simple over;
		Ui::Animations::Simple press;
		bool hovered = false;
		bool pressed = false;
	};
	const auto button = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	const auto feedback = button->lifetime().make_state<Feedback>();
	button->setClickedCallback(std::move(click));
	const auto repaint = [=] {
		button->update();
	};
	button->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter || type == QEvent::Leave) {
			const auto hovered = (type == QEvent::Enter);
			if (feedback->hovered != hovered) {
				feedback->hovered = hovered;
				feedback->over.start(
					repaint,
					hovered ? 0. : 1.,
					hovered ? 1. : 0.,
					kHoverDuration);
			}
			if (!hovered && feedback->pressed) {
				feedback->pressed = false;
				feedback->press.start(repaint, 1., 0., kPressDuration);
			}
		} else if (type == QEvent::MouseButtonPress
			|| type == QEvent::MouseButtonRelease) {
			const auto pressed = (type == QEvent::MouseButtonPress);
			if (feedback->pressed != pressed) {
				feedback->pressed = pressed;
				feedback->press.start(
					repaint,
					pressed ? 0. : 1.,
					pressed ? 1. : 0.,
					kPressDuration);
			}
		}
	}, button->lifetime());
	button->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(button);
		const auto r = button->rect();
		const auto press = feedback->press.value(feedback->pressed ? 1. : 0.);
		if (press > 0.) {
			const auto scale = 1. - kPressScale * press;
			const auto center = QRectF(r).center();
			p.translate(center);
			p.scale(scale, scale);
			p.translate(-center);
		}
		const auto over = feedback->over.value(feedback->hovered ? 1. : 0.);
		if (over <= 0.) {
			paint(p, r, false);
		} else if (over >= 1.) {
			paint(p, r, true);
		} else {
			paint(p, r, false);
			p.setOpacity(over);
			paint(p, r, true);
		}
	}, button->lifetime());
	button->show();
	return button;
}

void SetupScreenBox(not_null<Ui::GenericBox*> box) {
	box->setStyle(ScreenBoxStyle());
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::flashgramVerifyBoxHeight);
	box->setMinHeight(st::flashgramVerifyBoxHeight);
}

not_null<Block*> SetupScreenHeader(
		not_null<Ui::GenericBox*> box,
		QString title,
		QString subtitle) {
	const auto header = box->setPinnedToTopContent(object_ptr<Block>(
		box,
		C().bg,
		[](int) { return st::flashgramVerifyHeaderHeight; },
		[=](QPainter &p, QRect r) {
			const auto &font = st::flashgramVerifyTitleFont;
			const auto &subFont = st::flashgramVerifyHeaderSubFont;
			const auto left = st::flashgramVerifySide
				+ st::flashgramVerifyIconButton
				+ st::flashgramVerifyTitleSkip;
			const auto lines = font->height
				+ (subtitle.isEmpty() ? 0 : subFont->height);
			const auto top = (r.height() - lines) / 2;
			DrawText(
				p,
				font,
				C().text,
				QRect(left, top, r.width() - left, font->height),
				font->elided(title, r.width() - left - st::flashgramVerifySide));
			if (!subtitle.isEmpty()) {
				DrawText(
					p,
					subFont,
					C().sub,
					QRect(left, top + font->height, r.width() - left, subFont->height),
					subtitle);
			}
		}));
	const auto back = CreateButton(header, [](QPainter &p, QRect r, bool over) {
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
	header->setLayoutCallback([=](int width, int height) {
		const auto size = st::flashgramVerifyIconButton;
		back->setGeometry(
			st::flashgramVerifySide,
			(height - size) / 2,
			size,
			size);
	});
	return header;
}

} // namespace FlashGram::Design
