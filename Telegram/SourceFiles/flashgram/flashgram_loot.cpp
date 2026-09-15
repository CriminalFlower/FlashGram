/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_loot.h"

#include "base/random.h"
#include "base/unique_qptr.h"
#include "data/data_user.h"
#include "flashgram/flashgram_gift_view.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QPainterPath>

namespace FlashGram {
namespace {

constexpr auto kSpinCostCents = int64(2500);
constexpr auto kRouletteItems = 40;
constexpr auto kRouletteWinnerIndex = 32;
constexpr auto kSpinDuration = crl::time(5200);
constexpr auto kPreviewCount = 3;
constexpr auto kParticlesCount = 30;

[[nodiscard]] rpl::producer<QString> BalanceValue(not_null<UserData*> user) {
	return rpl::single(rpl::empty) | rpl::then(Changes()) | rpl::map([=] {
		return FormatBalance(LoadProfile(user).balance);
	});
}

[[nodiscard]] QStringList CollectibleIds() {
	auto result = QStringList();
	for (const auto &gift : GiftsCatalog()) {
		if (gift.kind == GiftKind::Collectible) {
			result.push_back(gift.id);
		}
	}
	return result;
}

void ShowBackendRequired(not_null<Window::SessionController*> controller) {
	controller->uiShow()->showToast(Tr(
		"This needs the FlashGram server, which is not available yet.",
		"Для этого нужен сервер FlashGram, он пока недоступен."));
}

void PaintCardBackground(QPainter &p, QRect rect, QColor top, QColor bottom) {
	auto hq = PainterHighQualityEnabler(p);
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., top);
	gradient.setColorAt(1., bottom);
	const auto radius = st::flashgramCardRadius;
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(rect), radius, radius);
	p.fillPath(path, gradient);
}

not_null<Ui::AbstractButton*> AddCard(
		not_null<Ui::GenericBox*> box,
		int height,
		QColor top,
		QColor bottom,
		QString title,
		QString text,
		bool arrow) {
	const auto card = box->addRow(
		object_ptr<Ui::AbstractButton>(box),
		st::flashgramCardMargin);
	card->resize(card->width(), height);
	card->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(card);
		PaintCardBackground(p, card->rect(), top, bottom);
		if (card->isOver() && arrow) {
			auto path = QPainterPath();
			const auto radius = st::flashgramCardRadius;
			path.addRoundedRect(QRectF(card->rect()), radius, radius);
			p.fillPath(path, QColor(255, 255, 255, 20));
		}
		const auto inner = card->rect().marginsRemoved(
			st::flashgramCardPadding);
		p.setPen(QColor(255, 255, 255));
		p.setFont(st::flashgramCardTitleFont->f);
		p.drawText(inner, int(Qt::AlignLeft | Qt::AlignTop), title);
		if (!text.isEmpty()) {
			p.setFont(st::flashgramCardTextFont->f);
			p.setPen(QColor(255, 255, 255, 210));
			p.drawText(
				inner.translated(
					0,
					st::flashgramCardTitleFont->height
						+ st::flashgramCardLineSkip),
				int(Qt::AlignLeft | Qt::AlignTop),
				text);
		}
		if (arrow) {
			p.setPen(QColor(255, 255, 255));
			p.setFont(st::flashgramCardArrowFont->f);
			p.drawText(
				inner,
				int(Qt::AlignRight | Qt::AlignTop),
				QString(QChar(0x203A)));
		}
	}, card->lifetime());
	return card;
}

void AddCardPreviews(
		not_null<Ui::AbstractButton*> card,
		not_null<Main::Session*> session,
		const QStringList &giftIds) {
	auto views = std::vector<not_null<LocalGiftView*>>();
	for (const auto &id : giftIds) {
		if (int(views.size()) >= kPreviewCount) {
			break;
		} else if (const auto gift = FindGift(id)) {
			const auto view = Ui::CreateChild<LocalGiftView>(
				card.get(),
				session,
				*gift,
				gift->number,
				false);
			view->setTransparentForMouse();
			view->show();
			views.push_back(view);
		}
	}
	card->sizeValue() | rpl::on_next([=](QSize size) {
		const auto preview = st::flashgramCardPreviewSize;
		const auto padding = st::flashgramCardPadding;
		auto left = padding.left();
		const auto top = size.height() - padding.bottom() - preview.height();
		for (const auto view : views) {
			view->setGeometry(left, top, preview.width(), preview.height());
			left += preview.width() + st::flashgramCardPreviewSkip;
		}
	}, card->lifetime());
}

