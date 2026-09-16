/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_liquid.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_wall_paper.h"
#include "storage/localstorage.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>

namespace FlashGram {
namespace {

constexpr auto kMarkerName = "liquid_theme_v1"_cs;
constexpr auto kThemeFileName = "liquid_theme"_cs;

[[nodiscard]] QString ThemePath() {
	return cWorkingDir() + u"tdata/flashgram/"_q + kThemeFileName.utf16();
}

[[nodiscard]] LiquidTheme ReadTheme() {
	auto file = QFile(ThemePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return LiquidTheme::Orange;
	}
	const auto key = QString::fromUtf8(file.readAll()).trimmed();
	for (const auto &palette : LiquidThemes()) {
		if (key == QLatin1String(palette.key)) {
			return palette.id;
		}
	}
	return LiquidTheme::Orange;
}

void WriteTheme(LiquidTheme theme) {
	const auto path = ThemePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QFile(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(LiquidThemePalette(theme).key);
	}
}

[[nodiscard]] LiquidTheme &CurrentThemeValue() {
	static auto result = ReadTheme();
	return result;
}

[[nodiscard]] QString WallPaperSlug(const LiquidPalette &palette) {
	auto parts = QStringList();
	for (const auto &color : palette.wallpaper) {
		parts.push_back(color.name(QColor::HexRgb).mid(1));
	}
	return parts.join(u"~"_q);
}

[[nodiscard]] QColor WithAlpha(QColor color, int alpha) {
	color.setAlpha(alpha);
	return color;
}

constexpr auto kOverlayName = "flashgram_liquid_overlay";
constexpr auto kFadeIn = crl::time(220);
constexpr auto kFadeOut = crl::time(360);
constexpr auto kMaxOverlayWidth = 1400;
constexpr auto kMaxOverlayHeight = 160;

// Transparent layer above a hovered button with a flowing highlight.
class LiquidOverlay final : public Ui::RpWidget {
public:
	explicit LiquidOverlay(not_null<QWidget*> button)
	: RpWidget(button)
	, _ticker([=] {
		update();
		return _hovered || _fade.animating();
	}) {
		setObjectName(QString::fromLatin1(kOverlayName));
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_TranslucentBackground);
		setGeometry(button->rect());
		_started = crl::now();
	}

	void setHovered(bool hovered) {
		if (_hovered == hovered) {
			return;
		}
		_hovered = hovered;
		if (hovered) {
			setGeometry(parentWidget()->rect());
			show();
			raise();
		}
		_fade.start(
			[=] {
				update();
				if (!_hovered && !_fade.animating()) {
					hide();
				}
			},
			hovered ? 0. : 1.,
			hovered ? 1. : 0.,
			hovered ? kFadeIn : kFadeOut,
			anim::easeOutCubic);
		if (!_ticker.animating()) {
			_ticker.start();
		}
	}

	void setCursorPoint(QPoint point) {
		_cursor = point;
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		const auto opacity = _fade.value(_hovered ? 1. : 0.);
		if (opacity <= 0.) {
			return;
		}
		auto p = QPainter(this);
		PainterHighQualityEnabler hq(p);
		const auto r = QRectF(rect());
		const auto radius = (height() <= 56)
			? (height() / 2.)
			: 12.;
		auto clip = QPainterPath();
		clip.addRoundedRect(r, radius, radius);
		p.setClipPath(clip);
		p.setOpacity(opacity);

		const auto t = float64(crl::now() - _started) / 1000.;

		// Soft glow following the cursor.
		const auto cursor = QRectF(r).contains(_cursor)
			? QPointF(_cursor)
			: r.center();
		const auto &palette = LiquidThemePalette(CurrentLiquidTheme());
		auto glow = QRadialGradient(
			cursor,
			std::max(r.height() * 1.6, 48.));
		glow.setColorAt(0., WithAlpha(palette.glow.lighter(130), 70));
		glow.setColorAt(0.5, WithAlpha(palette.glow, 26));
		glow.setColorAt(1., WithAlpha(palette.glow, 0));
		p.fillRect(r, glow);

		// Iridescent band flowing across the button.
		const auto period = 1.9;
		const auto phase = std::fmod(t, period) / period;
		const auto band = std::max(r.width() * 0.45, r.height() * 2.);
		const auto x = -band + (r.width() + 2 * band) * phase;
		auto sheen = QLinearGradient(
			QPointF(x, r.top()),
			QPointF(x + band, r.bottom()));
		sheen.setColorAt(0., QColor(255, 255, 255, 0));
		sheen.setColorAt(0.35, WithAlpha(palette.sheen, 34));
		sheen.setColorAt(0.5, QColor(255, 250, 244, 70));
		sheen.setColorAt(0.65, WithAlpha(palette.sheenTail, 30));
		sheen.setColorAt(1., QColor(255, 255, 255, 0));
		p.fillRect(r, sheen);

		// Liquid surface line along the top edge.
		auto wave = QPainterPath();
		const auto step = 4.;
		for (auto px = 0.; px <= r.width() + step; px += step) {
			const auto py = 1.5
				+ 1.2 * std::sin(px * 0.06 + t * 4.2)
				+ 0.6 * std::sin(px * 0.13 - t * 6.1);
			if (px == 0.) {
				wave.moveTo(px, py);
			} else {
				wave.lineTo(px, py);
			}
		}
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(255, 255, 255, 60), 1.2));
		p.drawPath(wave);
	}

