/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_glass_player.h"

#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "styles/style_flashgram.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtGui/QPainterPath>

namespace FlashGram {
namespace {

constexpr auto kMaxTimedTextSize = 2 * 1024 * 1024;
constexpr auto kBackdropWidth = 64;
constexpr auto kBackdropRefresh = crl::time(700);
constexpr auto kBackdropRetry = crl::time(250);
constexpr auto kLastLineDuration = crl::time(5000);
constexpr auto kMaxExtrapolation = crl::time(120);
constexpr auto kScrollDuration = crl::time(460);
constexpr auto kFadeDuration = crl::time(220);
constexpr auto kHoverDuration = crl::time(140);
constexpr auto kSheenPeriod = crl::time(7000);

[[nodiscard]] QString StripMarkup(QString text) {
	static const auto tags = QRegularExpression(u"<[^>]*>|\\{[^}]*\\}"_q);
	text.remove(tags);
	return text.simplified();
}

[[nodiscard]] std::optional<crl::time> ParseClock(const QString &value) {
	// hh:mm:ss,mmm | hh:mm:ss.mmm | mm:ss.mmm | mm:ss.xx
	static const auto re = QRegularExpression(
		u"^(?:(\\d+):)?(\\d{1,2}):(\\d{1,2})(?:[.,](\\d{1,3}))?$"_q);
	const auto match = re.match(value.trimmed());
	if (!match.hasMatch()) {
		return std::nullopt;
	}
	const auto hours = match.captured(1).toLongLong();
	const auto minutes = match.captured(2).toLongLong();
	const auto seconds = match.captured(3).toLongLong();
	auto fraction = match.captured(4);
	while (!fraction.isEmpty() && fraction.size() < 3) {
		fraction.append('0');
	}
	return ((hours * 60 + minutes) * 60 + seconds) * 1000
		+ fraction.toLongLong();
}

[[nodiscard]] std::vector<TimedLine> ParseLrc(const QStringList &lines) {
	static const auto stamp = QRegularExpression(
		u"\\[(\\d{1,3}:\\d{1,2}(?:[.,]\\d{1,3})?)\\]"_q);
	static const auto offsetTag = QRegularExpression(
		u"^\\[offset:\\s*([+-]?\\d+)\\s*\\]"_q,
		QRegularExpression::CaseInsensitiveOption);
	auto offset = crl::time(0);
	auto result = std::vector<TimedLine>();
	for (const auto &line : lines) {
		const auto offsetMatch = offsetTag.match(line);
		if (offsetMatch.hasMatch()) {
			offset = offsetMatch.captured(1).toLongLong();
			continue;
		}
		auto times = std::vector<crl::time>();
		auto position = 0;
		while (true) {
			const auto match = stamp.match(line, position);
			if (!match.hasMatch() || match.capturedStart() != position) {
				break;
			}
			if (const auto time = ParseClock(match.captured(1))) {
				times.push_back(*time);
			}
			position = match.capturedEnd();
		}
		const auto text = StripMarkup(line.mid(position));
		for (const auto time : times) {
			// Empty timed lines are pauses: they end the previous line.
			result.push_back({
				.from = std::max(time - offset, crl::time(0)),
				.text = text,
			});
		}
	}
	ranges::stable_sort(result, ranges::less(), &TimedLine::from);
	for (auto i = 0; i != int(result.size()); ++i) {
		result[i].till = (i + 1 < int(result.size()))
			? result[i + 1].from
			: (result[i].from + kLastLineDuration);
	}
	result.erase(ranges::remove_if(result, [](const TimedLine &line) {
		return line.text.isEmpty() || line.till <= line.from;
	}), end(result));
	return result;
}

[[nodiscard]] std::vector<TimedLine> ParseCues(const QStringList &lines) {
	auto result = std::vector<TimedLine>();
	auto current = std::optional<TimedLine>();
	const auto flush = [&] {
		if (current) {
			current->text = StripMarkup(current->text);
			if (!current->text.isEmpty() && current->till > current->from) {
				result.push_back(std::move(*current));
			}
			current = std::nullopt;
		}
	};
	for (const auto &line : lines) {
		const auto arrow = line.indexOf(u"-->"_q);
		if (arrow > 0) {
			flush();
			const auto from = ParseClock(line.left(arrow));
			const auto rest = line.mid(arrow + 3).trimmed();
			const auto space = rest.indexOf(' ');
			const auto till = ParseClock(
				(space > 0) ? rest.left(space) : rest);
			if (from && till) {
				current = TimedLine{ .from = *from, .till = *till };
			}
		} else if (line.trimmed().isEmpty()) {
			flush();
		} else if (current) {
			if (!current->text.isEmpty()) {
				current->text.append(' ');
			}
			current->text.append(line);
		}
	}
	flush();
	ranges::stable_sort(result, ranges::less(), &TimedLine::from);
	return result;
}

[[nodiscard]] float64 BarLevel(uint64 seed, int index) {
	const auto value = std::sin(
		index * 12.9898 + double(seed % 100003) * 0.0137) * 43758.5453;
	return 0.3 + 0.7 * (value - std::floor(value));
}

[[nodiscard]] QPainterPath RoundedPath(QRectF rect, float64 radius) {
	auto result = QPainterPath();
	result.addRoundedRect(rect, radius, radius);
	return result;
}

} // namespace

std::vector<TimedLine> ParseTimedText(const QByteArray &data) {
	auto text = QString::fromUtf8(data);
	if (text.startsWith(QChar(0xFEFF))) {
		text.remove(0, 1);
	}
	text.replace(u"\r\n"_q, u"\n"_q).replace('\r', '\n');
	const auto lines = text.split('\n');
	return text.contains(u"-->"_q) ? ParseCues(lines) : ParseLrc(lines);
}

QStringList FindTimedTextFiles(const QString &mediaPath) {
	if (mediaPath.isEmpty()) {
		return {};
	}
	const auto info = QFileInfo(mediaPath);
	const auto dir = info.absoluteDir();
	if (!dir.exists()) {
		return {};
	}
	const auto base = info.completeBaseName();
	auto result = QStringList();
	for (const auto &extension : { u"lrc"_q, u"srt"_q, u"vtt"_q }) {
		const auto exact = base + '.' + extension;
		if (dir.exists(exact)) {
			result.push_back(dir.filePath(exact));
		}
		const auto variants = dir.entryList(
			{ u"*."_q + extension },
			QDir::Files | QDir::Readable,
			QDir::Name);
		for (const auto &name : variants) {
			if (name != exact
				&& name.startsWith(base + '.', Qt::CaseInsensitive)
				&& name.size() > exact.size()) {
				result.push_back(dir.filePath(name));
			}
		}
	}
	return result;
}

struct GlassPlayer::Backdrop {
	QImage blurred;
	QColor tint = QColor(26, 28, 34);
	QRect content;
	int generation = 0;
	crl::time position = 0;
	crl::time length = 0;
	crl::time stamp = 0;
	bool playing = false;

