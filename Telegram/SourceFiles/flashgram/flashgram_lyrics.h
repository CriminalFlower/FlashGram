/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "flashgram/flashgram_glass_player.h"

namespace FlashGram {

struct LyricsQuery {
	QString title;
	QString performer;
	QString fileName;
	QString localPath;
	crl::time duration = 0;
};

struct LyricsResult {
	std::vector<TimedLine> lines;
	bool synced = false;
};

// Looks for lyrics in this order: a sidecar .lrc/.srt/.vtt next to the
// local file, ID3v2 SYLT / USLT tags of the local file, then the public
// LRCLIB database (only title, performer and duration are sent).
// The callback is always called on the main thread, maybe with no lines.
void ResolveLyrics(LyricsQuery query, Fn<void(LyricsResult)> done);

// Exposed for parsing tests.
[[nodiscard]] LyricsResult ParseId3Lyrics(const QByteArray &tag);
[[nodiscard]] LyricsResult LyricsFromText(
	const QString &text,
	crl::time duration);

} // namespace FlashGram
