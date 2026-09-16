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

// Warm dark "liquid" gradient, four colors like Telegram gradients.
constexpr auto kWallPaperSlug = "3b1a08~a4461a~1c0b04~d9772b"_cs;

[[nodiscard]] QColor Accent() {
	return QColor(255, 140, 40);
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
		auto glow = QRadialGradient(
			cursor,
			std::max(r.height() * 1.6, 48.));
		glow.setColorAt(0., QColor(255, 200, 140, 70));
		glow.setColorAt(0.5, QColor(255, 150, 70, 26));
		glow.setColorAt(1., QColor(255, 150, 70, 0));
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
		sheen.setColorAt(0.35, QColor(255, 176, 96, 34));
		sheen.setColorAt(0.5, QColor(255, 244, 228, 70));
		sheen.setColorAt(0.65, QColor(255, 120, 150, 30));
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

void ApplyLiquidTheme() {
	using namespace Window::Theme;

	const auto schemes = EmbeddedThemes();
	const auto night = ranges::find(
		schemes,
		EmbeddedType::Night,
		&EmbeddedScheme::type);
	if (night == end(schemes)) {
		return;
	}

	auto &settings = Core::App().settings();
	settings.setSystemAccentColorEnabled(false);
	settings.themesAccentColors().set(EmbeddedType::Night, Accent());
	Local::writeSettings();

	// The night scheme is colorized by the saved accent color.
	if (IsNightMode()) {
		ApplyDefaultWithPath(night->path);
	} else {
		ToggleNightMode(night->path);
	}
	KeepApplied();

	if (const auto paper = Data::WallPaper::FromColorsSlug(
			kWallPaperSlug.utf16())) {
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

	ApplyLiquidTheme();
}

} // namespace FlashGram