	[[nodiscard]] crl::time now() const {
		if (!playing) {
			return position;
		}
		const auto passed = std::clamp(
			crl::now() - stamp,
			crl::time(0),
			kMaxExtrapolation);
		return length > 0
			? std::min(position + passed, length)
			: (position + passed);
	}
};

namespace {

using Backdrop = GlassPlayer::Backdrop;

// Caches the blurred backdrop + tint + highlight for one surface.
class GlassSurface final {
public:
	void paint(
			QPainter &p,
			QRect glass,
			QPoint topLeftInParent,
			const Backdrop &backdrop,
			float64 radius,
			bool shadow) {
		const auto full = shadow
			? glass.marginsAdded(QMargins(
				st::flashgramGlassShadow,
				st::flashgramGlassShadow,
				st::flashgramGlassShadow,
				st::flashgramGlassShadow))
			: glass;
		const auto ratio = p.device()->devicePixelRatioF();
		if (_generation != backdrop.generation
			|| _glass != glass
			|| _position != topLeftInParent
			|| _ratio != ratio
			|| _shadow != shadow) {
			_generation = backdrop.generation;
			_glass = glass;
			_position = topLeftInParent;
			_ratio = ratio;
			_shadow = shadow;
			rebuild(full, backdrop, radius);
		}
		p.drawImage(full.topLeft(), _cache);

		// Soft reflection band drifting slowly with the playback clock.
		const auto phase = float64(backdrop.now() % kSheenPeriod)
			/ kSheenPeriod;
		const auto band = glass.width() * 0.6;
		const auto x = glass.x() - band + (glass.width() + 2 * band) * phase;
		auto sheen = QLinearGradient(
			QPointF(x, glass.y()),
			QPointF(x + band, glass.y() + glass.height()));
		sheen.setColorAt(0., QColor(255, 255, 255, 0));
		sheen.setColorAt(0.5, QColor(255, 255, 255, 16));
		sheen.setColorAt(1., QColor(255, 255, 255, 0));
		PainterHighQualityEnabler hq(p);
		p.fillPath(RoundedPath(glass, radius), sheen);
	}

private:
	void rebuild(QRect full, const Backdrop &backdrop, float64 radius) {
		_cache = QImage(
			full.size() * _ratio,
			QImage::Format_ARGB32_Premultiplied);
		_cache.setDevicePixelRatio(_ratio);
		_cache.fill(Qt::transparent);

		auto p = QPainter(&_cache);
		PainterHighQualityEnabler hq(p);
		p.translate(-full.topLeft());
		const auto rect = QRectF(_glass);
		if (_shadow) {
			p.setPen(Qt::NoPen);
			const auto steps = 4;
			const auto grow = float64(st::flashgramGlassShadow) / steps;
			for (auto i = steps; i != 0; --i) {
				p.setBrush(QColor(0, 0, 0, 10 + (steps - i) * 6));
				const auto margin = grow * i;
				p.drawRoundedRect(
					rect.adjusted(
						-margin,
						-margin + grow,
						margin,
						margin + grow),
					radius + margin,
					radius + margin);
			}
		}
		const auto path = RoundedPath(rect, radius);
		p.setClipPath(path);
		p.fillRect(rect, backdrop.tint.darker(170));
		if (!backdrop.blurred.isNull() && !backdrop.content.isEmpty()) {
			p.setOpacity(0.9);
			p.drawImage(
				QRect(
					backdrop.content.topLeft() - _position,
					backdrop.content.size()),
				backdrop.blurred);
			p.setOpacity(1.);
		}
		auto tint = backdrop.tint;
		tint.setAlpha(120);
		p.fillRect(rect, tint);
		p.fillRect(rect, QColor(255, 255, 255, 14));

		auto top = QLinearGradient(rect.topLeft(), rect.bottomLeft());
		top.setColorAt(0., QColor(255, 255, 255, 46));
		top.setColorAt(0.35, QColor(255, 255, 255, 8));
		top.setColorAt(1., QColor(255, 255, 255, 0));
		p.fillRect(rect, top);

		auto glow = QRadialGradient(
			rect.topLeft() + QPointF(rect.width() * 0.2, 0),
			rect.width() * 0.6);
		glow.setColorAt(0., QColor(255, 255, 255, 26));
		glow.setColorAt(1., QColor(255, 255, 255, 0));
		p.fillRect(rect, glow);
		p.setClipping(false);

		auto border = QLinearGradient(rect.topLeft(), rect.bottomRight());
		border.setColorAt(0., QColor(255, 255, 255, 110));
		border.setColorAt(0.5, QColor(255, 255, 255, 26));
		border.setColorAt(1., QColor(255, 255, 255, 60));
		p.setPen(QPen(QBrush(border), 1.));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
	}

