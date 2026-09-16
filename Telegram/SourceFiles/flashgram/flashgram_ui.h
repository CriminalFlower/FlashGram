/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace style {
struct Box;
} // namespace style

namespace Ui {
class AbstractButton;
class GenericBox;
class VerticalLayout;
} // namespace Ui

// Shared building blocks of the premium dark FlashGram screens:
// one palette, rounded cards, painted icons and buttons with hover and
// press feedback. Sizes come from flashgram.style.
namespace FlashGram::Design {

inline constexpr auto kHoverDuration = crl::time(150);
inline constexpr auto kPressDuration = crl::time(120);
inline constexpr auto kPressScale = 0.025;

struct Colors {
	QColor bg;
	QColor card;
	QColor cardOver;
	QColor inner;
	QColor text;
	QColor sub;
	QColor accent;
	QColor heroTop;
	QColor heroBottom;
	QColor lavender;
	QColor button;
	QColor buttonOver;
	QColor green;
	QColor red;
	QColor nav;
};

[[nodiscard]] const Colors &C();
[[nodiscard]] const style::Box &ScreenBoxStyle();

[[nodiscard]] int WrappedHeight(
	const style::font &font,
	const QString &text,
	int width);
void DrawText(
	QPainter &p,
	const style::font &font,
	const QColor &color,
	const QRect &rect,
	const QString &text,
	int flags = int(Qt::AlignLeft | Qt::AlignVCenter));
void FillRounded(
	QPainter &p,
	const QRectF &rect,
	float64 radius,
	const QBrush &brush);
void PaintCard(QPainter &p, const QRect &rect, const QColor &color);
[[nodiscard]] QRect CardRect(const QRect &rect);

void PaintChevronRight(
	QPainter &p,
	QPointF center,
	float64 size,
	const QColor &color);
void PaintChevronDown(
	QPainter &p,
	QPointF center,
	float64 size,
	const QColor &color);
void PaintBackArrow(
	QPainter &p,
	QPointF center,
	float64 size,
	const QColor &color);
void PaintDots(QPainter &p, QPointF center, float64 size, const QColor &color);
void PaintCheckMark(
	QPainter &p,
	QPointF center,
	float64 size,
	const QColor &color,
	float64 width);
void PaintSparkle(QPainter &p, QPointF c, float64 r, const QColor &color);
void PaintGem(
	QPainter &p,
	const QRectF &rect,
	const QColor &top,
	const QColor &bottom);

class Block final : public Ui::RpWidget {
public:
	Block(
		QWidget *parent,
		QColor background,
		Fn<int(int)> height,
		Fn<void(QPainter&, QRect)> paint);

	void setLayoutCallback(Fn<void(int, int)> callback);
	void refresh();

protected:
	int resizeGetHeight(int newWidth) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const QColor _background;
	const Fn<int(int)> _height;
	const Fn<void(QPainter&, QRect)> _paint;
	Fn<void(int, int)> _layout;

};

not_null<Block*> AddBlock(
	not_null<Ui::VerticalLayout*> layout,
	Fn<int(int)> height,
	Fn<void(QPainter&, QRect)> paint,
	QColor background = QColor());

not_null<Ui::AbstractButton*> CreateButton(
	not_null<QWidget*> parent,
	Fn<void(QPainter&, QRect, bool)> paint,
	Fn<void()> click);

void SetupScreenBox(not_null<Ui::GenericBox*> box);
not_null<Block*> SetupScreenHeader(
	not_null<Ui::GenericBox*> box,
	QString title,
	QString subtitle);

} // namespace FlashGram::Design
