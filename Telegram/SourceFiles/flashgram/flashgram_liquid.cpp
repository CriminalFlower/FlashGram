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
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace FlashGram {
namespace {

constexpr auto kMarkerName = "liquid_theme_v1"_cs;

// Warm dark "liquid" gradient, four colors like Telegram gradients.
constexpr auto kWallPaperSlug = "3b1a08~a4461a~1c0b04~d9772b"_cs;

[[nodiscard]] QColor Accent() {
	return QColor(255, 140, 40);
}

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