	QImage _cache;
	QRect _glass;
	QPoint _position;
	float64 _ratio = 0.;
	int _generation = -1;
	bool _shadow = false;

};

[[nodiscard]] QString FormatTime(crl::time ms) {
	return Ui::FormatDurationText(std::max(ms, crl::time(0)) / 1000);
}

} // namespace

class GlassPlayer::Header final : public Ui::RpWidget {
public:
	enum class Button {
		Language,
		Text,
		Close,
	};

	Header(
		QWidget *parent,
		std::shared_ptr<Backdrop> backdrop,
		const Descriptor &descriptor);

	void setButtons(bool language, bool text, bool textActive);
	void setShown(bool shown);
	void playbackUpdated();
	[[nodiscard]] int countHeight() const;

	Fn<void(Button)> clicked;
	Fn<void(crl::time)> seek;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct ButtonState {
		Button type = Button::Close;
		QRect rect;
		Ui::Animations::Simple hover;
		bool over = false;
	};

	[[nodiscard]] QRect glassRect() const;
	[[nodiscard]] QRect innerRect() const;
	[[nodiscard]] QRect pillRect() const;
	[[nodiscard]] QRect waveRect() const;
	void layoutButtons();
	void updateOver(QPoint point);
	void paintButton(QPainter &p, const ButtonState &button);
	void paintWave(QPainter &p, QRect rect);

	const std::shared_ptr<Backdrop> _backdrop;
	const QString _title;
	const QString _performer;
	const uint64 _seed = 0;
	GlassSurface _surface;
	std::vector<std::unique_ptr<ButtonState>> _buttons;
	bool _textActive = true;
	int _pressed = -1;
	bool _wavePressed = false;
	std::optional<int> _waveHover;
	Ui::Animations::Simple _press;
	Ui::Animations::Simple _shownAnimation;
	bool _shown = true;
	Ui::Animations::Basic _ticker;

};

GlassPlayer::Header::Header(
	QWidget *parent,
	std::shared_ptr<Backdrop> backdrop,
	const Descriptor &descriptor)
: RpWidget(parent)
, _backdrop(std::move(backdrop))
, _title(descriptor.title)
, _performer(descriptor.performer)
, _seed(descriptor.seed)
, _ticker([=] {
	update(waveRect());
	return _backdrop->playing && isVisible();
}) {
	setMouseTracking(true);
	setButtons(false, false, true);
}

void GlassPlayer::Header::setButtons(
		bool language,
		bool text,
		bool textActive) {
	_textActive = textActive;
	auto types = std::vector<Button>();
	if (language) {
		types.push_back(Button::Language);
	}
	if (text) {
		types.push_back(Button::Text);
	}
	types.push_back(Button::Close);
	auto same = (types.size() == _buttons.size());
	for (auto i = 0; same && i != int(types.size()); ++i) {
		same = (_buttons[i]->type == types[i]);
	}
	if (!same) {
		_buttons.clear();
		for (const auto type : types) {
			_buttons.push_back(std::make_unique<ButtonState>());
			_buttons.back()->type = type;
		}
		_pressed = -1;
		layoutButtons();
	}
	update();
}

void GlassPlayer::Header::setShown(bool shown) {
	if (_shown == shown) {
		return;
	}
	_shown = shown;
	if (shown) {
		show();
	}
	_shownAnimation.start(
		[=] {
			update();
			if (!_shown && !_shownAnimation.animating()) {
				hide();
			}
		},
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		kFadeDuration,
		anim::easeOutCubic);
}

void GlassPlayer::Header::playbackUpdated() {
	if (_backdrop->playing && !_ticker.animating() && isVisible()) {
		_ticker.start();
	}
	update(waveRect());
}

int GlassPlayer::Header::countHeight() const {
	const auto &padding = st::flashgramGlassPadding;
	const auto text = st::flashgramGlassTitleFont->height
		+ st::flashgramGlassSubtitleFont->height;
	const auto pill = st::flashgramGlassButtonSize
		+ 2 * st::flashgramGlassPillPadding;
	return 2 * st::flashgramGlassShadow
		+ padding.top()
		+ std::max(text, pill)
		+ st::flashgramGlassWaveSkip
		+ st::flashgramGlassWaveHeight
		+ padding.bottom();
}

QRect GlassPlayer::Header::glassRect() const {
	const auto shadow = st::flashgramGlassShadow;
	return rect().marginsRemoved({ shadow, shadow, shadow, shadow });
}

QRect GlassPlayer::Header::innerRect() const {
	return glassRect().marginsRemoved(st::flashgramGlassPadding);
}

QRect GlassPlayer::Header::pillRect() const {
	const auto inner = innerRect();
	const auto count = int(_buttons.size());
	const auto size = st::flashgramGlassButtonSize;
	const auto padding = st::flashgramGlassPillPadding;
	const auto width = count * size
		+ std::max(count - 1, 0) * st::flashgramGlassButtonSkip
		+ 2 * padding;
	return QRect(
		inner.x() + inner.width() - width,
		inner.y(),
		width,
		size + 2 * padding);
}

QRect GlassPlayer::Header::waveRect() const {
	const auto inner = innerRect();
	const auto height = st::flashgramGlassWaveHeight;
	return QRect(
		inner.x(),
		inner.y() + inner.height() - height,
		inner.width(),
		height);
}

void GlassPlayer::Header::layoutButtons() {
	const auto pill = pillRect();
	const auto size = st::flashgramGlassButtonSize;
	auto x = pill.x() + st::flashgramGlassPillPadding;
	for (const auto &button : _buttons) {
		button->rect = QRect(
			x,
			pill.y() + st::flashgramGlassPillPadding,
			size,
			size);
		x += size + st::flashgramGlassButtonSkip;
	}
}

void GlassPlayer::Header::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto opacity = _shownAnimation.value(_shown ? 1. : 0.);
	if (opacity <= 0.) {
		return;
	}
	p.setOpacity(opacity);
	layoutButtons();

