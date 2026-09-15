/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_gift_view.h"

#include "api/api_premium.h"
#include "data/data_document.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_flashgram.h"

#include <QtGui/QPainterPath>

namespace FlashGram {
namespace {

constexpr auto kGridColumns = 3;

struct StickerCache {
	std::vector<Data::StarGift> gifts;
	rpl::event_stream<> updated;
	std::unique_ptr<Api::PremiumGiftCodeOptions> api;
	rpl::lifetime lifetime;
	bool requested = false;
};

using CacheMap = base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<StickerCache>>;

[[nodiscard]] CacheMap &Caches() {
	static auto result = CacheMap();
	return result;
}

[[nodiscard]] StickerCache &CacheFor(not_null<Main::Session*> session) {
	auto &map = Caches();
	auto i = map.find(session);
	if (i == end(map)) {
		i = map.emplace(session, std::make_unique<StickerCache>()).first;
		session->lifetime().add([=] {
			Caches().remove(session);
		});
	}
	return *i->second;
}

[[nodiscard]] QString NormalizeEmoji(QString value) {
	value.remove(QChar(0xFE0F));
	return value.trimmed();
}

[[nodiscard]] Info::PeerGifts::GiftTypeStars MakeDescriptor(
		const Gift &gift,
		int number,
		not_null<DocumentData*> document) {
	auto info = Data::StarGift{ .document = document };
	if (gift.kind == GiftKind::Collectible) {
		const auto center = gift.backdropCenter.isValid()
			? gift.backdropCenter
			: RarityColor(gift.rarity);
		const auto edge = gift.backdropEdge.isValid()
			? gift.backdropEdge
			: center.darker(170);
		info.unique = std::make_shared<Data::UniqueGift>(Data::UniqueGift{
			.title = gift.name,
			.starsForResale = 0,
			.number = number,
			.model = Data::UniqueGiftModel{ { .name = gift.model }, document },
			.pattern = Data::UniqueGiftPattern{
				{ .name = gift.symbol },
				document,
			},
			.backdrop = Data::UniqueGiftBackdrop{
				{ .name = gift.backdrop },
				center,
				edge,
				edge.lighter(140),
				QColor(255, 255, 255),
			},
		});
		info.limitedCount = int(std::max(gift.amount, int64(1)));
	}
	return { .info = info, .mine = true };
}

void PaintPlaceholder(
		QPainter &p,
		QRect rect,
		const Gift &gift,
		int number,
		const QImage &image,
		bool over) {
	auto hq = PainterHighQualityEnabler(p);
	const auto collectible = (gift.kind == GiftKind::Collectible);
	const auto radius = st::flashgramGiftRadius;
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(rect), radius, radius);
	if (collectible) {
		const auto center = gift.backdropCenter.isValid()
			? gift.backdropCenter
			: RarityColor(gift.rarity);
		const auto edge = gift.backdropEdge.isValid()
			? gift.backdropEdge
			: center.darker(170);
		auto gradient = QRadialGradient(
			QPointF(rect.center()),
			std::max(rect.width(), rect.height()) * 0.7);
		gradient.setColorAt(0., center);
		gradient.setColorAt(1., edge);
		p.fillPath(path, gradient);
	} else {
		p.fillPath(path, st::windowBgOver->c);
	}
	if (over) {
		p.fillPath(path, QColor(255, 255, 255, 24));
	}

	const auto size = st::flashgramPlaceholderImageSize;
	const auto textHeight = st::flashgramGiftTextHeight;
	const auto target = QRect(
		rect.x() + (rect.width() - size) / 2,
		rect.y() + (rect.height() - textHeight - size) / 2,
		size,
		size);
	if (!image.isNull()) {
		const auto scaled = image.size().scaled(
			target.size(),
			Qt::KeepAspectRatio);
		p.drawImage(
			QRect(
				target.x() + (target.width() - scaled.width()) / 2,
				target.y() + (target.height() - scaled.height()) / 2,
				scaled.width(),
				scaled.height()),
			image);
	} else {
		auto color = RarityColor(gift.rarity);
		color.setAlpha(collectible ? 90 : 60);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(target, size / 4., size / 4.);
	}