private:
	Ui::Animations::Simple _fade;
	Ui::Animations::Basic _ticker;
	crl::time _started = 0;
	QPoint _cursor;
	bool _hovered = false;

};

class LiquidButtonsFilter final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *object, QEvent *e) override {
		const auto type = e->type();
		if (type != QEvent::Enter
			&& type != QEvent::Leave
			&& type != QEvent::MouseMove
			&& type != QEvent::Hide) {
			return false;
		}
		if (!object->isWidgetType()) {
			return false;
		}
		const auto widget = static_cast<QWidget*>(object);
		const auto button = dynamic_cast<Ui::AbstractButton*>(widget);
		if (!button) {
			return false;
		}
		if (type == QEvent::Enter) {
			if (!button->isEnabled()
				|| button->width() > kMaxOverlayWidth
				|| button->height() > kMaxOverlayHeight
				|| button->width() < 12
				|| button->height() < 12) {
				return false;
			}
			if (const auto overlay = find(button, true)) {
				overlay->setCursorPoint(button->mapFromGlobal(QCursor::pos()));
				overlay->setHovered(true);
			}
		} else if (type == QEvent::MouseMove) {
			if (const auto overlay = find(button, false)) {
				overlay->setCursorPoint(
					static_cast<QMouseEvent*>(e)->pos());
			}
		} else if (const auto overlay = find(button, false)) {
			overlay->setHovered(false);
		}
		return false;
	}

private:
	[[nodiscard]] LiquidOverlay *find(
			not_null<QWidget*> button,
			bool create) {
		const auto name = QString::fromLatin1(kOverlayName);
		for (const auto child : button->children()) {
			if (child->objectName() == name) {
				return static_cast<LiquidOverlay*>(child);
			}
		}
		return create ? Ui::CreateChild<LiquidOverlay>(button.get()) : nullptr;
	}

};

[[nodiscard]] QString MarkerPath() {
	return cWorkingDir() + u"tdata/flashgram/"_q + kMarkerName.utf16();
}

} // namespace