	const auto glass = glassRect();
	_surface.paint(p, glass, pos(), *_backdrop, st::flashgramGlassRadius, true);

	const auto inner = innerRect();
	const auto pill = pillRect();
	{
		PainterHighQualityEnabler hq(p);
		const auto radius = pill.height() / 2.;
		p.setPen(QPen(QColor(255, 255, 255, 34), 1.));
		p.setBrush(QColor(255, 255, 255, 22));
		p.drawRoundedRect(QRectF(pill).adjusted(.5, .5, -.5, -.5), radius, radius);
	}
	for (const auto &button : _buttons) {
		paintButton(p, *button);
	}

	const auto textWidth = pill.x() - inner.x() - st::flashgramGlassWaveSkip;
	const auto titleFont = st::flashgramGlassTitleFont;
	const auto subtitleFont = st::flashgramGlassSubtitleFont;
	const auto textHeight = titleFont->height
		+ (_performer.isEmpty() ? 0 : subtitleFont->height);
	auto top = inner.y() + std::max((pill.height() - textHeight) / 2, 0);
	p.setFont(titleFont);
	p.setPen(QColor(255, 255, 255));
	p.drawText(
		QRect(inner.x(), top, textWidth, titleFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		titleFont->elided(_title, textWidth));
	top += titleFont->height;
	if (!_performer.isEmpty()) {
		p.setFont(subtitleFont);
		p.setPen(QColor(255, 255, 255, 140));
		p.drawText(
			QRect(inner.x(), top, textWidth, subtitleFont->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			subtitleFont->elided(_performer, textWidth));
	}
	paintWave(p, waveRect());
}

void GlassPlayer::Header::paintButton(
		QPainter &p,
		const ButtonState &button) {
	auto index = -1;
	for (auto i = 0; i != int(_buttons.size()); ++i) {
		if (_buttons[i].get() == &button) {
			index = i;
		}
	}
	const auto hover = button.hover.value(button.over ? 1. : 0.);
	const auto pressed = (_pressed == index);
	const auto scale = pressed ? (1. - 0.08 * _press.value(1.)) : 1.;
	const auto rect = QRectF(button.rect);
	const auto center = rect.center();

	PainterHighQualityEnabler hq(p);
	p.save();
	p.translate(center);
	p.scale(scale, scale);
	p.translate(-center);
	const auto active = (button.type == Button::Text) && _textActive;
	const auto fill = int(std::round(hover * 38 + (pressed ? 24 : 0)))
		+ (active ? 34 : 0);
	if (fill > 0) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, std::min(fill, 110)));
		p.drawEllipse(rect);
	}
	const auto alpha = (button.type == Button::Text && !_textActive)
		? 150
		: 235;
	auto pen = QPen(QColor(255, 255, 255, alpha));
	pen.setWidthF(st::flashgramGlassButtonSize / 16.);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const auto unit = rect.width() / 36.;
	const auto icon = QRectF(
		center.x() - 9 * unit,
		center.y() - 9 * unit,
		18 * unit,
		18 * unit);
	switch (button.type) {
	case Button::Close:
		p.drawLine(icon.topLeft() + QPointF(2 * unit, 2 * unit),
			icon.bottomRight() - QPointF(2 * unit, 2 * unit));
		p.drawLine(icon.topRight() + QPointF(-2 * unit, 2 * unit),
			icon.bottomLeft() + QPointF(2 * unit, -2 * unit));
		break;
	case Button::Language:
		p.drawEllipse(icon);
		p.drawEllipse(icon.adjusted(4.5 * unit, 0, -4.5 * unit, 0));
		p.drawLine(
			QPointF(icon.left(), center.y()),
			QPointF(icon.right(), center.y()));
		break;
	case Button::Text: {
		p.drawRoundedRect(icon, 4 * unit, 4 * unit);
		auto font = st::flashgramGlassTimeFont->f;
		font.setPixelSize(std::max(int(std::round(11 * unit)), 6));
		p.setFont(font);
		p.drawText(icon, Qt::AlignCenter, u"A"_q);
	} break;
	}
	p.restore();
}