void AddBalanceCard(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> user) {
	const auto card = AddCard(
		box,
		st::flashgramBalanceCardHeight,
		QColor(0x2B, 0x2F, 0x38),
		QColor(0x1A, 0x1D, 0x24),
		Tr("Your balance", "Твой баланс"),
		QString(),
		false);
	const auto balance = Ui::CreateChild<Ui::FlatLabel>(
		card.get(),
		BalanceValue(user),
		st::flashgramBalanceLabel);
	balance->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto note = Ui::CreateChild<Ui::FlatLabel>(
		card.get(),
		Tr(
			"FlashGram Balance. Not Telegram Stars.",
			"FlashGram Balance. Это не Telegram Stars."),
		st::flashgramBalanceNote);
	note->setAttribute(Qt::WA_TransparentForMouseEvents);
	card->sizeValue() | rpl::on_next([=](QSize size) {
		const auto padding = st::flashgramCardPadding;
		balance->moveToLeft(
			padding.left(),
			padding.top() + st::flashgramBalanceTop);
		note->moveToLeft(
			padding.left(),
			size.height() - padding.bottom() - note->height());
	}, card->lifetime());
}

void AddGiftPreview(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const Gift &gift,
		int number) {
	const auto row = box->addRow(object_ptr<Ui::RpWidget>(box));
	row->resize(row->width(), st::flashgramDetailsPreviewSize.height());
	const auto view = Ui::CreateChild<LocalGiftView>(
		row,
		session,
		gift,
		number,
		false);
	view->show();
	row->widthValue() | rpl::on_next([=](int width) {
		const auto size = st::flashgramDetailsPreviewSize;
		view->setGeometry(
			(width - size.width()) / 2,
			0,
			size.width(),
			size.height());
	}, view->lifetime());
	Ui::AddSkip(box->verticalLayout());
}

void AddField(
		not_null<Ui::GenericBox*> box,
		const QString &name,
		const QString &value) {
	if (value.isEmpty()) {
		return;
	}
	auto text = tr::bold(name);
	text.append(u": "_q).append(value);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::producer<TextWithEntities>(rpl::single(text)),
		st::boxLabel));
	Ui::AddSkip(box->verticalLayout(), st::flashgramDetailsRowSkip);
}

class RouletteStrip final : public Ui::RpWidget {
public:
	RouletteStrip(
		QWidget *parent,
		not_null<Main::Session*> session,
		QColor fade,
		QColor marker);

	void fill(const QStringList &pool, const Gift *winner);
	void setOffset(float64 offset);
	[[nodiscard]] float64 winnerOffset() const;
	[[nodiscard]] int itemStep() const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void layoutItems();

	const not_null<Main::Session*> _session;
	std::vector<base::unique_qptr<LocalGiftView>> _items;
	base::unique_qptr<Ui::RpWidget> _overlay;
	float64 _offset = 0.;

};

RouletteStrip::RouletteStrip(
	QWidget *parent,
	not_null<Main::Session*> session,
	QColor fade,
	QColor marker)
: RpWidget(parent)
, _session(session)
, _overlay(base::make_unique_q<Ui::RpWidget>(this)) {
	_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto overlay = _overlay.get();
	_overlay->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(overlay);
		auto hq = PainterHighQualityEnabler(p);
		const auto width = overlay->width();
		const auto height = overlay->height();
		if (fade.isValid()) {
			const auto size = st::flashgramRouletteFade;
			auto transparent = fade;
			transparent.setAlpha(0);
			auto left = QLinearGradient(0, 0, size, 0);
			left.setColorAt(0., fade);
			left.setColorAt(1., transparent);
			p.fillRect(0, 0, size, height, left);
			auto right = QLinearGradient(width - size, 0, width, 0);
			right.setColorAt(0., transparent);
			right.setColorAt(1., fade);
			p.fillRect(width - size, 0, size, height, right);
		}
		const auto size = st::flashgramRouletteMarker;
		const auto center = width / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(marker);
		auto top = QPainterPath();
		top.moveTo(center - size, 0);
		top.lineTo(center + size, 0);
		top.lineTo(center, size * 1.2);
		top.closeSubpath();
		p.drawPath(top);
		auto bottom = QPainterPath();
		bottom.moveTo(center - size, height);
		bottom.lineTo(center + size, height);
		bottom.lineTo(center, height - size * 1.2);
		bottom.closeSubpath();
		p.drawPath(bottom);
	}, _overlay->lifetime());
}