	const auto margin = st::flashgramGiftRarityMargin;
	const auto textWidth = rect.width() - 2 * margin;
	const auto nameRect = QRect(
		rect.x() + margin,
		rect.y() + rect.height() - textHeight,
		textWidth,
		textHeight / 2);
	const auto &nameFont = st::flashgramGiftNameFont;
	p.setFont(nameFont->f);
	p.setPen(collectible ? QColor(255, 255, 255) : st::windowFg->c);
	p.drawText(
		nameRect,
		int(Qt::AlignHCenter | Qt::AlignBottom),
		nameFont->elided(gift.name, textWidth));
	p.setFont(st::flashgramGiftNumberFont->f);
	p.setPen(collectible ? QColor(255, 255, 255, 190) : st::windowSubTextFg->c);
	p.drawText(
		nameRect.translated(0, textHeight / 2),
		int(Qt::AlignHCenter | Qt::AlignTop),
		u"#"_q + FormatCount(number));
}

void PaintLocalMark(QPainter &p, QRect rect) {
	auto hq = PainterHighQualityEnabler(p);
	const auto size = st::flashgramFgMarkSize;
	const auto margin = st::flashgramFgMarkMargin;
	const auto circle = QRect(
		rect.x() + rect.width() - margin - size,
		rect.y() + rect.height() - margin - size,
		size,
		size);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 60));
	p.drawEllipse(circle);
	p.setFont(st::flashgramFgMarkFont->f);
	p.setPen(QColor(255, 255, 255, 210));
	p.drawText(circle, Qt::AlignCenter, u"FG"_q);
}

} // namespace

void RequestGiftStickers(not_null<Main::Session*> session) {
	auto &cache = CacheFor(session);
	if (cache.requested) {
		return;
	}
	cache.requested = true;
	cache.api = std::make_unique<Api::PremiumGiftCodeOptions>(
		session->user());
	const auto raw = &cache;
	cache.api->requestStarGifts(
	) | rpl::on_error_done([=](const QString &error) {
		LOG(("FlashGram Error: Gift stickers request failed: %1"
			).arg(error));
		raw->requested = false;
	}, [=] {
		raw->gifts = raw->api->starGifts();
		raw->updated.fire({});
	}, cache.lifetime);
}

DocumentData *LookupGiftSticker(
		not_null<Main::Session*> session,
		const QString &emoji) {
	const auto wanted = NormalizeEmoji(emoji);
	if (wanted.isEmpty()) {
		return nullptr;
	}
	for (const auto &gift : CacheFor(session).gifts) {
		const auto sticker = gift.document->sticker();
		if (sticker && NormalizeEmoji(sticker->alt) == wanted) {
			return gift.document;
		}
	}
	return nullptr;
}

rpl::producer<> GiftStickersUpdated(not_null<Main::Session*> session) {
	return CacheFor(session).updated.events();
}

LocalGiftView::LocalGiftView(
	QWidget *parent,
	not_null<Main::Session*> session,
	const Gift &gift,
	int number,
	bool localMark)
: RpWidget(parent)
, _session(session)
, _gift(gift)
, _number(number)
, _localMark(localMark)
, _delegate(session, Info::PeerGifts::GiftButtonMode::Minimal)
, _image(LoadGiftImage(gift)) {
	RequestGiftStickers(session);
	if (_localMark) {
		_mark = Ui::CreateChild<Ui::RpWidget>(this);
		_mark->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto mark = _mark;
		_mark->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(mark);
			PaintLocalMark(p, mark->rect());
		}, _mark->lifetime());
		_mark->show();
	}
	refresh();
	if (!_button && _image.isNull()) {
		GiftStickersUpdated(
			session
		) | rpl::filter([=] {
			return !_button;
		}) | rpl::on_next([=] {
			refresh();
		}, lifetime());
	}
}

LocalGiftView::~LocalGiftView() = default;

void LocalGiftView::setClickedCallback(Fn<void()> callback) {
	_clicked = std::move(callback);
}