void GlassPlayer::Header::paintWave(QPainter &p, QRect rect) {
	const auto step = st::flashgramGlassWaveStep;
	const auto bar = st::flashgramGlassWaveBar;
	const auto count = std::max(rect.width() / step, 1);
	const auto left = rect.x() + (rect.width() - (count - 1) * step - bar) / 2;
	const auto position = _backdrop->now();
	const auto length = _backdrop->length;
	const auto progress = (length > 0)
		? std::clamp(float64(position) / length, 0., 1.)
		: 0.;
	const auto played = progress * count;
	const auto playing = _backdrop->playing;
	const auto centerY = rect.y() + rect.height() / 2.;

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != count; ++i) {
		const auto x = left + i * step;
		const auto level = BarLevel(_seed, i);
		if (i < played) {
			const auto pulse = playing
				? (0.72 + 0.28 * std::sin(position / 160. + i * 0.85))
				: 0.85;
			const auto height = std::max(
				float64(bar),
				rect.height() * level * pulse);
			const auto recent = std::clamp(played - i, 0., 6.) / 6.;
			p.setBrush(QColor(255, 255, 255, int(150 + 95 * recent)));
			p.drawRoundedRect(
				QRectF(x, centerY - height / 2., bar, height),
				bar / 2.,
				bar / 2.);
		} else {
			const auto tall = (i - played < 1.) && playing;
			const auto height = tall ? rect.height() * 0.5 : float64(bar);
			p.setBrush(QColor(255, 255, 255, tall ? 200 : 90));
			p.drawRoundedRect(
				QRectF(x, centerY - height / 2., bar, height),
				bar / 2.,
				bar / 2.);
		}
	}
	if (_waveHover && length > 0) {
		const auto x = std::clamp(*_waveHover, rect.left(), rect.right());
		const auto time = FormatTime(
			length * (x - rect.x()) / std::max(rect.width(), 1));
		const auto font = st::flashgramGlassTimeFont;
		const auto width = font->width(time) + font->height;
		const auto height = font->height + font->height / 3;
		auto bubble = QRectF(
			std::clamp(
				x - width / 2.,
				float64(glassRect().x()),
				float64(glassRect().right() - width)),
			rect.y() - height - 2,
			width,
			height);
		p.setBrush(QColor(0, 0, 0, 150));
		p.setPen(QPen(QColor(255, 255, 255, 60), 1.));
		p.drawRoundedRect(bubble, height / 2., height / 2.);
		p.setPen(QColor(255, 255, 255));
		p.setFont(font);
		p.drawText(bubble, Qt::AlignCenter, time);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 220));
		p.drawRoundedRect(
			QRectF(x - 1, rect.y(), 2, rect.height()),
			1,
			1);
	}
}

void GlassPlayer::Header::updateOver(QPoint point) {
	for (const auto &button : _buttons) {
		const auto over = button->rect.contains(point);
		if (button->over != over) {
			button->over = over;
			button->hover.start(
				[=] { update(); },
				over ? 0. : 1.,
				over ? 1. : 0.,
				kHoverDuration);
		}
	}
	const auto wave = waveRect().marginsAdded({ 0, 4, 0, 4 });
	const auto hover = (wave.contains(point) && _backdrop->length > 0)
		? std::make_optional(point.x())
		: std::nullopt;
	if (_waveHover != hover || _wavePressed) {
		_waveHover = hover;
		update();
	}
	const auto clickable = _waveHover
		|| ranges::any_of(_buttons, [](const auto &button) {
			return button->over;
		});
	setCursor(clickable ? style::cur_pointer : style::cur_default);
}

void GlassPlayer::Header::mouseMoveEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_wavePressed) {
		_waveHover = e->pos().x();
		update();
	}
}

void GlassPlayer::Header::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	updateOver(e->pos());
	_pressed = -1;
	for (auto i = 0; i != int(_buttons.size()); ++i) {
		if (_buttons[i]->over) {
			_pressed = i;
			_press.start([=] { update(); }, 0., 1., kHoverDuration);
			return;
		}
	}
	_wavePressed = _waveHover.has_value();
}

void GlassPlayer::Header::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	update();
	if (std::exchange(_wavePressed, false)) {
		const auto wave = waveRect();
		const auto x = std::clamp(e->pos().x() - wave.x(), 0, wave.width());
		if (seek && _backdrop->length > 0) {
			seek(_backdrop->length * x / std::max(wave.width(), 1));
		}
		updateOver(e->pos());
		return;
	}
	if (pressed >= 0
		&& pressed < int(_buttons.size())
		&& _buttons[pressed]->rect.contains(e->pos())
		&& clicked) {
		clicked(_buttons[pressed]->type);
	}
}

void GlassPlayer::Header::leaveEventHook(QEvent *e) {
	updateOver(QPoint(-1, -1));
}

class GlassPlayer::Lyrics final : public Ui::RpWidget {
public:
	Lyrics(QWidget *parent, std::shared_ptr<Backdrop> backdrop);

	void setLines(std::vector<TimedLine> lines);
	[[nodiscard]] bool empty() const;
	void playbackUpdated();

	Fn<void(crl::time)> seek;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Layout {
		int top = 0;
		int height = 0;
		int width = 0;
	};

	[[nodiscard]] QRect glassRect() const;
	[[nodiscard]] QRect innerRect() const;
	void relayout();
	[[nodiscard]] int anchorFor(crl::time position) const;
	[[nodiscard]] float64 scrollValue() const;
	[[nodiscard]] int lineAt(QPoint point) const;

