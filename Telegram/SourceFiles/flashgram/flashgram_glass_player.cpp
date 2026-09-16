/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_glass_player.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "flashgram/flashgram_lyrics.h"
#include "flashgram/flashgram_music_player.h"
#include "flashgram/flashgram_state.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/image/image.h"
#include "ui/text/format_song_document_name.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"

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
#include <QtGui/QFontDatabase>
#include <QtGui/QTextLayout>

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

// Flowing, iridescent light band for hovered liquid buttons.
void PaintLiquidSheen(
		QPainter &p,
		const QPainterPath &clip,
		QRectF r,
		float64 strength,
		bool onLight = false) {
	if (strength <= 0.) {
		return;
	}
	const auto t = float64(crl::now() % 100000000) / 1000.;
	p.save();
	p.setClipPath(clip, Qt::IntersectClip);
	p.setPen(Qt::NoPen);
	const auto alpha = [&](int value) {
		return std::clamp(int(value * strength), 0, 255);
	};
	const auto period = 1.6;
	const auto phase = std::fmod(t, period) / period;
	const auto band = std::max(r.width() * 0.9, 30.);
	const auto x = r.left() - band + (r.width() + 2 * band) * phase;
	auto sheen = QLinearGradient(
		QPointF(x, r.top()),
		QPointF(x + band, r.bottom()));
	sheen.setColorAt(0., QColor(255, 255, 255, 0));
	if (onLight) {
		sheen.setColorAt(0.35, QColor(255, 150, 60, alpha(70)));
		sheen.setColorAt(0.5, QColor(255, 196, 120, alpha(110)));
		sheen.setColorAt(0.65, QColor(255, 110, 150, alpha(60)));
	} else {
		sheen.setColorAt(0.35, QColor(255, 176, 96, alpha(60)));
		sheen.setColorAt(0.5, QColor(255, 246, 232, alpha(120)));
		sheen.setColorAt(0.65, QColor(255, 120, 160, alpha(50)));
	}
	sheen.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillRect(r, sheen);

	// A slowly turning caustic spot, like light under water.
	const auto spot = QPointF(
		r.center().x() + r.width() * 0.3 * std::sin(t * 2.3),
		r.center().y() + r.height() * 0.3 * std::cos(t * 1.9));
	auto caustic = QRadialGradient(spot, std::max(r.width(), r.height()) * 0.6);
	caustic.setColorAt(0., onLight
		? QColor(255, 170, 90, alpha(60))
		: QColor(255, 255, 255, alpha(50)));
	caustic.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillRect(r, caustic);
	p.restore();
}

} // namespace

QColor LiquidAccent() {
	return QColor(255, 140, 40);
}

QColor LiquidTint(const QColor &average, float64 orange) {
	auto hue = 0., saturation = 0., value = 0.;
	average.getHsvF(&hue, &saturation, &value);
	const auto base = QColor::fromHsvF(
		std::max(hue, 0.),
		std::min(saturation, 0.55),
		std::clamp(value, 0.16, 0.34));
	const auto accent = QColor::fromHsvF(26. / 360., 0.78, 0.30);
	const auto mix = [&](int a, int b) {
		return int(std::round(a + (b - a) * orange));
	};
	return QColor(
		mix(base.red(), accent.red()),
		mix(base.green(), accent.green()),
		mix(base.blue(), accent.blue()));
}

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
	QColor tint = QColor(70, 38, 16);
	QRect content;
	int generation = 0;
	crl::time position = 0;
	crl::time length = 0;
	crl::time stamp = 0;
	bool playing = false;
	bool liquid = false;

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
		const auto path = RoundedPath(glass, radius);
		p.fillPath(path, sheen);
		if (backdrop.liquid) {
			paintLiquid(p, glass, path);
		}
	}

private:
	void paintLiquid(QPainter &p, QRect glass, const QPainterPath &path) {
		const auto t = float64(crl::now() % 1000000);
		p.save();
		p.setClipPath(path);

		// A caustic light spot slowly flowing inside the glass.
		const auto cx = glass.x()
			+ glass.width() * (0.5 + 0.42 * std::sin(t * 0.00061));
		const auto cy = glass.y()
			+ glass.height() * (0.5 + 0.45 * std::cos(t * 0.00083));
		auto caustic = QRadialGradient(
			QPointF(cx, cy),
			std::max(glass.height() * 1.3, 40.));
		caustic.setColorAt(0., QColor(255, 236, 214, 34));
		caustic.setColorAt(1., QColor(255, 236, 214, 0));
		p.fillRect(glass, caustic);

		// Warm reflection rising from the bottom edge.
		auto warm = QLinearGradient(glass.bottomLeft(), glass.topLeft());
		warm.setColorAt(0., QColor(255, 140, 40, 40));
		warm.setColorAt(0.35, QColor(255, 140, 40, 0));
		p.fillRect(glass, warm);

		// Wavy specular edge, like the surface of a liquid.
		const auto wave = [&](float64 base, float64 amplitude, float64 speed) {
			auto line = QPainterPath();
			const auto step = 6.;
			for (auto x = float64(glass.x()); x <= glass.right() + step; x += step) {
				const auto y = base
					+ amplitude * std::sin(x * 0.03 + t * speed)
					+ amplitude * 0.5 * std::sin(x * 0.071 - t * speed * 1.7);
				if (x == glass.x()) {
					line.moveTo(x, y);
				} else {
					line.lineTo(x, y);
				}
			}
			return line;
		};
		auto edge = QLinearGradient(glass.topLeft(), glass.topRight());
		edge.setColorAt(0., QColor(255, 255, 255, 30));
		edge.setColorAt(0.5, QColor(255, 255, 255, 120));
		edge.setColorAt(1., QColor(255, 255, 255, 30));
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QBrush(edge), 1.6));
		p.drawPath(wave(glass.y() + 3., 1.6, 0.0024));
		p.setPen(QPen(QColor(255, 255, 255, 26), 1.));
		p.drawPath(wave(glass.y() + 8., 2.4, -0.0017));
		p.restore();
	}

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
	void setTexts(QString title, QString performer, uint64 seed);
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
	QString _title;
	QString _performer;
	uint64 _seed = 0;
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
	update();
	const auto hovered = ranges::any_of(_buttons, [](const auto &button) {
		return button->over || button->hover.animating();
	});
	return (_backdrop->playing || hovered) && isVisible();
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

