/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace FlashGram {

// Switches the client once to the FlashGram "Liquid Orange" look: the
// night theme with an orange accent and a warm liquid gradient wallpaper.
// Later theme changes by the user are kept.
void ApplyLiquidThemeOnce();

// Applies the look again (used by an explicit user action).
void ApplyLiquidTheme();

// Adds a flowing "liquid" highlight to buttons under the mouse cursor
// all over the client. Safe to call more than once.
void InstallLiquidButtons();

} // namespace FlashGram