void RouletteStrip::fill(const QStringList &pool, const Gift *winner) {
	_items.clear();
	_items.reserve(kRouletteItems);
	for (auto i = 0; i != kRouletteItems; ++i) {
		const auto gift = (winner && i == kRouletteWinnerIndex)
			? winner
			: RollGift(pool);
		if (!gift) {
			continue;
		}
		auto item = base::make_unique_q<LocalGiftView>(
			this,
			_session,
			*gift,
			gift->number,
			false);
		item->setTransparentForMouse();
		item->show();
		_items.push_back(std::move(item));
	}
	_offset = 0.;
	_overlay->raise();
	layoutItems();
}

void RouletteStrip::setOffset(float64 offset) {
	_offset = offset;
	layoutItems();
}

int RouletteStrip::itemStep() const {
	return st::flashgramRouletteItemWidth + st::flashgramRouletteSkip;
}

float64 RouletteStrip::winnerOffset() const {
	return float64(kRouletteWinnerIndex * itemStep());
}

void RouletteStrip::resizeEvent(QResizeEvent *e) {
	_overlay->setGeometry(rect());
	layoutItems();
}

int RouletteStrip::resizeGetHeight(int newWidth) {
	return st::flashgramRouletteHeight;
}

void RouletteStrip::layoutItems() {
	const auto itemWidth = st::flashgramRouletteItemWidth;
	const auto itemHeight = st::flashgramRouletteItemHeight;
	const auto first = width() / 2. - itemWidth / 2.;
	const auto top = (height() - itemHeight) / 2;
	for (auto i = 0; i != int(_items.size()); ++i) {
		_items[i]->setGeometry(
			int(std::round(first + i * itemStep() - _offset)),
			top,
			itemWidth,
			itemHeight);
	}
}

[[nodiscard]] float64 SpinTarget(not_null<RouletteStrip*> strip) {
	const auto spread = std::max(strip->itemStep() / 2, 1);
	const auto jitter = int(base::RandomValue<uint32>() % uint32(spread))
		- spread / 2;
	return strip->winnerOffset() + jitter;
}

class RouletteScreen final : public Ui::RpWidget {
public:
	RouletteScreen(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> close);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	enum class State {
		Idle,
		Spinning,
		Result,
	};

	void setFeatured(const Gift &gift, int number);
	void spin();
	void finish();
	void layoutChildren(int width);

	const not_null<Window::SessionController*> _controller;
	const not_null<UserData*> _user;
	const QStringList _pool;
	const not_null<Ui::AbstractButton*> _back;
	const not_null<Ui::AbstractButton*> _spin;
	const not_null<Ui::AbstractButton*> _myGifts;
	base::unique_qptr<ScaledGiftView> _featured;
	base::unique_qptr<RouletteStrip> _strip;
	Ui::Animations::Simple _animation;
	State _state = State::Idle;
	QString _featuredTitle;
	QString _balance;
	OwnedGift _won;

};