void GlassPlayer::Header::setTexts(
		QString title,
		QString performer,
		uint64 seed) {
	_title = std::move(title);
	_performer = std::move(performer);
	_seed = seed;
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
	if (hover > 0.) {
		auto clip = QPainterPath();
		clip.addEllipse(rect);
		PaintLiquidSheen(p, clip, rect, hover);
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
			if (over && !_ticker.animating()) {
				_ticker.start();
			}
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

	void setLines(std::vector<TimedLine> lines, bool synced = true);
	void setCard(bool card);
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
	[[nodiscard]] QFont font() const;
	void relayout();
	[[nodiscard]] int anchorFor(crl::time position) const;
	[[nodiscard]] float64 scrollValue() const;
	[[nodiscard]] int lineAt(QPoint point) const;

	void paintGlow(QPainter &q, int index, QPointF topLeft);

	const std::shared_ptr<Backdrop> _backdrop;
	GlassSurface _surface;
	std::vector<TimedLine> _lines;
	std::vector<Layout> _layout;
	std::vector<std::unique_ptr<QTextLayout>> _texts;
	QImage _glow;
	int _glowIndex = -1;
	int _glowWidth = 0;
	std::vector<QRect> _hitRects;
	QImage _buffer;
	bool _synced = true;
	bool _card = true;
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

void GlassPlayer::Lyrics::setCard(bool card) {
	_card = card;
	relayout();
	update();
}

void GlassPlayer::Lyrics::setLines(
		std::vector<TimedLine> lines,
		bool synced) {
	_lines = std::move(lines);
	_synced = synced;
	_anchor = -1;
	_scroll.stop();
	relayout();
	update();
}

bool GlassPlayer::Lyrics::empty() const {
	return _lines.empty();
}

QRect GlassPlayer::Lyrics::glassRect() const {
	if (!_card) {
		return rect();
	}
	const auto shadow = st::flashgramGlassShadow;
	return rect().marginsRemoved({ shadow, shadow, shadow, shadow });
}

QRect GlassPlayer::Lyrics::innerRect() const {
	return glassRect().marginsRemoved(_card
		? st::flashgramGlassLyricsPadding
		: st::flashgramMusicLyricsPadding);
}

QFont GlassPlayer::Lyrics::font() const {
	return _card
		? st::flashgramGlassLyricsFont->f
		: st::flashgramMusicLyricsFont->f;
}

void GlassPlayer::Lyrics::resizeEvent(QResizeEvent *e) {
	relayout();
}

void GlassPlayer::Lyrics::relayout() {
	_layout.clear();
	_texts.clear();
	_glowIndex = -1;
	const auto width = innerRect().width();
	if (width <= 0) {
		return;
	}
	const auto textFont = font();
	auto option = QTextOption(Qt::AlignHCenter);
	option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	auto top = 0;
	for (const auto &line : _lines) {
		auto text = std::make_unique<QTextLayout>(line.text, textFont);
		text->setTextOption(option);
		text->beginLayout();
		auto height = 0.;
		auto widest = 0.;
		while (true) {
			auto row = text->createLine();
			if (!row.isValid()) {
				break;
			}
			row.setLineWidth(width);
			row.setPosition(QPointF(0., height));
			height += row.height();
			widest = std::max(widest, row.naturalTextWidth());
		}
		text->endLayout();
		const auto rounded = int(std::ceil(height));
		_layout.push_back({
			.top = top,
			.height = rounded,
			.width = std::min(int(std::ceil(widest)), width),
		});
		_texts.push_back(std::move(text));
		top += rounded
			+ st::flashgramGlassProgressSkip
			+ st::flashgramGlassProgressHeight
			+ (_card
				? st::flashgramGlassLyricsSkip
				: st::flashgramMusicLyricsSkip);
	}
	if (_anchor >= 0 && _anchor < int(_layout.size())) {
		const auto &layout = _layout[_anchor];
		_scrollFrom = _scrollTo = layout.top + layout.height / 2.;
		_scroll.stop();
	}
}

void GlassPlayer::Lyrics::paintGlow(
		QPainter &q,
		int index,
		QPointF topLeft) {
	const auto width = innerRect().width();
	if (_glowIndex != index || _glowWidth != width) {
		_glowIndex = index;
		_glowWidth = width;
		const auto &layout = _layout[index];
		const auto margin = 24;
		const auto ratio = devicePixelRatioF();
		auto image = QImage(
			QSize(width + 2 * margin, layout.height + 2 * margin) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		{
			auto g = QPainter(&image);
			g.setRenderHint(QPainter::Antialiasing);
			g.setPen(QColor(255, 222, 186));
			_texts[index]->draw(&g, QPointF(margin, margin));
		}
		_glow = Images::BlurLargeImage(
			std::move(image),
			int(std::round(10 * ratio)));
		_glow.setDevicePixelRatio(ratio);
	}
	const auto margin = 24;
	q.drawImage(
		QRectF(
			topLeft.x() - margin,
			topLeft.y() - margin,
			width + 2 * margin,
			_layout[index].height + 2 * margin),
		_glow);
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
	const auto position = _backdrop->now();
	const auto anchor = _synced
		? std::max(anchorFor(position), 0)
		: std::clamp(
			int(float64(position)
				/ std::max(_backdrop->length, crl::time(1))
				* _lines.size()),
			0,
			int(_lines.size()) - 1);
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
	if (_card) {
		_surface.paint(
			p,
			glass,
			pos(),
			*_backdrop,
			st::flashgramGlassRadius,
			true);
	}
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
	const auto active = _synced
		&& (position >= current.from)
		&& (position < current.till);
	const auto appear = std::clamp(_scroll.value(1.), 0., 1.);
	const auto center = inner.y() + inner.height() * 0.4;
	const auto scroll = scrollValue();
	const auto skipTop = inner.y() - glass.y();
	{
		auto q = QPainter(&_buffer);
		PainterHighQualityEnabler hq(q);
		q.translate(-glass.topLeft());
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
			if (!_synced) {
				opacity = std::max(0.9 - 0.12 * std::abs(distance), 0.4);
			} else if (isCurrent) {
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
			const auto &text = _texts[i];
			const auto origin = rect.topLeft();
			q.setOpacity(std::clamp(opacity, 0., 1.));
			if (!_card) {
				// Soft shadow keeps bold text readable on bright liquid.
				q.setPen(QColor(0, 0, 0, 90));
				text->draw(&q, origin + QPointF(0., 2.));
			}
			if (!_card && isCurrent && active) {
				// Warm light slowly flowing through the current line.
				const auto t = float64(crl::now() % 100000000) / 1000.;
				const auto shift = std::fmod(t * 0.35, 1.) * 2. - 0.5;
				const auto from = rect.left() + rect.width() * shift;
				auto flow = QLinearGradient(
					QPointF(from, rect.top()),
					QPointF(from + rect.width(), rect.top()));
				flow.setSpread(QGradient::ReflectSpread);
				flow.setColorAt(0., QColor(255, 255, 255));
				flow.setColorAt(0.5, QColor(255, 226, 196));
				flow.setColorAt(1., QColor(255, 255, 255));
				q.setPen(QPen(QBrush(flow), 1.));
			} else {
				q.setPen((isCurrent || !_synced)
					? QColor(255, 255, 255)
					: QColor(214, 216, 222));
			}
			text->draw(&q, origin);
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
	if (_card) {
		p.setClipPath(RoundedPath(glass, st::flashgramGlassRadius));
	}
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
	_backdrop->tint = LiquidTint(average, 0.45);
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

class MusicPlayer final : public Ui::RpWidget {
public:
	MusicPlayer(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const QString &preview = QString());

	void closeAnimated();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	using Header = GlassPlayer::Header;
	using Lyrics = GlassPlayer::Lyrics;
	using Type = AudioMsgId::Type;

	enum class Control {
		None,
		Previous,
		PlayPause,
		Next,
		Seek,
	};
	enum class LyricsState {
		Loading,
		Found,
		Missing,
	};
	struct ControlState {
		Ui::Animations::Simple hover;
		bool over = false;
	};

	void handleState(const Media::Player::TrackState &state);
	void refreshTrack(const AudioMsgId &id);
	void refreshCover();
	void resolveLyrics();
	void rebuildBackground();
	void renderLiquid(crl::time now);
	void startPreview();
	void updatePreview(crl::time now);
	void cursorMoved(QPointF point);
	void cursorLeft();
	void addRipple(QPointF point, float64 strength);
	void paintWater(QPainter &p, crl::time now);
	bool eventFilter(QObject *object, QEvent *e) override;
	void updateLayout();
	void seekTo(crl::time position);
	void seekBy(crl::time delta);
	void setOver(Control control);
	[[nodiscard]] Control controlAt(QPoint point) const;
	[[nodiscard]] float64 progressAt(int x) const;
	[[nodiscard]] float64 shownProgress() const;
	[[nodiscard]] ControlState &state(Control control);
	void paintSeek(QPainter &p);
	void paintButton(QPainter &p, Control control, QRect rect);
	void paintCoverAndStatus(QPainter &p);

	const not_null<Window::SessionController*> _controller;
	const std::shared_ptr<Backdrop> _backdrop;
	std::unique_ptr<Header> _header;
	std::unique_ptr<Lyrics> _lyrics;

	AudioMsgId _current;
	std::shared_ptr<Data::DocumentMedia> _media;
	QImage _cover;
	QImage _shade;
	QImage _liquid;
	std::array<QColor, 5> _palette;
	bool _backgroundDirty = true;
	crl::time _liquidRendered = 0;
	crl::time _backdropPushed = 0;
	Ui::Animations::Basic _liquidTicker;
	rpl::lifetime _coverLifetime;

	LyricsState _lyricsState = LyricsState::Loading;
	bool _lyricsEnabled = true;
	int _lyricsRequest = 0;

	QRect _column;
	QRect _lyricsArea;
	QRect _seekRect;
	QRect _previousRect;
	QRect _playRect;
	QRect _nextRect;

	bool _playing = false;
	Control _over = Control::None;
	Control _pressed = Control::None;
	ControlState _previousState;
	ControlState _playState;
	ControlState _nextState;
	ControlState _seekState;
	bool _seeking = false;
	float64 _seekProgress = 0.;
	std::optional<int> _seekHover;

	Ui::Animations::Simple _shown;
	bool _closing = false;

	struct Ripple {
		QPointF center;
		crl::time start = 0;
		float64 strength = 1.;
	};
	std::vector<Ripple> _ripples;
	QPointF _cursor;
	QPointF _cursorSmooth;
	QPointF _lastRipplePosition;
	crl::time _lastRipple = 0;
	bool _cursorInside = false;
	Ui::Animations::Simple _cursorFade;

	// Developer preview without a real track (FLASHGRAM_PLAYER_PREVIEW).
	const QString _preview;
	crl::time _previewStart = 0;

};

MusicPlayer::MusicPlayer(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	const QString &preview)
: RpWidget(parent)
, _controller(controller)
, _backdrop(std::make_shared<Backdrop>())
, _preview(preview) {
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);

	_header = std::make_unique<Header>(
		this,
		_backdrop,
		GlassPlayer::Descriptor{ .parent = this });
	_header->clicked = [=](Header::Button button) {
		if (button == Header::Button::Close) {
			closeAnimated();
		} else if (button == Header::Button::Text) {
			_lyricsEnabled = !_lyricsEnabled;
			_header->setButtons(false, true, _lyricsEnabled);
			updateLayout();
			update();
		}
	};
	_header->seek = [=](crl::time position) { seekTo(position); };
	_header->setButtons(false, true, _lyricsEnabled);
	_header->show();

	_backdrop->liquid = true;
	_liquidTicker.init([=](crl::time now) {
		if (_closing || !isVisible() || window()->isMinimized()) {
			return !_closing;
		}
		if (!_preview.isEmpty()) {
			updatePreview(now);
		}
		const auto kRippleLife = crl::time(1500);
		_ripples.erase(ranges::remove_if(_ripples, [&](const Ripple &r) {
			return now - r.start > kRippleLife;
		}), end(_ripples));
		_cursorSmooth += (_cursor - _cursorSmooth) * 0.08;
		const auto water = !_ripples.empty()
			|| _cursorInside
			|| _cursorFade.animating();
		if (now - _liquidRendered >= 33) {
			renderLiquid(now);
			update();
		} else if (water) {
			update();
		}
		return true;
	});

	_lyrics = std::make_unique<Lyrics>(this, _backdrop);
	_lyrics->setCard(false);
	_lyrics->seek = [=](crl::time position) { seekTo(position); };
	_lyrics->hide();

	// Water follows the cursor above the header and lyrics too.
	_header->installEventFilter(this);
	_lyrics->installEventFilter(this);

	using namespace Media::Player;
	instance()->updatedNotifier(
	) | rpl::filter([](const TrackState &state) {
		return (state.id.type() == Type::Song);
	}) | rpl::on_next([=](const TrackState &state) {
		handleState(state);
	}, lifetime());

	instance()->trackChanged(
	) | rpl::filter([](Type type) {
		return (type == Type::Song);
	}) | rpl::on_next([=] {
		handleState(instance()->getState(Type::Song));
	}, lifetime());

	instance()->closePlayerRequests(
	) | rpl::on_next([=] {
		closeAnimated();
	}, lifetime());

	static_cast<Ui::RpWidget*>(parent)->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto offset = int(std::round(
			(1. - shownProgress()) * size.height() * 0.06));
		setGeometry(0, offset, size.width(), size.height());
	}, lifetime());

	if (_preview.isEmpty()) {
		_current = instance()->current(Type::Song);
		refreshTrack(_current);
		handleState(instance()->getState(Type::Song));
	} else {
		startPreview();
	}

	show();
	raise();
	setFocus();
	_liquidTicker.start();
	_shown.start([=] {
		const auto size = parentWidget()->size();
		const auto offset = int(std::round(
			(1. - shownProgress()) * size.height() * 0.06));
		move(0, offset);
		update();
	}, 0., 1., crl::time(280), anim::easeOutCubic);
}

float64 MusicPlayer::shownProgress() const {
	return _shown.value(_closing ? 0. : 1.);
}

void MusicPlayer::closeAnimated() {
	if (_closing) {
		return;
	}
	_closing = true;
	_shown.start([=] {
		const auto size = parentWidget()->size();
		const auto progress = shownProgress();
		move(0, int(std::round((1. - progress) * size.height() * 0.06)));
		update();
		if (!_shown.animating()) {
			deleteLater();
		}
	}, 1., 0., crl::time(200), anim::easeOutCubic);
}

void MusicPlayer::startPreview() {
	_header->setTexts(u"FlashGram Liquid Glass"_q, u"Preview"_q, 7);
	auto file = QFile(_preview);
	auto lines = file.open(QIODevice::ReadOnly)
		? ParseTimedText(file.readAll())
		: std::vector<TimedLine>();
	_backdrop->length = lines.empty()
		? crl::time(60000)
		: (lines.back().till + crl::time(3000));
	_lyricsState = lines.empty() ? LyricsState::Missing : LyricsState::Found;
	_lyrics->setLines(std::move(lines), true);
	_playing = true;
	_backdrop->playing = true;
	_previewStart = crl::now();
	updateLayout();
}

void MusicPlayer::updatePreview(crl::time now) {
	const auto length = std::max(_backdrop->length, crl::time(1));
	_backdrop->position = (now - _previewStart) % length;
	_backdrop->stamp = now;
	_header->playbackUpdated();
	if (_lyrics->isVisible()) {
		_lyrics->playbackUpdated();
	}
}

void MusicPlayer::refreshTrack(const AudioMsgId &id) {
	_current = id;
	_media = nullptr;
	_cover = QImage();
	_coverLifetime.destroy();
	_backgroundDirty = true;
	const auto document = id.audio();
	if (!document) {
		_header->setTexts(
			Tr("Nothing is playing", "Ничего не играет"),
			QString(),
			0);
		_lyrics->setLines({});
		_lyricsState = LyricsState::Missing;
		updateLayout();
		update();
		return;
	}
	const auto name = Ui::Text::FormatSongNameFor(document).composedName();
	_header->setTexts(name.title, name.performer, document->id);
	_media = document->createMediaView();
	refreshCover();
	if (_cover.isNull() && document->hasThumbnail()) {
		document->loadThumbnail(Data::FileOriginMessage(id.contextId()));
		document->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			refreshCover();
			if (!_cover.isNull()) {
				_coverLifetime.destroy();
			}
		}, _coverLifetime);
	}
	resolveLyrics();
	updateLayout();
	update();
}

void MusicPlayer::refreshCover() {
	const auto image = _media ? _media->thumbnail() : nullptr;
	if (!image || !_cover.isNull()) {
		return;
	}
	_cover = image->original();
	_backgroundDirty = true;
	update();
}

void MusicPlayer::resolveLyrics() {
	const auto document = _current.audio();
	if (!document) {
		return;
	}
	const auto request = ++_lyricsRequest;
	_lyrics->setLines({});
	_lyricsState = LyricsState::Loading;
	const auto name = Ui::Text::FormatSongNameFor(document).composedName();
	const auto song = document->song();
	ResolveLyrics({
		.title = (song && !song->title.isEmpty()) ? song->title : QString(),
		.performer = (song && !song->performer.isEmpty())
			? song->performer
			: (song ? QString() : name.performer),
		.fileName = document->filename(),
		.localPath = document->filepath(true),
		.duration = document->duration(),
	}, crl::guard(this, [=](LyricsResult result) {
		if (request != _lyricsRequest) {
			return;
		}
		_lyricsState = result.lines.empty()
			? LyricsState::Missing
			: LyricsState::Found;
		_lyrics->setLines(std::move(result.lines), result.synced);
		updateLayout();
		update();
	}));
}

void MusicPlayer::handleState(const Media::Player::TrackState &state) {
	if (!_preview.isEmpty()) {
		return;
	}
	if (state.id != _current) {
		refreshTrack(state.id);
	}
	const auto frequency = std::max(state.frequency, 1);
	const auto stoppedAtEnd = Media::Player::IsStoppedAtEnd(state.state);
	const auto length = state.length * crl::time(1000) / frequency;
	const auto position = stoppedAtEnd
		? length
		: Media::Player::IsStoppedOrStopping(state.state)
		? crl::time(0)
		: (state.position * crl::time(1000) / frequency);
	_playing = !Media::Player::IsPausedOrPausing(state.state)
		&& !Media::Player::IsStoppedOrStopping(state.state);
	_backdrop->position = std::max(position, crl::time(0));
	_backdrop->length = std::max(length, crl::time(0));
	_backdrop->playing = _playing;
	_backdrop->stamp = crl::now();
	_header->playbackUpdated();
	if (_lyrics->isVisible()) {
		_lyrics->playbackUpdated();
	}
	update(_seekRect.marginsAdded({ 0, 40, 0, 40 }));
	update(_playRect.united(_previousRect).united(_nextRect));
}

void MusicPlayer::seekTo(crl::time position) {
	const auto length = _backdrop->length;
	if (length <= 0) {
		return;
	} else if (!_preview.isEmpty()) {
		_previewStart = crl::now() - std::clamp(position, crl::time(0), length);
		return;
	}
	const auto progress = std::clamp(float64(position) / length, 0., 1.);
	crl::on_main(this, [=] {
		const auto player = Media::Player::instance();
		player->startSeeking(Type::Song);
		player->finishSeeking(Type::Song, progress);
	});
}

void MusicPlayer::seekBy(crl::time delta) {
	seekTo(std::clamp(
		_backdrop->now() + delta,
		crl::time(0),
		_backdrop->length));
}

void MusicPlayer::resizeEvent(QResizeEvent *e) {
	_backgroundDirty = true;
	updateLayout();
}

void MusicPlayer::updateLayout() {
	const auto gutter = st::flashgramMusicGutter;
	const auto shadow = st::flashgramGlassShadow;
	const auto columnWidth = std::min(
		st::flashgramMusicMaxWidth,
		width() - 2 * gutter);
	_column = QRect(
		(width() - columnWidth) / 2,
		gutter,
		columnWidth,
		height() - 2 * gutter);

	const auto headerHeight = _header->countHeight();
	_header->setGeometry(
		_column.x() - shadow,
		_column.y() - shadow,
		_column.width() + 2 * shadow,
		headerHeight);
	const auto headerBottom = _column.y() + headerHeight - 2 * shadow;

	const auto play = st::flashgramMusicPlayButton;
	const auto button = st::flashgramMusicButton;
	const auto skip = st::flashgramMusicButtonSkip;
	const auto playTop = _column.y() + _column.height() - play;
	const auto center = _column.x() + _column.width() / 2;
	_playRect = QRect(center - play / 2, playTop, play, play);
	const auto buttonTop = playTop + (play - button) / 2;
	_previousRect = QRect(
		_playRect.x() - skip - button,
		buttonTop,
		button,
		button);
	_nextRect = QRect(_playRect.right() + 1 + skip, buttonTop, button, button);

	const auto timeHeight = st::flashgramGlassTimeFont->height;
	const auto seekTop = playTop
		- st::flashgramMusicControlsSkip
		- timeHeight
		- st::flashgramMusicTimeSkip
		- st::flashgramMusicSeekArea;
	_seekRect = QRect(
		_column.x(),
		seekTop,
		_column.width(),
		st::flashgramMusicSeekArea);

	_lyricsArea = QRect(
		_column.x(),
		headerBottom,
		_column.width(),
		std::max(seekTop - headerBottom, 0));
	const auto showLyrics = _lyricsEnabled
		&& (_lyricsState == LyricsState::Found)
		&& !_lyrics->empty()
		&& (_lyricsArea.height() >= st::flashgramGlassLyricsMinHeight);
	if (showLyrics) {
		_lyrics->setGeometry(_lyricsArea);
		_lyrics->show();
		_lyrics->playbackUpdated();
	} else {
		_lyrics->hide();
	}
	_header->raise();
}

void MusicPlayer::rebuildBackground() {
	_backgroundDirty = false;

	// Palette: cover colors pushed towards warm orange.
	const auto accents = std::array<QColor, 5>{
		QColor(255, 140, 40),
		QColor(206, 72, 22),
		QColor(255, 196, 102),
		QColor(126, 42, 12),
		QColor(255, 108, 64),
	};
	auto average = QColor(120, 60, 20);
	if (!_cover.isNull()) {
		const auto grid = _cover.scaled(
			3,
			3,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		const auto points = std::array<QPoint, 5>{
			QPoint(0, 0), QPoint(2, 2), QPoint(1, 1), QPoint(2, 0), QPoint(0, 2),
		};
		for (auto i = 0; i != 5; ++i) {
			auto color = QColor::fromRgb(grid.pixel(points[i]));
			auto h = 0., sat = 0., v = 0.;
			color.getHsvF(&h, &sat, &v);
			color = QColor::fromHsvF(
				std::max(h, 0.),
				std::clamp(sat * 1.3, 0.35, 1.),
				std::clamp(v * 1.15, 0.35, 1.));
			const auto mix = [](int a, int b) { return (a * 6 + b * 4) / 10; };
			_palette[i] = QColor(
				mix(color.red(), accents[i].red()),
				mix(color.green(), accents[i].green()),
				mix(color.blue(), accents[i].blue()));
		}
		average = QColor::fromRgb(_cover.scaled(
			1,
			1,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation).pixel(0, 0));
	} else {
		_palette = accents;
	}
	_backdrop->tint = LiquidTint(average, _cover.isNull() ? 0.9 : 0.5);

	// Static shade on top of the flowing liquid.
	const auto ratio = devicePixelRatioF();
	_shade = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	_shade.setDevicePixelRatio(ratio);
	_shade.fill(Qt::transparent);
	auto p = QPainter(&_shade);
	const auto full = QRectF(rect());
	auto shade = QLinearGradient(full.topLeft(), full.bottomLeft());
	shade.setColorAt(0., QColor(0, 0, 0, 70));
	shade.setColorAt(0.4, QColor(0, 0, 0, 20));
	shade.setColorAt(0.78, QColor(0, 0, 0, 120));
	shade.setColorAt(1., QColor(0, 0, 0, 215));
	p.fillRect(full, shade);
	auto vignette = QRadialGradient(
		full.center(),
		std::max(full.width(), full.height()) * 0.75);
	vignette.setColorAt(0.6, QColor(0, 0, 0, 0));
	vignette.setColorAt(1., QColor(0, 0, 0, 110));
	p.fillRect(full, vignette);
	p.end();

	renderLiquid(crl::now());
}

void MusicPlayer::renderLiquid(crl::time now) {
	_liquidRendered = now;
	const auto w = std::clamp(width() / 12, 24, 220);
	const auto h = std::clamp(height() / 12, 24, 160);
	auto image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
	image.fill(_backdrop->tint.darker(220));
	{
		auto p = QPainter(&image);
		p.setRenderHint(QPainter::Antialiasing);
		const auto t = float64(now % 10000000) / 1000.;
		const auto playing = _backdrop->playing;
		if (!_cover.isNull()) {
			// The cover itself drifts slowly under the liquid.
			const auto side = std::max(w, h) * 1.35;
			p.setOpacity(0.5);
			p.drawImage(
				QRectF(
					(w - side) / 2. + std::sin(t * 0.21) * w * 0.08,
					(h - side) / 2. + std::cos(t * 0.17) * h * 0.08,
					side,
					side),
				_cover);
			p.setOpacity(1.);
		}
		p.setCompositionMode(QPainter::CompositionMode_Screen);
		struct Blob {
			float64 x, y, ax, ay, sx, sy, r;
		};
		static const auto blobs = std::array<Blob, 6>{ {
			{ 0.22, 0.28, 0.20, 0.16, 0.23, 0.31, 0.55 },
			{ 0.78, 0.62, 0.18, 0.22, 0.19, 0.27, 0.60 },
			{ 0.50, 0.45, 0.30, 0.20, 0.13, 0.17, 0.45 },
			{ 0.30, 0.80, 0.22, 0.12, 0.29, 0.21, 0.50 },
			{ 0.85, 0.20, 0.12, 0.24, 0.25, 0.15, 0.42 },
			{ 0.55, 0.95, 0.26, 0.10, 0.17, 0.33, 0.48 },
		} };
		const auto base = float64(std::min(w, h));
		for (auto i = 0; i != int(blobs.size()); ++i) {
			const auto &blob = blobs[i];
			const auto pulse = playing
				? 1. + 0.10 * std::sin(t * 3.1 + i * 1.7)
				: 1.;
			const auto center = QPointF(
				(blob.x + blob.ax * std::sin(t * blob.sx + i)) * w,
				(blob.y + blob.ay * std::cos(t * blob.sy + i * 0.7)) * h);
			const auto radius = blob.r * base * 1.4 * pulse;
			auto color = _palette[i % _palette.size()];
			auto gradient = QRadialGradient(center, radius);
			color.setAlpha(170);
			gradient.setColorAt(0., color);
			color.setAlpha(70);
			gradient.setColorAt(0.55, color);
			color.setAlpha(0);
			gradient.setColorAt(1., color);
			p.fillRect(QRectF(0, 0, w, h), gradient);
		}
	}
	const auto light = _cursorFade.value(_cursorInside ? 1. : 0.);
	if (light > 0. && width() > 0 && height() > 0) {
		auto p = QPainter(&image);
		p.setRenderHint(QPainter::Antialiasing);
		p.setCompositionMode(QPainter::CompositionMode_Screen);
		const auto center = QPointF(
			_cursorSmooth.x() * w / width(),
			_cursorSmooth.y() * h / height());
		auto color = _palette[0].lighter(120);
		auto gradient = QRadialGradient(center, std::min(w, h) * 0.45);
		color.setAlpha(int(150 * light));
		gradient.setColorAt(0., color);
		color.setAlpha(0);
		gradient.setColorAt(1., color);
		p.fillRect(QRectF(0, 0, w, h), gradient);
	}
	_liquid = Images::BlurLargeImage(std::move(image), 2);

	if (now - _backdropPushed >= 66) {
		_backdropPushed = now;
		_backdrop->blurred = _liquid;
		_backdrop->content = rect();
		++_backdrop->generation;
	}
}

bool MusicPlayer::eventFilter(QObject *object, QEvent *e) {
	if (object == _header.get() || object == _lyrics.get()) {
		const auto widget = static_cast<QWidget*>(object);
		if (e->type() == QEvent::MouseMove) {
			const auto point = static_cast<QMouseEvent*>(e)->pos();
			cursorMoved(QPointF(widget->mapToParent(point)));
		} else if (e->type() == QEvent::MouseButtonPress) {
			const auto point = static_cast<QMouseEvent*>(e)->pos();
			addRipple(QPointF(widget->mapToParent(point)), 1.6);
		} else if (e->type() == QEvent::Leave
			&& !rect().contains(mapFromGlobal(QCursor::pos()))) {
			cursorLeft();
		}
	}
	return RpWidget::eventFilter(object, e);
}

void MusicPlayer::cursorMoved(QPointF point) {
	_cursor = point;
	if (!_cursorInside) {
		_cursorInside = true;
		_cursorSmooth = point;
		_cursorFade.start([=] { update(); }, 0., 1., crl::time(300));
	}
	const auto now = crl::now();
	const auto moved = point - _lastRipplePosition;
	const auto distance = std::sqrt(
		moved.x() * moved.x() + moved.y() * moved.y());
	if (now - _lastRipple >= 90 && distance >= 28.) {
		addRipple(point, 0.7);
	}
}

void MusicPlayer::cursorLeft() {
	if (_cursorInside) {
		_cursorInside = false;
		_cursorFade.start([=] { update(); }, 1., 0., crl::time(500));
	}
}

void MusicPlayer::addRipple(QPointF point, float64 strength) {
	if (_ripples.size() >= 16) {
		_ripples.erase(begin(_ripples));
	}
	_ripples.push_back({ .center = point, .start = crl::now(), .strength = strength });
	_lastRipple = crl::now();
	_lastRipplePosition = point;
}

void MusicPlayer::paintWater(QPainter &p, crl::time now) {
	const auto light = _cursorFade.value(_cursorInside ? 1. : 0.);
	if (light <= 0. && _ripples.empty()) {
		return;
	}
	PainterHighQualityEnabler hq(p);
	p.save();
	p.setCompositionMode(QPainter::CompositionMode_Screen);
	p.setPen(Qt::NoPen);
	if (light > 0.) {
		const auto radius = std::max(width(), height()) * 0.22;
		auto glow = QRadialGradient(_cursorSmooth, radius);
		glow.setColorAt(0., QColor(255, 196, 140, int(70 * light)));
		glow.setColorAt(0.45, QColor(255, 140, 60, int(26 * light)));
		glow.setColorAt(1., QColor(255, 140, 60, 0));
		p.fillRect(rect(), glow);
	}
	for (const auto &ripple : _ripples) {
		const auto age = std::clamp(
			float64(now - ripple.start) / 1500.,
			0.,
			1.);
		const auto eased = 1. - std::pow(1. - age, 3.);
		const auto fade = std::pow(1. - age, 2.) * ripple.strength;
		for (auto ring = 0; ring != 2; ++ring) {
			const auto radius = 12. + (230. - ring * 70.) * eased;
			const auto width = 16. + 10. * age;
			auto gradient = QRadialGradient(ripple.center, radius);
			const auto inner = std::clamp((radius - width) / radius, 0., 1.);
			const auto middle = std::clamp(
				(radius - width * 0.35) / radius,
				inner,
				1.);
			const auto alpha = int(std::clamp(
				(ring ? 45. : 85.) * fade,
				0.,
				255.));
			gradient.setColorAt(0., QColor(255, 255, 255, 0));
			gradient.setColorAt(inner, QColor(255, 255, 255, 0));
			gradient.setColorAt(middle, QColor(255, 236, 214, alpha));
			gradient.setColorAt(1., QColor(255, 255, 255, 0));
			p.setBrush(gradient);
			p.drawEllipse(ripple.center, radius, radius);
		}
	}
	p.restore();
}

void MusicPlayer::paintEvent(QPaintEvent *e) {
	if (_backgroundDirty
		|| _shade.size() != size() * devicePixelRatioF()) {
		rebuildBackground();
	}
	auto p = QPainter(this);
	{
		PainterHighQualityEnabler hq(p);
		p.drawImage(rect(), _liquid);
	}
	p.drawImage(0, 0, _shade);
	paintWater(p, crl::now());

	if (!_lyrics->isVisible()) {
		paintCoverAndStatus(p);
	}
	paintSeek(p);
	paintButton(p, Control::Previous, _previousRect);
	paintButton(p, Control::PlayPause, _playRect);
	paintButton(p, Control::Next, _nextRect);
}

void MusicPlayer::paintCoverAndStatus(QPainter &p) {
	if (_lyricsArea.isEmpty()) {
		return;
	}
	const auto statusFont = st::flashgramMusicStatusFont;
	const auto status = !_lyricsEnabled
		? QString()
		: (_lyricsState == LyricsState::Loading)
		? Tr("Looking for lyrics...", "Ищем текст песни...")
		: (_lyricsState == LyricsState::Missing)
		? Tr("Lyrics not found", "Текст песни не найден")
		: QString();
	const auto statusHeight = status.isEmpty() ? 0 : statusFont->height * 2;
	const auto side = std::min({
		st::flashgramMusicCoverMax,
		_lyricsArea.width(),
		_lyricsArea.height() - statusHeight - st::flashgramMusicGutter * 2,
	});
	auto top = _lyricsArea.y()
		+ (_lyricsArea.height() - std::max(side, 0) - statusHeight) / 2;
	PainterHighQualityEnabler hq(p);
	if (side >= st::flashgramMusicButton) {
		const auto rect = QRect(
			_lyricsArea.x() + (_lyricsArea.width() - side) / 2,
			top,
			side,
			side);
		const auto radius = float64(st::flashgramMusicCoverRadius);
		p.setPen(Qt::NoPen);
		for (auto i = 3; i != 0; --i) {
			p.setBrush(QColor(0, 0, 0, 22));
			p.drawRoundedRect(
				QRectF(rect).adjusted(-i * 3, -i * 2, i * 3, i * 5),
				radius + i * 3,
				radius + i * 3);
		}
		p.save();
		p.setClipPath(RoundedPath(rect, radius));
		if (!_cover.isNull()) {
			const auto scaled = _cover.size().scaled(
				rect.size(),
				Qt::KeepAspectRatioByExpanding);
			p.drawImage(
				QRect(
					rect.x() + (rect.width() - scaled.width()) / 2,
					rect.y() + (rect.height() - scaled.height()) / 2,
					scaled.width(),
					scaled.height()),
				_cover);
		} else {
			auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
			gradient.setColorAt(0., QColor(255, 150, 60));
			gradient.setColorAt(1., QColor(150, 50, 20));
			p.fillRect(rect, gradient);
			p.setPen(QPen(QColor(255, 255, 255, 220), side / 28.));
			p.setBrush(Qt::NoBrush);
			const auto unit = side / 10.;
			const auto c = QRectF(rect).center();
			p.drawEllipse(QPointF(c.x() - unit, c.y() + unit * 1.6), unit, unit * 0.8);
			p.drawLine(
				QPointF(c.x(), c.y() + unit * 1.6),
				QPointF(c.x(), c.y() - unit * 2.4));
			p.drawLine(
				QPointF(c.x(), c.y() - unit * 2.4),
				QPointF(c.x() + unit * 1.6, c.y() - unit * 1.6));
		}
		p.restore();
		p.setPen(QPen(QColor(255, 255, 255, 40), 1.));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(QRectF(rect).adjusted(.5, .5, -.5, -.5), radius, radius);
		top = rect.bottom() + 1;
	}
	if (!status.isEmpty()) {
		p.setFont(statusFont);
		p.setPen(QColor(255, 255, 255, 150));
		p.drawText(
			QRect(_lyricsArea.x(), top, _lyricsArea.width(), statusHeight),
			Qt::AlignCenter,
			status);
	}
}

void MusicPlayer::paintSeek(QPainter &p) {
	const auto length = _backdrop->length;
	const auto progress = _seeking
		? _seekProgress
		: (length > 0)
		? std::clamp(float64(_backdrop->now()) / length, 0., 1.)
		: 0.;
	const auto hover = _seekState.hover.value(
		(_seekState.over || _seeking) ? 1. : 0.);
	const auto height = st::flashgramMusicSeekHeight * (1. + 0.5 * hover);
	const auto y = _seekRect.y() + (_seekRect.height() - height) / 2.;
	const auto bar = QRectF(_seekRect.x(), y, _seekRect.width(), height);
	const auto radius = height / 2.;

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 64));
	p.drawRoundedRect(bar, radius, radius);
	auto fill = QLinearGradient(bar.topLeft(), bar.topRight());
	fill.setColorAt(0., QColor(255, 255, 255, 235));
	fill.setColorAt(1., QColor(255, 214, 170, 245));
	p.setBrush(fill);
	p.drawRoundedRect(
		QRectF(bar.x(), bar.y(), std::max(bar.width() * progress, height), height),
		radius,
		radius);
	if (hover > 0.) {
		auto barPath = QPainterPath();
		barPath.addRoundedRect(bar, radius, radius);
		PaintLiquidSheen(p, barPath, bar, hover);
		const auto knob = st::flashgramMusicSeekArea * 0.32 * hover;
		p.setBrush(QColor(255, 255, 255));
		p.drawEllipse(
			QPointF(bar.x() + bar.width() * progress, bar.center().y()),
			knob,
			knob);
	}

	const auto font = st::flashgramGlassTimeFont;
	const auto timeTop = _seekRect.bottom() + 1 + st::flashgramMusicTimeSkip;
	const auto current = _seeking
		? crl::time(length * _seekProgress)
		: _backdrop->now();
	p.setFont(font);
	p.setPen(QColor(255, 255, 255, 170));
	p.drawText(
		QRect(_seekRect.x(), timeTop, _seekRect.width(), font->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		FormatTime(current));
	p.drawText(
		QRect(_seekRect.x(), timeTop, _seekRect.width(), font->height),
		Qt::AlignRight | Qt::AlignVCenter,
		FormatTime(length));

	if (_seekHover && length > 0) {
		const auto x = std::clamp(*_seekHover, _seekRect.left(), _seekRect.right());
		const auto text = FormatTime(crl::time(length * progressAt(x)));
		const auto width = font->width(text) + font->height;
		const auto bubbleHeight = font->height + font->height / 3;
		const auto bubble = QRectF(
			std::clamp(
				x - width / 2.,
				float64(_column.x()),
				float64(_column.right() - width)),
			_seekRect.y() - bubbleHeight - 2,
			width,
			bubbleHeight);
		p.setPen(QPen(QColor(255, 255, 255, 60), 1.));
		p.setBrush(QColor(0, 0, 0, 150));
		p.drawRoundedRect(bubble, bubbleHeight / 2., bubbleHeight / 2.);
		p.setPen(QColor(255, 255, 255));
		p.drawText(bubble, Qt::AlignCenter, text);
	}
}

auto MusicPlayer::state(Control control) -> ControlState & {
	switch (control) {
	case Control::Previous: return _previousState;
	case Control::PlayPause: return _playState;
	case Control::Next: return _nextState;
	default: return _seekState;
	}
}

void MusicPlayer::paintButton(QPainter &p, Control control, QRect rect) {
	const auto player = Media::Player::instance();
	const auto enabled = (control == Control::Previous)
		? player->previousAvailable(Type::Song)
		: (control == Control::Next)
		? player->nextAvailable(Type::Song)
		: bool(_current.audio());
	const auto hover = enabled
		? state(control).hover.value(state(control).over ? 1. : 0.)
		: 0.;
	const auto pressed = enabled && (_pressed == control) && (_over == control);
	const auto scale = pressed ? 0.9 : (1. + 0.08 * hover);
	const auto big = (control == Control::PlayPause);
	const auto center = QRectF(rect).center();

	PainterHighQualityEnabler hq(p);
	if (big && _playing) {
		// Liquid glow breathing around the play button.
		const auto t = float64(crl::now() % 1000000) / 1000.;
		const auto breath = 0.5 + 0.5 * std::sin(t * 2.4);
		const auto glowRadius = rect.width() * (0.62 + 0.1 * breath);
		auto glow = QRadialGradient(center, glowRadius);
		glow.setColorAt(0.7, QColor(255, 150, 60, int(90 * breath)));
		glow.setColorAt(1., QColor(255, 150, 60, 0));
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(center, glowRadius, glowRadius);
	}
	p.save();
	p.translate(center);
	p.scale(scale, scale);
	p.translate(-center);

	const auto circle = QRectF(rect);
	auto circlePath = QPainterPath();
	circlePath.addEllipse(circle);
	if (big) {
		auto fill = QLinearGradient(circle.topLeft(), circle.bottomLeft());
		fill.setColorAt(0., QColor(255, 255, 255, 250));
		fill.setColorAt(1., QColor(255, 226, 196, 250));
		p.setPen(Qt::NoPen);
		p.setBrush(fill);
		p.drawEllipse(circle);
		PaintLiquidSheen(p, circlePath, circle, std::clamp(hover, 0., 1.), true);
	} else {
		auto fill = QLinearGradient(circle.topLeft(), circle.bottomLeft());
		fill.setColorAt(0., QColor(255, 255, 255, int(46 + 30 * hover)));
		fill.setColorAt(1., QColor(255, 255, 255, int(16 + 20 * hover)));
		p.setBrush(fill);
		auto border = QLinearGradient(circle.topLeft(), circle.bottomRight());
		border.setColorAt(0., QColor(255, 255, 255, 120));
		border.setColorAt(1., QColor(255, 255, 255, 30));
		p.setPen(QPen(QBrush(border), 1.));
		p.drawEllipse(circle.adjusted(.5, .5, -.5, -.5));
		PaintLiquidSheen(p, circlePath, circle, std::clamp(hover, 0., 1.));
	}

	const auto color = big
		? QColor(60, 30, 12)
		: QColor(255, 255, 255, enabled ? 240 : 90);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto unit = circle.width() / 24.;
	const auto triangle = [&](float64 left, bool forward) {
		auto path = QPainterPath();
		const auto top = center.y() - 5 * unit;
		const auto bottom = center.y() + 5 * unit;
		if (forward) {
			path.moveTo(left, top);
			path.lineTo(left + 8 * unit, center.y());
			path.lineTo(left, bottom);
		} else {
			path.moveTo(left + 8 * unit, top);
			path.lineTo(left, center.y());
			path.lineTo(left + 8 * unit, bottom);
		}
		path.closeSubpath();
		p.drawPath(path);
	};
	switch (control) {
	case Control::PlayPause:
		if (_playing) {
			const auto w = 2.6 * unit;
			const auto h = 10 * unit;
			p.drawRoundedRect(
				QRectF(center.x() - 1.4 * unit - w, center.y() - h / 2, w, h),
				unit,
				unit);
			p.drawRoundedRect(
				QRectF(center.x() + 1.4 * unit, center.y() - h / 2, w, h),
				unit,
				unit);
		} else {
			triangle(center.x() - 3 * unit, true);
		}
		break;
	case Control::Previous:
		p.drawRoundedRect(
			QRectF(center.x() - 5 * unit, center.y() - 5 * unit, 2 * unit, 10 * unit),
			unit * 0.6,
			unit * 0.6);
		triangle(center.x() - 3 * unit, false);
		break;
	case Control::Next:
		triangle(center.x() - 5 * unit, true);
		p.drawRoundedRect(
			QRectF(center.x() + 3 * unit, center.y() - 5 * unit, 2 * unit, 10 * unit),
			unit * 0.6,
			unit * 0.6);
		break;
	default: break;
	}
	p.restore();
}

auto MusicPlayer::controlAt(QPoint point) const -> Control {
	const auto inCircle = [&](QRect rect) {
		const auto center = QRectF(rect).center();
		const auto dx = point.x() - center.x();
		const auto dy = point.y() - center.y();
		return (dx * dx + dy * dy) <= (rect.width() * rect.width() / 4.);
	};
	if (inCircle(_playRect)) {
		return Control::PlayPause;
	} else if (inCircle(_previousRect)) {
		return Control::Previous;
	} else if (inCircle(_nextRect)) {
		return Control::Next;
	} else if (_seekRect.contains(point) && _backdrop->length > 0) {
		return Control::Seek;
	}
	return Control::None;
}

float64 MusicPlayer::progressAt(int x) const {
	return std::clamp(
		float64(x - _seekRect.x()) / std::max(_seekRect.width(), 1),
		0.,
		1.);
}

void MusicPlayer::setOver(Control control) {
	if (_over == control) {
		return;
	}
	const auto animate = [&](Control which, bool over) {
		if (which == Control::None) {
			return;
		}
		auto &data = state(which);
		data.over = over;
		data.hover.start(
			[=] { update(); },
			over ? 0. : 1.,
			over ? 1. : 0.,
			over ? crl::time(320) : kHoverDuration,
			over ? anim::easeOutBack : anim::linear);
	};
	animate(_over, false);
	_over = control;
	animate(_over, true);
	setCursor((_over == Control::None)
		? style::cur_default
		: style::cur_pointer);
}

void MusicPlayer::mouseMoveEvent(QMouseEvent *e) {
	cursorMoved(QPointF(e->pos()));
	setOver(_seeking ? Control::Seek : controlAt(e->pos()));
	const auto hover = (_over == Control::Seek)
		? std::make_optional(e->pos().x())
		: std::nullopt;
	if (_seeking) {
		_seekProgress = progressAt(e->pos().x());
		if (_preview.isEmpty()) {
			Media::Player::instance()->updateSeeking(
				Type::Song,
				_seekProgress);
		}
	}
	if (hover != _seekHover || _seeking) {
		_seekHover = hover;
		update(_seekRect.marginsAdded({ 0, 40, 0, 40 }));
	}
}

void MusicPlayer::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	setFocus();
	addRipple(QPointF(e->pos()), 1.6);
	setOver(controlAt(e->pos()));
	_pressed = _over;
	if (_pressed == Control::Seek) {
		_seeking = true;
		_seekProgress = progressAt(e->pos().x());
		if (_preview.isEmpty()) {
			const auto player = Media::Player::instance();
			player->startSeeking(Type::Song);
			player->updateSeeking(Type::Song, _seekProgress);
		}
	}
	update();
}

