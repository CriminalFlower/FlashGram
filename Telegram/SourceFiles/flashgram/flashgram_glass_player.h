/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace FlashGram {

struct TimedLine {
	crl::time from = 0;
	crl::time till = 0;
	QString text;
};

// Parses LRC, SRT and WebVTT content into sorted, non-empty lines.
[[nodiscard]] std::vector<TimedLine> ParseTimedText(const QByteArray &data);

// Sidecar subtitle / lyrics files next to a local media file:
// "clip.lrc", "clip.srt", "clip.vtt", "clip.en.srt" and so on.
[[nodiscard]] QStringList FindTimedTextFiles(const QString &mediaPath);

// Liquid Glass layer for the media viewer: a header with title, performer,
// actions and a playback visualizer, plus synced lyrics / subtitles.
// The backdrop is a heavily blurred downscaled copy of the current frame
// refreshed a few times per second, never per paint.
class GlassPlayer final {
public:
	struct Descriptor {
		not_null<QWidget*> parent;
		QString title;
		QString performer;
		QString mediaPath;
		uint64 seed = 0;
		Fn<QImage()> frame;
		Fn<QRect()> contentRect;
		Fn<void(crl::time)> seek;
		Fn<void()> close;
		// Called when the lyrics card appears, hides or switches track.
		Fn<void()> layoutChanged;
	};

	explicit GlassPlayer(Descriptor &&descriptor);
	~GlassPlayer();

	void setMediaPath(const QString &path);
	void updatePlayback(crl::time position, crl::time length, bool playing);
	void setControlsShown(bool shown);

	// Places the header at the top and the lyrics card right above
	// the bottom limit (usually the playback controls).
	void updateGeometry(int width, int top, int bottom);

	[[nodiscard]] bool lyricsShown() const;
	[[nodiscard]] int lyricsTop() const;

	// Paints a glass surface for a sibling widget, rect in parent coords.
	void paintGlass(QPainter &p, QRect local, QPoint topLeftInParent);

	struct Backdrop;

private:
	class Header;
	class Lyrics;
	class Surface;

	void refreshBackdrop(bool force);
	void loadTrack(int index);
	void refreshButtons();
	void notifyLayout();
	void toggleLyrics();
	void nextTrack();

	Descriptor _descriptor;
	std::shared_ptr<Backdrop> _backdrop;
	std::unique_ptr<Surface> _controlsSurface;
	std::unique_ptr<Header> _header;
	std::unique_ptr<Lyrics> _lyrics;
	QStringList _tracks;
	int _trackIndex = -1;
	bool _lyricsEnabled = true;
	bool _controlsShown = true;
	crl::time _backdropUpdated = 0;
	int _width = 0;
	int _top = 0;
	int _bottom = 0;

};

} // namespace FlashGram