const std::vector<LiquidPalette> &LiquidThemes() {
	static const auto result = std::vector<LiquidPalette>{
		{
			.id = LiquidTheme::Orange,
			.key = "orange",
			.nameEn = "Liquid Orange",
			.nameRu = "Жидкий оранжевый",
			.accent = QColor(255, 140, 40),
			.wallpaper = { {
				QColor(0x3b, 0x1a, 0x08),
				QColor(0xa4, 0x46, 0x1a),
				QColor(0x1c, 0x0b, 0x04),
				QColor(0xd9, 0x77, 0x2b),
			} },
			.glow = QColor(255, 150, 70),
			.sheen = QColor(255, 176, 96),
			.sheenTail = QColor(255, 120, 150),
		},
		{
			.id = LiquidTheme::Green,
			.key = "green",
			.nameEn = "Emerald",
			.nameRu = "Изумрудная",
			.accent = QColor(52, 211, 120),
			.wallpaper = { {
				QColor(0x06, 0x2b, 0x1a),
				QColor(0x13, 0x8a, 0x55),
				QColor(0x03, 0x14, 0x0c),
				QColor(0x5f, 0xd3, 0x8d),
			} },
			.glow = QColor(70, 220, 140),
			.sheen = QColor(120, 240, 170),
			.sheenTail = QColor(80, 220, 220),
		},
		{
			.id = LiquidTheme::Red,
			.key = "red",
			.nameEn = "Crimson",
			.nameRu = "Алая",
			.accent = QColor(240, 64, 72),
			.wallpaper = { {
				QColor(0x33, 0x06, 0x0c),
				QColor(0xa8, 0x16, 0x2c),
				QColor(0x16, 0x02, 0x05),
				QColor(0xe8, 0x4a, 0x5f),
			} },
			.glow = QColor(250, 80, 90),
			.sheen = QColor(255, 130, 130),
			.sheenTail = QColor(255, 90, 180),
		},
		{
			.id = LiquidTheme::Blue,
			.key = "blue",
			.nameEn = "Deep Ocean",
			.nameRu = "Глубокий океан",
			.accent = QColor(64, 150, 255),
			.wallpaper = { {
				QColor(0x06, 0x16, 0x3a),
				QColor(0x17, 0x5c, 0xc4),
				QColor(0x02, 0x08, 0x1c),
				QColor(0x3f, 0xb4, 0xf0),
			} },
			.glow = QColor(80, 160, 255),
			.sheen = QColor(130, 200, 255),
			.sheenTail = QColor(150, 120, 255),
		},
		{
			.id = LiquidTheme::Black,
			.key = "black",
			.nameEn = "Obsidian",
			.nameRu = "Обсидиан",
			.accent = QColor(176, 180, 196),
			.wallpaper = { {
				QColor(0x0a, 0x0a, 0x0c),
				QColor(0x2e, 0x2f, 0x36),
				QColor(0x02, 0x02, 0x03),
				QColor(0x4a, 0x4c, 0x57),
			} },
			.glow = QColor(200, 205, 220),
			.sheen = QColor(220, 225, 240),
			.sheenTail = QColor(160, 170, 200),
		},
	};
	return result;
}

const LiquidPalette &LiquidThemePalette(LiquidTheme theme) {
	const auto &list = LiquidThemes();
	const auto i = ranges::find(list, theme, &LiquidPalette::id);
	return (i != end(list)) ? *i : list.front();
}

LiquidTheme CurrentLiquidTheme() {
	return CurrentThemeValue();
}

void ApplyLiquidTheme(LiquidTheme theme) {
	using namespace Window::Theme;

	const auto schemes = EmbeddedThemes();
	const auto night = ranges::find(
		schemes,
		EmbeddedType::Night,
		&EmbeddedScheme::type);
	if (night == end(schemes)) {
		return;
	}
	const auto &palette = LiquidThemePalette(theme);
	CurrentThemeValue() = theme;
	WriteTheme(theme);

	auto &settings = Core::App().settings();
	settings.setSystemAccentColorEnabled(false);
	settings.themesAccentColors().set(EmbeddedType::Night, palette.accent);
	Local::writeSettings();

	// The night scheme is colorized by the saved accent color.
	if (IsNightMode()) {
		ApplyDefaultWithPath(night->path);
	} else {
		ToggleNightMode(night->path);
	}
	KeepApplied();

	if (const auto paper = Data::WallPaper::FromColorsSlug(
			WallPaperSlug(palette))) {
		Background()->set(*paper);
	}
}

void InstallLiquidButtons() {
	static auto installed = QPointer<QObject>();
	if (installed) {
		return;
	}
	const auto filter = new LiquidButtonsFilter(qApp);
	qApp->installEventFilter(filter);
	installed = filter;
}

void ApplyLiquidThemeOnce() {
	const auto path = MarkerPath();
	if (QFile::exists(path)) {
		return;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto marker = QFile(path);
	if (!marker.open(QIODevice::WriteOnly)) {
		return;
	}
	marker.write("1");
	marker.close();

	ApplyLiquidTheme(LiquidTheme::Orange);
}

} // namespace FlashGram
