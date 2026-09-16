/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace FlashGram {

// Full window Liquid Glass player for the current song: blurred cover,
// header with actions and visualizer, synced lyrics, seek and controls.
void ShowMusicPlayer(not_null<Window::SessionController*> controller);

// Developer preview with a simulated clock and lyrics from a local file,
// opened when FLASHGRAM_PLAYER_PREVIEW points to an .lrc/.srt file.
void ShowMusicPlayerPreview(
	not_null<Window::SessionController*> controller,
	const QString &lyricsPath);

} // namespace FlashGram