	const std::shared_ptr<Backdrop> _backdrop;
	GlassSurface _surface;
	std::vector<TimedLine> _lines;
	std::vector<Layout> _layout;
	std::vector<QRect> _hitRects;
	QImage _buffer;
	int _anchor = -1;
	float64 _scrollFrom = 0.;
	float64 _scrollTo = 0.;
	Ui::Animations::Simple _scroll;
	Ui::Animations::Basic _ticker;

};

GlassPlayer::Lyrics::Lyrics(
	QWidget *parent,
	std::shared_ptr<Backdrop> backdrop)
: RpWidget(parent)
, _backdrop(std::move(backdrop))
, _ticker([=] {
	playbackUpdated();
	return _backdrop->playing && isVisible();
}) {
	setMouseTracking(true);
}

void GlassPlayer::Lyrics::setLines(std::vector<TimedLine> lines) {
	_lines = std::move(lines);
	_anchor = -1;
	_scroll.stop();
	relayout();
	update();
}

bool GlassPlayer::Lyrics::empty() const {
	return _lines.empty();
}

QRect GlassPlayer::Lyrics::glassRect() const {
	const auto shadow = st::flashgramGlassShadow;
	return rect().marginsRemoved({ shadow, shadow, shadow, shadow });
}

QRect GlassPlayer::Lyrics::innerRect() const {
	return glassRect().marginsRemoved(st::flashgramGlassLyricsPadding);
}

void GlassPlayer::Lyrics::resizeEvent(QResizeEvent *e) {
	relayout();
}

void GlassPlayer::Lyrics::relayout() {
	_layout.clear();
	const auto width = innerRect().width();
	if (width <= 0) {
		return;
	}
	const auto metrics = QFontMetrics(st::flashgramGlassLyricsFont->f);
	auto top = 0;
	for (const auto &line : _lines) {
		const auto bounds = metrics.boundingRect(
			QRect(0, 0, width, INT_MAX / 4),
			Qt::AlignHCenter | Qt::TextWordWrap,
			line.text);
		_layout.push_back({
			.top = top,
			.height = bounds.height(),
			.width = std::min(bounds.width(), width),
		});
		top += bounds.height()
			+ st::flashgramGlassProgressSkip
			+ st::flashgramGlassProgressHeight
			+ st::flashgramGlassLyricsSkip;
	}
	if (_anchor >= 0 && _anchor < int(_layout.size())) {
		const auto &layout = _layout[_anchor];
		_scrollFrom = _scrollTo = layout.top + layout.height / 2.;
		_scroll.stop();
	}
}

int GlassPlayer::Lyrics::anchorFor(crl::time position) const {
	const auto i = ranges::upper_bound(
		_lines,
		position,
		ranges::less(),
		&TimedLine::from);
	return int(i - begin(_lines)) - 1;
}

float64 GlassPlayer::Lyrics::scrollValue() const {
	const auto progress = _scroll.value(1.);
	return _scrollFrom + (_scrollTo - _scrollFrom) * progress;
}

void GlassPlayer::Lyrics::playbackUpdated() {
	if (_lines.empty() || _layout.size() != _lines.size()) {
		return;
	}
	const auto anchor = std::max(anchorFor(_backdrop->now()), 0);
	if (anchor != _anchor) {
		const auto &layout = _layout[anchor];
		const auto target = layout.top + layout.height / 2.;
		if (_anchor < 0) {
			_scrollFrom = _scrollTo = target;
		} else {
			_scrollFrom = scrollValue();
			_scrollTo = target;
			_scroll.start(
				[=] { update(); },
				0.,
				1.,
				kScrollDuration,
				anim::easeOutCubic);
		}
		_anchor = anchor;
	}
	if (_backdrop->playing && !_ticker.animating() && isVisible()) {
		_ticker.start();
	}
	update();
}

