/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <array>

namespace FlashGram {

// Animated FlashGram color themes. Each one is the night theme with its
// own accent, a flowing four color gradient wallpaper and a matching
// liquid highlight on hovered buttons.
enum class LiquidTheme {
	Orange,
	Green,
	Red,
	Blue,
	Black,
};

struct LiquidPalette {
	LiquidTheme id = LiquidTheme::Orange;
	const char *key = "";
	const char *nameEn = "";
	const char *nameRu = "";
	QColor accent;
	// Colors of the flowing gradient: dark base, two lights and a deep one.
	std::array<QColor, 4> wallpaper;
	QColor glow;
	QColor sheen;
	QColor sheenTail;
};

[[nodiscard]] const std::vector<LiquidPalette> &LiquidThemes();
[[nodiscard]] const LiquidPalette &LiquidThemePalette(LiquidTheme theme);
[[nodiscard]] LiquidTheme CurrentLiquidTheme();

// Switches the client once to the FlashGram "Liquid Orange" look.
// Later theme changes by the user are kept.
void ApplyLiquidThemeOnce();

// Applies the chosen animated theme and remembers it.
void ApplyLiquidTheme(LiquidTheme theme);

// Adds a flowing "liquid" highlight to buttons under the mouse cursor
// all over the client. Safe to call more than once.
void InstallLiquidButtons();

} // namespace FlashGram
