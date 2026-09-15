/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "flashgram/flashgram_state.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "ui/rp_widget.h"

class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class AbstractButton;
} // namespace Ui

namespace Data {
class DocumentMedia;
} // namespace Data

namespace HistoryView {
class StickerPlayer;
} // namespace HistoryView

namespace FlashGram {

// Public Telegram gift stickers are used only as visuals for
// FlashGram Local gifts. Nothing is sent back to Telegram.
void RequestGiftStickers(not_null<Main::Session*> session);
[[nodiscard]] DocumentData *LookupGiftSticker(
	not_null<Main::Session*> session,
	const QString &emoji);
[[nodiscard]] rpl::producer<> GiftStickersUpdated(
	not_null<Main::Session*> session);

class LocalGiftView final : public Ui::RpWidget {
public:
	LocalGiftView(
		QWidget *parent,
		not_null<Main::Session*> session,
		const Gift &gift,
		int number,
		bool localMark);
	~LocalGiftView();

	void setClickedCallback(Fn<void()> callback);
	void setTransparentForMouse();

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void refresh();
	void updateChildGeometry();

	const not_null<Main::Session*> _session;
	const Gift _gift;
	const int _number = 0;
	const bool _localMark = false;
	Info::PeerGifts::Delegate _delegate;
	std::unique_ptr<Info::PeerGifts::GiftButton> _button;
	Ui::AbstractButton *_placeholder = nullptr;
	Ui::RpWidget *_mark = nullptr;
	QImage _image;
	Fn<void()> _clicked;
	bool _mouseTransparent = false;

};

void PaintGiftGem(QPainter &p, QRectF rect, const Gift &gift);

// A large animated gift sticker without a card, for hero previews.
class GiftStickerView final : public Ui::RpWidget {
public:
	GiftStickerView(
		QWidget *parent,
		not_null<Main::Session*> session,
		const Gift &gift);
	~GiftStickerView();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void refresh();
	void createPlayer();

	const not_null<Main::Session*> _session;
	const Gift _gift;
	QImage _image;
	DocumentData *_document = nullptr;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<HistoryView::StickerPlayer> _player;
	QSize _playerSize;
	rpl::lifetime _mediaLifetime;
	rpl::lifetime _stickersLifetime;

};

// Telegram gift buttons draw stickers at a fixed size, so large previews
// render a regular gift view offscreen and paint it scaled.
class ScaledGiftView final : public Ui::RpWidget {
public:
	ScaledGiftView(
		QWidget *parent,
		not_null<Main::Session*> session,
		const Gift &gift,
		int number);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const not_null<LocalGiftView*> _source;
	base::Timer _timer;

};

class LocalGiftsGrid final : public Ui::RpWidget {
public:
	LocalGiftsGrid(
		QWidget *parent,
		not_null<Main::Session*> session,
		std::vector<OwnedGift> gifts,
		Fn<void(OwnedGift)> open);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	std::vector<not_null<LocalGiftView*>> _views;

};

} // namespace FlashGram