void GlassPlayer::Lyrics::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto glass = glassRect();
	_surface.paint(
		p,
		glass,
		pos(),
		*_backdrop,
		st::flashgramGlassRadius,
		true);
	_hitRects.assign(_lines.size(), QRect());
	if (_lines.empty() || _layout.size() != _lines.size()) {
		return;
	}
	const auto inner = innerRect();
	const auto ratio = devicePixelRatioF();
	const auto size = glass.size() * ratio;
	if (_buffer.size() != size) {
		_buffer = QImage(size, QImage::Format_ARGB32_Premultiplied);
	}
	_buffer.setDevicePixelRatio(ratio);
	_buffer.fill(Qt::transparent);

	const auto position = _backdrop->now();
	const auto anchor = std::max(_anchor, 0);
	const auto &current = _lines[anchor];
	const auto active = (position >= current.from)
		&& (position < current.till);
	const auto appear = _scroll.value(1.);
	const auto center = inner.y() + inner.height() * 0.4;
	const auto scroll = scrollValue();
	const auto skipTop = inner.y() - glass.y();
	{
		auto q = QPainter(&_buffer);
		PainterHighQualityEnabler hq(q);
		q.translate(-glass.topLeft());
		q.setFont(st::flashgramGlassLyricsFont->f);
		for (auto i = 0; i != int(_lines.size()); ++i) {
			const auto &layout = _layout[i];
			const auto top = center - scroll + layout.top;
			const auto bottom = top + layout.height;
			if (bottom < glass.y() || top > glass.y() + glass.height()) {
				continue;
			}
			const auto distance = i - anchor;
			const auto isCurrent = (distance == 0);
			auto opacity = 0.;
			if (isCurrent) {
				opacity = active ? (0.55 + 0.45 * appear) : 0.6;
			} else if (distance > 0) {
				opacity = std::max(0.42 - 0.08 * (distance - 1), 0.16);
			} else {
				opacity = std::max(0.26 + 0.06 * (distance + 1), 0.08);
			}
			const auto rect = QRectF(
				inner.x(),
				top,
				inner.width(),
				layout.height);
			q.save();
			if (isCurrent && active) {
				const auto scale = 0.95 + 0.05 * appear;
				const auto middle = rect.center();
				q.translate(middle);
				q.scale(scale, scale);
				q.translate(-middle);
			}
			q.setOpacity(opacity);
			q.setPen(isCurrent
				? QColor(255, 255, 255)
				: QColor(214, 216, 222));
			q.drawText(
				rect,
				Qt::AlignHCenter | Qt::TextWordWrap,
				_lines[i].text);
			q.restore();
			_hitRects[i] = QRect(
				inner.x() + (inner.width() - layout.width) / 2,
				int(top),
				layout.width,
				layout.height);

			if (isCurrent && active) {
				const auto line = QRectF(
					inner.x() + (inner.width() - layout.width) / 2.,
					bottom + st::flashgramGlassProgressSkip,
					layout.width,
					st::flashgramGlassProgressHeight);
				const auto progress = std::clamp(
					float64(position - current.from)
						/ std::max(current.till - current.from, crl::time(1)),
					0.,
					1.);
				const auto radius = line.height() / 2.;
				q.setOpacity(appear);
				q.setPen(Qt::NoPen);
				q.setBrush(QColor(255, 255, 255, 60));
				q.drawRoundedRect(line, radius, radius);
				q.setBrush(QColor(255, 255, 255, 235));
				q.drawRoundedRect(
					QRectF(
						line.x(),
						line.y(),
						std::max(line.width() * progress, line.height()),
						line.height()),
					radius,
					radius);
			}
		}

		// Past and far lines fade out at the card edges.
		q.resetTransform();
		q.setOpacity(1.);
		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		auto mask = QLinearGradient(0, 0, 0, glass.height());
		const auto edge = std::min(
			float64(skipTop * 2) / std::max(glass.height(), 1),
			0.3);
		mask.setColorAt(0., QColor(0, 0, 0, 0));
		mask.setColorAt(edge, QColor(0, 0, 0, 255));
		mask.setColorAt(1. - edge, QColor(0, 0, 0, 255));
		mask.setColorAt(1., QColor(0, 0, 0, 0));
		q.fillRect(QRect(QPoint(), glass.size()), mask);
	}
	PainterHighQualityEnabler hq(p);
	p.setClipPath(RoundedPath(glass, st::flashgramGlassRadius));
	p.drawImage(glass.topLeft(), _buffer);
}

int GlassPlayer::Lyrics::lineAt(QPoint point) const {
	if (!glassRect().contains(point)) {
		return -1;
	}
	for (auto i = 0; i != int(_hitRects.size()); ++i) {
		if (_hitRects[i].contains(point)) {
			return i;
		}
	}
	return -1;
}

void GlassPlayer::Lyrics::mouseMoveEvent(QMouseEvent *e) {
	setCursor((lineAt(e->pos()) >= 0)
		? style::cur_pointer
		: style::cur_default);
}

void GlassPlayer::Lyrics::mouseReleaseEvent(QMouseEvent *e) {
	const auto index = lineAt(e->pos());
	if (e->button() == Qt::LeftButton
		&& index >= 0
		&& index < int(_lines.size())
		&& seek) {
		seek(_lines[index].from);
	}
}

class GlassPlayer::Surface final {
public:
	GlassSurface surface;
};

GlassPlayer::GlassPlayer(Descriptor &&descriptor)
: _descriptor(std::move(descriptor))
, _backdrop(std::make_shared<Backdrop>())
, _controlsSurface(std::make_unique<Surface>()) {
	const auto parent = _descriptor.parent.get();
	_header = std::make_unique<Header>(parent, _backdrop, _descriptor);
	_header->clicked = [=](Header::Button button) {
		switch (button) {
		case Header::Button::Close:
			if (const auto close = _descriptor.close) {
				crl::on_main(_header.get(), close);
			}
			break;
		case Header::Button::Text: toggleLyrics(); break;
		case Header::Button::Language: nextTrack(); break;
		}
	};
	_header->seek = _descriptor.seek;
	_header->show();

	_lyrics = std::make_unique<Lyrics>(parent, _backdrop);
	_lyrics->seek = _descriptor.seek;
	_lyrics->hide();

	const auto path = base::take(_descriptor.mediaPath);
	setMediaPath(path);
}

GlassPlayer::~GlassPlayer() = default;

void GlassPlayer::setMediaPath(const QString &path) {
	if (path.isEmpty() || path == _descriptor.mediaPath) {
		return;
	}
	_descriptor.mediaPath = path;
	_tracks = FindTimedTextFiles(path);
	_trackIndex = -1;
	for (auto i = 0; i != int(_tracks.size()); ++i) {
		loadTrack(i);
		if (!_lyrics->empty()) {
			break;
		}
	}
	refreshButtons();
	updateGeometry(_width, _top, _bottom);
	notifyLayout();
}

void GlassPlayer::notifyLayout() {
	if (const auto changed = _descriptor.layoutChanged) {
		changed();
	}
}