RouletteScreen::RouletteScreen(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> close)
: RpWidget(parent)
, _controller(controller)
, _user(controller->session().user())
, _pool(CollectibleIds())
, _back(Ui::CreateChild<Ui::AbstractButton>(this))
, _spin(Ui::CreateChild<Ui::AbstractButton>(this))
, _myGifts(Ui::CreateChild<Ui::AbstractButton>(this)) {
	RequestGiftStickers(&controller->session());

	BalanceValue(_user) | rpl::on_next([=](const QString &balance) {
		_balance = balance;
		update();
	}, lifetime());

	_back->setClickedCallback(close);
	_back->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_back);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = _back->rect();
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, _back->isOver() ? 60 : 40));
		p.drawRoundedRect(r, r.height() / 2., r.height() / 2.);
		p.setPen(QColor(255, 255, 255));
		p.setFont(st::flashgramRouletteBackFont->f);
		p.drawText(
			r,
			Qt::AlignCenter,
			QString(QChar(0x2039)) + ' ' + Tr("Back", "Назад"));
	}, _back->lifetime());

	_spin->setClickedCallback([=] {
		if (_state == State::Result) {
			_state = State::Idle;
			_spin->update();
			update();
			_controller->show(Box(MyGiftsBox, _controller));
		} else {
			spin();
		}
	});
	_spin->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_spin);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = _spin->rect();
		p.setPen(QPen(QColor(255, 255, 255, 90), 1.));
		p.setBrush(QColor(255, 255, 255, _spin->isOver() ? 75 : 55));
		p.drawRoundedRect(
			QRectF(r).marginsRemoved({ 0.5, 0.5, 0.5, 0.5 }),
			r.height() / 2.,
			r.height() / 2.);
		p.setPen(QColor(255, 255, 255, (_state == State::Spinning)
			? 150
			: 240));
		p.setFont(st::flashgramRouletteButtonFont->f);
		p.drawText(
			r,
			Qt::AlignCenter,
			(_state == State::Result)
				? Tr("To collection", "В коллекцию")
				: (_state == State::Spinning)
				? Tr("Spinning...", "Крутится...")
				: (Tr("Spin the roulette", "Прокрутить рулетку")
					+ u" · "_q
					+ FormatBalance({ .cents = kSpinCostCents })));
	}, _spin->lifetime());

	_myGifts->setClickedCallback([=] {
		_controller->show(Box(MyGiftsBox, _controller));
	});
	_myGifts->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_myGifts);
		const auto r = _myGifts->rect();
		const auto &icon = st::menuIconGiftPremium;
		const auto &font = st::flashgramRouletteLinkFont;
		const auto text = Tr("My Gifts", "Мои подарки")
			+ ' '
			+ QString(QChar(0x203A));
		const auto full = icon.width()
			+ st::flashgramRouletteLinkIconSkip
			+ font->width(text);
		const auto left = (r.width() - full) / 2;
		icon.paint(
			p,
			left,
			(r.height() - icon.height()) / 2,
			r.width(),
			QColor(255, 255, 255));
		p.setFont(font->f);
		p.setPen(QColor(255, 255, 255, _myGifts->isOver() ? 255 : 230));
		p.drawText(
			QRect(
				left + icon.width() + st::flashgramRouletteLinkIconSkip,
				0,
				r.width(),
				r.height()),
			int(Qt::AlignLeft | Qt::AlignVCenter),
			text);
	}, _myGifts->lifetime());

	const auto profile = LoadProfile(_user);
	const auto owned = ranges::find_if(profile.gifts, [](
			const OwnedGift &gift) {
		const auto data = FindGift(gift.giftId);
		return data && (data->kind == GiftKind::Collectible);
	});
	if (owned != end(profile.gifts)) {
		setFeatured(*FindGift(owned->giftId), owned->number);
	} else if (const auto gift = RollGift(_pool)) {
		setFeatured(*gift, gift->number);
	}
}

int RouletteScreen::resizeGetHeight(int newWidth) {
	layoutChildren(newWidth);
	return st::flashgramRouletteScreenHeight;
}

void RouletteScreen::layoutChildren(int width) {
	const auto height = st::flashgramRouletteScreenHeight;
	const auto side = st::flashgramRouletteSide;
	const auto backText = QString(QChar(0x2039)) + ' ' + Tr("Back", "Назад");
	_back->setGeometry(
		side,
		st::flashgramRouletteTop,
		st::flashgramRouletteBackFont->width(backText)
			+ 2 * st::flashgramRouletteBackPadding,
		st::flashgramRouletteBackHeight);
	const auto giftSize = st::flashgramRouletteGiftSize;
	if (_featured) {
		_featured->setGeometry(
			(width - giftSize) / 2,
			st::flashgramRouletteGiftTop,
			giftSize,
			giftSize);
	}
	if (_strip) {
		_strip->setGeometry(
			0,
			st::flashgramRouletteStripTop,
			width,
			st::flashgramRouletteHeight);
	}
	_spin->setGeometry(
		side,
		height
			- st::flashgramRouletteButtonBottom
			- st::flashgramRouletteButtonHeight,
		width - 2 * side,
		st::flashgramRouletteButtonHeight);
	_myGifts->setGeometry(
		side,
		height
			- st::flashgramRouletteLinkBottom
			- st::flashgramRouletteLinkHeight,
		width - 2 * side,
		st::flashgramRouletteLinkHeight);
}

void RouletteScreen::setFeatured(const Gift &gift, int number) {
	_featured = base::make_unique_q<ScaledGiftView>(
		this,
		&_controller->session(),
		gift,
		number);
	_featured->show();
	_featuredTitle = GiftTitle(gift, number);
	layoutChildren(width());
	update();
}