void LocalGiftView::setTransparentForMouse() {
	_mouseTransparent = true;
	setAttribute(Qt::WA_TransparentForMouseEvents);
	if (_button) {
		_button->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	if (_placeholder) {
		_placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

void LocalGiftView::resizeEvent(QResizeEvent *e) {
	updateChildGeometry();
}

void LocalGiftView::refresh() {
	const auto document = _image.isNull()
		? LookupGiftSticker(_session, _gift.sticker)
		: nullptr;
	if (document) {
		delete base::take(_placeholder);
		if (!_button) {
			_button = std::make_unique<Info::PeerGifts::GiftButton>(
				this,
				&_delegate);
			if (_mouseTransparent) {
				_button->setAttribute(Qt::WA_TransparentForMouseEvents);
			}
			_button->setClickedCallback([=] {
				if (_clicked) {
					_clicked();
				}
			});
			_button->show();
		}
		_button->setDescriptor(
			MakeDescriptor(_gift, _number, document),
			Info::PeerGifts::GiftButtonMode::Minimal);
	} else if (!_placeholder) {
		_placeholder = Ui::CreateChild<Ui::AbstractButton>(this);
		const auto placeholder = _placeholder;
		if (_mouseTransparent) {
			placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
		placeholder->setClickedCallback([=] {
			if (_clicked) {
				_clicked();
			}
		});
		placeholder->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(placeholder);
			PaintPlaceholder(
				p,
				placeholder->rect(),
				_gift,
				_number,
				_image,
				placeholder->isOver());
		}, placeholder->lifetime());
		placeholder->show();
	}
	if (_mark) {
		_mark->raise();
	}
	updateChildGeometry();
}

void LocalGiftView::updateChildGeometry() {
	const auto inner = rect();
	if (_button) {
		const auto extend = _delegate.buttonExtend();
		_button->setGeometry(inner.marginsRemoved(extend), extend);
	}
	if (_placeholder) {
		_placeholder->setGeometry(inner);
	}
	if (_mark) {
		_mark->setGeometry(inner);
	}
}

ScaledGiftView::ScaledGiftView(
	QWidget *parent,
	not_null<Main::Session*> session,
	const Gift &gift,
	int number)
: RpWidget(parent)
, _source(Ui::CreateChild<LocalGiftView>(this, session, gift, number, false))
, _timer([=] { update(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	_source->setTransparentForMouse();
	_source->setGeometry(
		-10000,
		-10000,
		st::flashgramRouletteItemWidth,
		st::flashgramRouletteItemHeight);
	_source->show();
	_timer.callEach(crl::time(33));
}

void ScaledGiftView::paintEvent(QPaintEvent *e) {
	const auto ratio = style::DevicePixelRatio();
	auto frame = QImage(
		_source->size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(ratio);
	frame.fill(Qt::transparent);
	_source->render(
		&frame,
		QPoint(),
		QRegion(),
		QWidget::RenderFlags(QWidget::DrawChildren));

	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto scaled = _source->size().scaled(size(), Qt::KeepAspectRatio);
	p.drawImage(
		QRect(
			(width() - scaled.width()) / 2,
			(height() - scaled.height()) / 2,
			scaled.width(),
			scaled.height()),
		frame);
}

LocalGiftsGrid::LocalGiftsGrid(
	QWidget *parent,
	not_null<Main::Session*> session,
	std::vector<OwnedGift> gifts,
	Fn<void(OwnedGift)> open)
: RpWidget(parent) {
	for (const auto &owned : gifts) {
		const auto gift = FindGift(owned.giftId);
		if (!gift) {
			continue;
		}
		const auto view = Ui::CreateChild<LocalGiftView>(
			this,
			session,
			*gift,
			owned.number ? owned.number : gift->number,
			true);
		view->setClickedCallback([=] {
			open(owned);
		});
		view->show();
		_views.push_back(view);
	}
}

int LocalGiftsGrid::resizeGetHeight(int newWidth) {
	const auto skip = st::flashgramGridSkip;
	const auto width = std::max(
		(newWidth - skip * (kGridColumns - 1)) / kGridColumns,
		1);
	const auto height = st::flashgramGridItemHeight;
	for (auto i = 0; i != int(_views.size()); ++i) {
		_views[i]->setGeometry(
			(i % kGridColumns) * (width + skip),
			(i / kGridColumns) * (height + skip),
			width,
			height);
	}
	const auto rows = (int(_views.size()) + kGridColumns - 1) / kGridColumns;
	return rows ? (rows * height + (rows - 1) * skip) : 0;
}

} // namespace FlashGram