void GlassPlayer::refreshButtons() {
	_header->setButtons(
		_tracks.size() > 1,
		!_lyrics->empty(),
		_lyricsEnabled);
}

void GlassPlayer::loadTrack(int index) {
	auto file = QFile(_tracks[index]);
	auto lines = std::vector<TimedLine>();
	if (file.size() <= kMaxTimedTextSize && file.open(QIODevice::ReadOnly)) {
		lines = ParseTimedText(file.readAll());
	}
	_trackIndex = index;
	_lyrics->setLines(std::move(lines));
}

void GlassPlayer::toggleLyrics() {
	_lyricsEnabled = !_lyricsEnabled;
	refreshButtons();
	updateGeometry(_width, _top, _bottom);
	notifyLayout();
}

void GlassPlayer::nextTrack() {
	if (_tracks.size() < 2) {
		return;
	}
	const auto start = _trackIndex;
	for (auto step = 1; step <= int(_tracks.size()); ++step) {
		loadTrack((start + step) % int(_tracks.size()));
		if (!_lyrics->empty()) {
			break;
		}
	}
	updateGeometry(_width, _top, _bottom);
	notifyLayout();
}

void GlassPlayer::refreshBackdrop(bool force) {
	const auto now = crl::now();
	const auto delay = _backdrop->blurred.isNull()
		? kBackdropRetry
		: kBackdropRefresh;
	if (!force && _backdropUpdated && now - _backdropUpdated < delay) {
		return;
	}
	_backdropUpdated = now;
	const auto content = _descriptor.contentRect
		? _descriptor.contentRect()
		: QRect();
	auto frame = _descriptor.frame ? _descriptor.frame() : QImage();
	if (frame.isNull() || frame.width() <= 0 || frame.height() <= 0) {
		if (_backdrop->content != content) {
			_backdrop->content = content;
			++_backdrop->generation;
		}
		return;
	}
	const auto height = std::clamp(
		kBackdropWidth * frame.height() / frame.width(),
		1,
		kBackdropWidth * 4);
	auto small = frame.scaled(
		kBackdropWidth,
		height,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation
	).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	frame = QImage();
	const auto average = QColor::fromRgba(small.scaled(
		1,
		1,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation).pixel(0, 0));
	auto hue = 0., saturation = 0., value = 0.;
	average.getHsvF(&hue, &saturation, &value);
	_backdrop->tint = QColor::fromHsvF(
		std::max(hue, 0.),
		std::min(saturation, 0.55),
		std::clamp(value, 0.16, 0.34));
	_backdrop->blurred = Images::BlurLargeImage(std::move(small), 3);
	_backdrop->content = content;
	++_backdrop->generation;
	_header->update();
	_lyrics->update();
}

void GlassPlayer::updatePlayback(
		crl::time position,
		crl::time length,
		bool playing) {
	_backdrop->position = std::max(position, crl::time(0));
	_backdrop->length = std::max(length, crl::time(0));
	_backdrop->playing = playing;
	_backdrop->stamp = crl::now();
	refreshBackdrop(false);
	_header->playbackUpdated();
	if (_lyrics->isVisible()) {
		_lyrics->playbackUpdated();
	}
}

void GlassPlayer::setControlsShown(bool shown) {
	_controlsShown = shown;
	_header->setShown(shown);
}

void GlassPlayer::updateGeometry(int width, int top, int bottom) {
	_width = width;
	_top = top;
	_bottom = bottom;
	const auto shadow = st::flashgramGlassShadow;
	const auto gutter = st::flashgramGlassLyricsGap;
	const auto glassWidth = std::min(
		st::flashgramGlassMaxWidth,
		width - 2 * gutter);
	if (glassWidth < st::flashgramGlassButtonSize * 6) {
		_header->hide();
		_lyrics->hide();
		return;
	}
	const auto full = glassWidth + 2 * shadow;
	const auto left = (width - full) / 2;
	const auto headerHeight = _header->countHeight();
	_header->setGeometry(left, top - shadow, full, headerHeight);
	if (_controlsShown) {
		_header->show();
	}
	const auto headerBottom = top + headerHeight - 2 * shadow;

	const auto available = bottom - headerBottom - 2 * gutter;
	const auto height = std::min(
		st::flashgramGlassLyricsMaxHeight,
		available);
	const auto showLyrics = _lyricsEnabled
		&& !_lyrics->empty()
		&& (height >= st::flashgramGlassLyricsMinHeight);
	if (showLyrics) {
		_lyrics->setGeometry(
			left,
			bottom - gutter - height - shadow,
			full,
			height + 2 * shadow);
		_lyrics->show();
		_lyrics->raise();
		_lyrics->playbackUpdated();
	} else {
		_lyrics->hide();
	}
	_header->raise();
	refreshBackdrop(true);
}

bool GlassPlayer::lyricsShown() const {
	return !_lyrics->isHidden();
}

int GlassPlayer::lyricsTop() const {
	return lyricsShown()
		? (_lyrics->y() + st::flashgramGlassShadow)
		: _bottom;
}

void GlassPlayer::paintGlass(
		QPainter &p,
		QRect local,
		QPoint topLeftInParent) {
	_controlsSurface->surface.paint(
		p,
		local,
		topLeftInParent,
		*_backdrop,
		std::min(local.height() / 2., float64(st::flashgramGlassRadius)),
		false);
}

} // namespace FlashGram