void RouletteScreen::spin() {
	if (_state != State::Idle) {
		return;
	}
	const auto winner = RollGift(_pool);
	if (!winner) {
		return;
	} else if (!SpendBalance(_user, { .cents = kSpinCostCents })) {
		_controller->uiShow()->showToast(Tr(
			"Not enough FlashGram Balance.",
			"Недостаточно FlashGram Balance."));
		return;
	}
	_state = State::Spinning;
	_won = OwnedGift{
		.giftId = winner->id,
		.number = RollNumber(*winner),
	};
	if (_featured) {
		_featured->hide();
	}
	if (!_strip) {
		_strip = base::make_unique_q<RouletteStrip>(
			this,
			&_controller->session(),
			QColor(),
			QColor(255, 255, 255));
	}
	_strip->show();
	layoutChildren(width());
	_strip->fill(_pool, winner);
	const auto strip = _strip.get();
	const auto target = SpinTarget(strip);
	_spin->update();
	update();
	_animation.start([=](float64 value) {
		strip->setOffset(value);
		if (!_animation.animating() && _state == State::Spinning) {
			finish();
		}
	}, 0., target, kSpinDuration, anim::easeOutCubic);
}

void RouletteScreen::finish() {
	_won = AddInventoryGift(_user, _won);
	_state = State::Result;
	if (_strip) {
		_strip->hide();
	}
	if (const auto gift = FindGift(_won.giftId)) {
		setFeatured(*gift, _won.number);
	}
	_spin->update();
	update();
}

void RouletteScreen::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto w = width();
	const auto h = height();

	auto background = QLinearGradient(0, 0, 0, h);
	background.setColorAt(0., QColor(0x4E, 0x9A, 0x74));
	background.setColorAt(0.5, QColor(0x6A, 0xAE, 0x82));
	background.setColorAt(1., QColor(0x3C, 0x7A, 0x5C));
	p.fillRect(rect(), background);

	const auto giftSize = st::flashgramRouletteGiftSize;
	const auto giftCenter = QPointF(
		w / 2.,
		st::flashgramRouletteGiftTop + giftSize / 2.);
	auto glow = QRadialGradient(giftCenter, giftSize * 0.8);
	glow.setColorAt(0., QColor(0xF0, 0xD8, 0x6A, 140));
	glow.setColorAt(1., QColor(0xF0, 0xD8, 0x6A, 0));
	p.fillRect(rect(), glow);

	const auto colors = std::array{
		QColor(0xF5, 0xC5, 0x42, 200),
		QColor(255, 255, 255, 150),
		QColor(0xF0, 0x8A, 0x3C, 190),
	};
	for (auto i = 0; i != kParticlesCount; ++i) {
		const auto position = QPointF(
			((i * 37 + 11) % 100) / 100. * w,
			(0.12 + ((i * 53 + 29) % 70) / 100.) * h);
		const auto &color = colors[i % colors.size()];
		if (i % 3 == 0) {
			p.setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(position, position + QPointF(4., 9.));
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			const auto radius = (i % 3 == 1) ? 2.5 : 1.8;
			p.drawEllipse(position, radius, radius);
		}
	}

	const auto side = st::flashgramRouletteSide;
	const auto titleRect = QRect(
		side,
		st::flashgramRouletteTitleTop,
		w - 2 * side,
		st::flashgramRouletteTitleFont->height);
	p.setPen(QColor(255, 255, 255));
	p.setFont(st::flashgramRouletteTitleFont->f);
	p.drawText(
		titleRect,
		int(Qt::AlignLeft | Qt::AlignVCenter),
		Tr("Roulette", "Рулетка"));
	p.setFont(st::flashgramRouletteBalanceFont->f);
	p.drawText(titleRect, int(Qt::AlignRight | Qt::AlignVCenter), _balance);

	if (_state == State::Result) {
		p.setFont(st::flashgramRouletteCongratsFont->f);
		p.drawText(
			QRect(
				0,
				st::flashgramRouletteCongratsTop,
				w,
				st::flashgramRouletteCongratsFont->height),
			Qt::AlignCenter,
			Tr("CONGRATULATIONS", "ПОЗДРАВЛЯЕМ"));
	}
	if (_state != State::Spinning && !_featuredTitle.isEmpty()) {
		p.setFont(st::flashgramRouletteNameFont->f);
		p.setPen(QColor(255, 255, 255, 245));
		p.drawText(
			QRect(
				0,
				st::flashgramRouletteGiftTop
					+ giftSize
					+ st::flashgramRouletteNameSkip,
				w,
				st::flashgramRouletteNameFont->height),
			Qt::AlignCenter,
			_featuredTitle);
	}
}

} // namespace

void LootBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto user = session->user();
	box->setTitle(rpl::single(u"FlashGram"_q));
	box->setWidth(st::boxWideWidth);
	RequestGiftStickers(session);

	AddBalanceCard(box, user);
	AddCard(
		box,
		st::flashgramNewsCardHeight,
		QColor(0x2F, 0xC4, 0xB2),
		QColor(0x3A, 0x7B, 0xE8),
		Tr("New drop!", "Новый розыгрыш!"),
		Tr("The drop is being prepared", "Розыгрыш готовится"),
		true);

	const auto roulette = AddCard(
		box,
		st::flashgramPreviewCardHeight,
		QColor(0x7B, 0x5C, 0xF0),
		QColor(0xE0, 0x3A, 0xB5),
		Tr("Roulette", "Рулетка"),
		Tr("A big win in one spin", "Крупный выигрыш в одном спине"),
		true);
	AddCardPreviews(roulette, session, CollectibleIds());
	roulette->setClickedCallback([=] {
		controller->show(Box(RouletteBox, controller));
	});

	auto casePreviews = QStringList();
	for (const auto &entry : Cases()) {
		if (!entry.giftIds.isEmpty()) {
			casePreviews.push_back(entry.giftIds.back());
		}
	}
	const auto cases = AddCard(
		box,
		st::flashgramCaseCardHeight,
		QColor(0xF0, 0xA0, 0x3A),
		QColor(0xE8, 0x3A, 0x7A),
		Tr("Cases", "Кейсы"),
		Tr("Open and collect gifts", "Открывай и забирай подарки"),
		true);
	AddCardPreviews(cases, session, casePreviews);
	cases->setClickedCallback([=] {
		controller->show(Box(CasesBox, controller));
	});

	const auto myGifts = AddCard(
		box,
		st::flashgramNewsCardHeight,
		QColor(0x3A, 0x3F, 0x4A),
		QColor(0x22, 0x26, 0x2E),
		Tr("My Gifts", "Мои подарки"),
		QString(),
		true);
	myGifts->setClickedCallback([=] {
		controller->show(Box(MyGiftsBox, controller));
	});

	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void RouletteBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	box->addRow(
		object_ptr<RouletteScreen>(box, controller, [=] {
			box->closeBox();
		}),
		QMargins());
}

void CasesBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	box->setTitle(TrValue("Cases", "Кейсы"));
	box->setWidth(st::boxWideWidth);
	RequestGiftStickers(session);
	for (const auto &entry : Cases()) {
		const auto id = entry.id;
		const auto card = AddCard(
			box,
			st::flashgramCaseCardHeight,
			entry.top.isValid() ? entry.top : QColor(0x4F, 0x8F, 0xE8),
			entry.bottom.isValid() ? entry.bottom : QColor(0x2A, 0x4F, 0xA3),
			entry.name,
			entry.description + u" · "_q + FormatBalance(entry.price),
			true);
		AddCardPreviews(card, session, entry.giftIds);
		card->setClickedCallback([=] {
			controller->show(Box(CaseBox, controller, id));
		});
	}
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void CaseBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		QString caseId) {
	const auto session = &controller->session();
	const auto user = session->user();
	box->setWidth(st::boxWideWidth);
	RequestGiftStickers(session);

	const auto found = FindCase(caseId);
	if (!found) {
		box->setTitle(TrValue("Cases", "Кейсы"));
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
		return;
	}
	const auto entry = *found;
	box->setTitle(rpl::single(entry.name));

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		BalanceValue(user) | rpl::map([](const QString &balance) {
			return Tr("Balance: ", "Баланс: ") + balance;
		}),
		st::boxLabel));
	Ui::AddSkip(box->verticalLayout());

	const auto strip = box->addRow(
		object_ptr<RouletteStrip>(
			box,
			session,
			st::boxBg->c,
			st::activeButtonBg->c),
		QMargins());
	strip->fill(entry.giftIds, nullptr);

	Ui::AddSubsectionTitle(
		box->verticalLayout(),
		TrValue("Possible gifts", "Возможные подарки"));
	auto list = std::vector<OwnedGift>();
	for (const auto &id : entry.giftIds) {
		if (const auto gift = FindGift(id)) {
			list.push_back({ .giftId = id, .number = gift->number });
		}
	}
	box->addRow(
		object_ptr<LocalGiftsGrid>(
			box,
			session,
			std::move(list),
			[=](OwnedGift owned) {
				controller->show(
					Box(LocalGiftDetailsBox, controller, owned, false));
			}),
		st::flashgramBoxGiftsPadding);

	struct State {
		Ui::Animations::Simple animation;
		bool opening = false;
	};
	const auto state = box->lifetime().make_state<State>();
	box->addButton(
		rpl::single(Tr("Open · %1", "Открыть · %1").arg(
			FormatBalance(entry.price))),
		[=] {
			if (state->opening) {
				return;
			}
			const auto winner = RollGift(entry.giftIds);
			if (!winner) {
				return;
			} else if (!SpendBalance(user, entry.price)) {
				controller->uiShow()->showToast(Tr(
					"Not enough FlashGram Balance.",
					"Недостаточно FlashGram Balance."));
				return;
			}
			state->opening = true;
			strip->fill(entry.giftIds, winner);
			const auto owned = OwnedGift{
				.giftId = winner->id,
				.number = RollNumber(*winner),
			};
			state->animation.start([=](float64 value) {
				strip->setOffset(value);
				if (!state->animation.animating() && state->opening) {
					state->opening = false;
					const auto added = AddInventoryGift(user, owned);
					controller->show(
						Box(LocalGiftDetailsBox, controller, added, true));
				}
			}, 0., SpinTarget(strip), kSpinDuration, anim::easeOutCubic);
		});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void LocalGiftDetailsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		OwnedGift owned,
		bool justWon) {
	const auto session = &controller->session();
	const auto user = session->user();
	box->setWidth(st::boxWideWidth);

	const auto found = FindGift(owned.giftId);
	if (!found) {
		box->setTitle(TrValue("Gift", "Подарок"));
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
		return;
	}
	const auto gift = *found;
	const auto number = owned.number ? owned.number : gift.number;
	box->setTitle(rpl::single(justWon
		? Tr("Congratulations!", "Поздравляем!")
		: GiftTitle(gift, number)));

	AddGiftPreview(box, session, gift, number);

	const auto uid = owned.uid;
	const auto current = [=]() -> std::optional<OwnedGift> {
		if (uid.isEmpty()) {
			return std::nullopt;
		}
		const auto profile = LoadProfile(user);
		const auto i = ranges::find(profile.gifts, uid, &OwnedGift::uid);
		return (i != end(profile.gifts))
			? std::make_optional(*i)
			: std::nullopt;
	};
	const auto profile = LoadProfile(user);
	const auto mine = current();
	const auto collectible = (gift.kind == GiftKind::Collectible);
	AddField(box, Tr("Name", "Название"), GiftTitle(gift, number));
	AddField(
		box,
		Tr("Number", "Номер"),
		u"#"_q + FormatCount(number) + ((gift.amount > 0)
			? (u" / "_q + FormatCount(gift.amount))
			: QString()));
	AddField(
		box,
		Tr("Type", "Тип"),
		collectible
			? Tr("Collectible", "Коллекционный")
			: Tr("Ordinary", "Обычный"));
	AddField(box, Tr("Rarity", "Редкость"), RarityName(gift.rarity));
	AddField(box, tr::lng_gift_unique_model(tr::now), gift.model);
	AddField(box, tr::lng_gift_unique_backdrop(tr::now), gift.backdrop);
	AddField(box, tr::lng_gift_unique_symbol(tr::now), gift.symbol);
	if (gift.value.cents > 0) {
		AddField(box, Tr("Value", "Ценность"), FormatBalance(gift.value));
	}
	AddField(
		box,
		Tr("Owner", "Владелец"),
		!mine
			? Tr("Not in your collection", "Нет в вашей коллекции")
			: profile.owner
			? (profile.displayName + u" ("_q + profile.flashgramId + ')')
			: profile.displayName);
	AddField(
		box,
		Tr("Source", "Источник"),
		QString::fromLatin1(kLocalSource));
	AddField(box, Tr("Description", "Описание"), gift.description);

	if (mine) {
		const auto layout = box->verticalLayout();
		Ui::AddDivider(layout);
		Ui::AddSkip(layout);
		const auto flagText = [=](
				GiftFlag flag,
				const char *onEn,
				const char *onRu,
				const char *offEn,
				const char *offRu) {
			return rpl::single(rpl::empty) | rpl::then(
				Changes()
			) | rpl::map([=] {
				const auto now = current();
				const auto on = now && ((flag == GiftFlag::Pinned)
					? now->pinned
					: now->inProfile);
				return on ? Tr(onEn, onRu) : Tr(offEn, offRu);
			});
		};
		Settings::AddButtonWithIcon(
			layout,
			flagText(GiftFlag::Pinned, "Unpin", "Открепить", "Pin", "Закрепить"),
			st::settingsButton,
			{ .icon = &st::menuIconPin }
		)->setClickedCallback([=] {
			if (const auto now = current()) {
				SetGiftFlag(user, now->uid, GiftFlag::Pinned, !now->pinned);
			}
		});
		Settings::AddButtonWithIcon(
			layout,
			flagText(
				GiftFlag::InProfile,
				"Remove from profile",
				"Убрать из профиля",
				"Show in profile",
				"В профиль"),
			st::settingsButton,
			{ .icon = &st::menuIconProfile }
		)->setClickedCallback([=] {
			if (const auto now = current()) {
				SetGiftFlag(
					user,
					now->uid,
					GiftFlag::InProfile,
					!now->inProfile);
			}
		});
		Settings::AddButtonWithIcon(
			layout,
			TrValue("Transfer", "Передать"),
			st::settingsButton,
			{ .icon = &st::menuIconSend }
		)->setClickedCallback([=] {
			if (const auto now = current()) {
				controller->show(Box(TransferGiftBox, controller, *now));
			}
		});
		Settings::AddButtonWithIcon(
			layout,
			TrValue("Sell", "Продать"),
			st::settingsButton,
			{ .icon = &st::menuIconEarn }
		)->setClickedCallback([=] {
			if (const auto now = current()) {
				controller->show(Box(SellGiftBox, controller, *now));
			}
		});
		Ui::AddSkip(layout);
	}

	if (justWon) {
		box->addButton(TrValue("To collection", "В коллекцию"), [=] {
			box->closeBox();
			controller->show(Box(MyGiftsBox, controller));
		});
	}
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void SellGiftBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		OwnedGift owned) {
	const auto session = &controller->session();
	box->setTitle(TrValue("Sell gift", "Продать подарок"));
	box->setWidth(st::boxWideWidth);
	const auto gift = FindGift(owned.giftId);
	if (gift) {
		AddGiftPreview(box, session, *gift, owned.number);
		AddField(box, Tr("Gift", "Подарок"), GiftTitle(*gift, owned.number));
	}
	const auto suggested = gift ? (gift->value.cents / 100) : 0;
	box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		TrValue("Price in FG", "Цена в FG"),
		QString::number(suggested)));
	Ui::AddSkip(box->verticalLayout());
	Ui::AddDividerText(
		box->verticalLayout(),
		TrValue(
			"Selling to other people needs the FlashGram server to move "
			"ownership safely. It is not available yet.",
			"Продажа другим людям требует сервер FlashGram, чтобы "
			"безопасно сменить владельца. Он пока недоступен."));
	box->addButton(
		TrValue("List for sale (soon)", "Выставить (скоро)"),
		[=] { ShowBackendRequired(controller); });
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void TransferGiftBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		OwnedGift owned) {
	const auto session = &controller->session();
	box->setTitle(TrValue("Transfer gift", "Передать подарок"));
	box->setWidth(st::boxWideWidth);
	if (const auto gift = FindGift(owned.giftId)) {
		AddGiftPreview(box, session, *gift, owned.number);
		AddField(box, Tr("Gift", "Подарок"), GiftTitle(*gift, owned.number));
	}
	box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		TrValue("Recipient FlashGram ID", "FlashGram ID получателя"),
		QString()));
	Ui::AddSkip(box->verticalLayout());
	Ui::AddDividerText(
		box->verticalLayout(),
		TrValue(
			"Transfers to other people need the FlashGram server so a "
			"gift always has a single owner. It is not available yet.",
			"Передача другим людям требует сервер FlashGram, чтобы у "
			"подарка всегда был один владелец. Он пока недоступен."));
	box->addButton(
		TrValue("Transfer (soon)", "Передать (скоро)"),
		[=] { ShowBackendRequired(controller); });
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace FlashGram