void MusicPlayer::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, Control::None);
	const auto player = Media::Player::instance();
	if (std::exchange(_seeking, false)) {
		_seekProgress = progressAt(e->pos().x());
		const auto target = crl::time(_backdrop->length * _seekProgress);
		_backdrop->position = target;
		_backdrop->stamp = crl::now();
		if (_preview.isEmpty()) {
			player->finishSeeking(Type::Song, _seekProgress);
		} else {
			_previewStart = crl::now() - target;
		}
	} else if (pressed != Control::None && pressed == controlAt(e->pos())) {
		switch (pressed) {
		case Control::PlayPause: player->playPause(Type::Song); break;
		case Control::Previous: player->previous(Type::Song); break;
		case Control::Next: player->next(Type::Song); break;
		default: break;
		}
	}
	setOver(controlAt(e->pos()));
	update();
}

void MusicPlayer::leaveEventHook(QEvent *e) {
	if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
		cursorLeft();
	}
	if (!_seeking) {
		setOver(Control::None);
		_seekHover = std::nullopt;
		update();
	}
}

void MusicPlayer::keyPressEvent(QKeyEvent *e) {
	switch (e->key()) {
	case Qt::Key_Escape: closeAnimated(); break;
	case Qt::Key_Space:
		Media::Player::instance()->playPause(Type::Song);
		break;
	case Qt::Key_Left: seekBy(-crl::time(5000)); break;
	case Qt::Key_Right: seekBy(crl::time(5000)); break;
	default: RpWidget::keyPressEvent(e); break;
	}
}

void ShowMusicPlayerPreview(
		not_null<Window::SessionController*> controller,
		const QString &lyricsPath) {
	if (const auto body = controller->widget()->bodyWidget()) {
		Ui::CreateChild<MusicPlayer>(body, controller, lyricsPath);
	}
}

void ShowMusicPlayer(not_null<Window::SessionController*> controller) {
	const auto body = controller->widget()->bodyWidget();
	if (!body) {
		return;
	}
	static auto Current = QPointer<MusicPlayer>();
	if (Current && Current->parentWidget() == body) {
		Current->raise();
		Current->setFocus();
		return;
	}
	Current = Ui::CreateChild<MusicPlayer>(body, controller);
}

} // namespace FlashGram
