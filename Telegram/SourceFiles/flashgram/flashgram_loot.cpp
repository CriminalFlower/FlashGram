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
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_flashgram.h"
#include "styles/style_layers.h"

#include <QtGui/QPainterPath>

namespace FlashGram {
namespace {

constexpr auto kSpinCostCents = int64(2500);
constexpr auto kRouletteItems = 40;
constexpr auto kRouletteWinnerIndex = 32;
constexpr auto kSpinDuration = crl::time(5200);
constexpr auto kPreviewCount = 3;

[[nodiscard]] rpl::producer<QString> BalanceValue(not_null<UserData*> user) {
	return rpl::single(rpl::empty) | rpl::then(Changes()) | rpl::map([=] {
		return FormatBalance(LoadProfile(user).balance);
	});
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
		QColor(0x3A, 0x47, 0x6B),
		QColor(0x1E, 0x25, 0x3D),
		u"FlashGram Balance"_q,
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
			"Local FlashGram currency. Not Telegram Stars.",
			"Локальная валюта FlashGram. Это не Telegram Stars."),
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

[[nodiscard]] QStringList RoulettePreviewIds() {
	auto result = QStringList();
	const auto &catalog = GiftsCatalog();
	for (auto i = catalog.rbegin(); i != catalog.rend(); ++i) {
		if (result.size() >= kPreviewCount) {
			break;
		}
		result.push_back(i->id);
	}
	return result;
}

class RouletteStrip final : public Ui::RpWidget {
public:
	RouletteStrip(QWidget *parent, not_null<Main::Session*> session);

	void fill(const Gift *winner);
	void setOffset(float64 offset);
	[[nodiscard]] float64 winnerOffset() const;
	[[nodiscard]] int itemStep() const;

protected:
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
	not_null<Main::Session*> session)
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
		const auto fade = st::flashgramRouletteFade;
		const auto bg = st::boxBg->c;
		auto transparent = bg;
		transparent.setAlpha(0);
		auto left = QLinearGradient(0, 0, fade, 0);
		left.setColorAt(0., bg);
		left.setColorAt(1., transparent);
		p.fillRect(0, 0, fade, height, left);
		auto right = QLinearGradient(width - fade, 0, width, 0);
		right.setColorAt(0., transparent);
		right.setColorAt(1., bg);
		p.fillRect(width - fade, 0, fade, height, right);

		const auto marker = st::flashgramRouletteMarker;
		const auto center = width / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::activeButtonBg);
		auto top = QPainterPath();
		top.moveTo(center - marker, 0);
		top.lineTo(center + marker, 0);
		top.lineTo(center, marker * 1.2);
		top.closeSubpath();
		p.drawPath(top);
		auto bottom = QPainterPath();
		bottom.moveTo(center - marker, height);
		bottom.lineTo(center + marker, height);
		bottom.lineTo(center, height - marker * 1.2);
		bottom.closeSubpath();
		p.drawPath(bottom);
	}, _overlay->lifetime());
}

void RouletteStrip::fill(const Gift *winner) {
	_items.clear();
	_items.reserve(kRouletteItems);
	for (auto i = 0; i != kRouletteItems; ++i) {
		const auto gift = (winner && i == kRouletteWinnerIndex)
			? winner
			: RollGift({});
		if (!gift) {
			continue;
		}
		auto item = base::make_unique_q<LocalGiftView>(
			this,
			_session,
			*gift,
			gift->number,
			true);
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

int RouletteStrip::resizeGetHeight(int newWidth) {
	const auto height = st::flashgramRouletteHeight;
	_overlay->setGeometry(0, 0, newWidth, height);
	crl::on_main(this, [=] {
		layoutItems();
	});
	return height;
}

void RouletteStrip::layoutItems() {
	const auto itemWidth = st::flashgramRouletteItemWidth;
	const auto itemHeight = st::flashgramRouletteItemHeight;
	const auto first = width() / 2. - itemWidth / 2.;
	const auto top = (st::flashgramRouletteHeight - itemHeight) / 2;
	for (auto i = 0; i != int(_items.size()); ++i) {
		_items[i]->setGeometry(
			int(std::round(first + i * itemStep() - _offset)),
			top,
			itemWidth,
			itemHeight);
	}
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

void AddBalanceLine(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> user) {
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		BalanceValue(user) | rpl::map([](const QString &balance) {
			return Tr("Balance: ", "Баланс: ") + balance;
		}),
		st::boxLabel));
	Ui::AddSkip(box->verticalLayout());
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
		QColor(0x2F, 0xB5, 0x9B),
		QColor(0x1D, 0x6F, 0x8F),
		Tr("New drop", "Новый розыгрыш"),
		Tr(
			"New FlashGram Local gifts are in the cases.",
			"В кейсах новые подарки FlashGram Local."),
		false);

	const auto roulette = AddCard(
		box,
		st::flashgramPreviewCardHeight,
		QColor(0x5B, 0x6C, 0xF0),
		QColor(0x2A, 0x2F, 0x8F),
		Tr("Roulette", "Рулетка"),
		Tr("Spin for %1", "Прокрутка за %1").arg(
			FormatBalance({ .cents = kSpinCostCents })),
		true);
	AddCardPreviews(roulette, session, RoulettePreviewIds());
	roulette->setClickedCallback([=] {
		controller->show(Box(RouletteBox, controller));
	});

	Ui::AddSubsectionTitle(
		box->verticalLayout(),
		TrValue("Cases", "Кейсы"));
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

	box->addButton(TrValue("My Gifts", "Мои подарки"), [=] {
		controller->show(Box(MyGiftsBox, controller));
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void RouletteBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto user = session->user();
	box->setTitle(TrValue("Roulette", "Рулетка"));
	box->setWidth(st::boxWideWidth);
	RequestGiftStickers(session);

	AddBalanceLine(box, user);
	const auto strip = box->addRow(
		object_ptr<RouletteStrip>(box, session),
		QMargins());
	strip->fill(nullptr);
	Ui::AddSkip(box->verticalLayout());
	Ui::AddDividerText(
		box->verticalLayout(),
		TrValue(
			"Cosmetic FlashGram feature. Spins use only the local "
			"FlashGram Balance and give FlashGram Local gifts. "
			"No real money and no Telegram Stars are used.",
			"Косметическая функция FlashGram. Прокрутка тратит только "
			"локальный FlashGram Balance и даёт подарки FlashGram Local. "
			"Реальные деньги и Telegram Stars не используются."));

	struct State {
		Ui::Animations::Simple animation;
		bool spinning = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto spin = [=] {
		if (state->spinning) {
			return;
		}
		const auto winner = RollGift({});
		if (!winner) {
			return;
		} else if (!SpendBalance(user, { .cents = kSpinCostCents })) {
			controller->uiShow()->showToast(Tr(
				"Not enough FlashGram Balance.",
				"Недостаточно FlashGram Balance."));
			return;
		}
		state->spinning = true;
		strip->fill(winner);
		const auto step = strip->itemStep();
		const auto spread = std::max(step / 2, 1);
		const auto jitter = int(base::RandomValue<uint32>() % uint32(spread))
			- spread / 2;
		const auto target = strip->winnerOffset() + jitter;
		const auto owned = OwnedGift{
			.giftId = winner->id,
			.number = RollNumber(*winner),
		};
		state->animation.start([=](float64 value) {
			strip->setOffset(value);
			if (!state->animation.animating() && state->spinning) {
				state->spinning = false;
				AddInventoryGift(user, owned);
				controller->show(
					Box(LocalGiftDetailsBox, controller, owned, true));
			}
		}, 0., target, kSpinDuration, anim::easeOutCubic);
	};
	box->addButton(
		rpl::single(Tr("Spin · %1", "Крутить · %1").arg(
			FormatBalance({ .cents = kSpinCostCents }))),
		spin);
	box->addButton(TrValue("My Gifts", "Мои подарки"), [=] {
		controller->show(Box(MyGiftsBox, controller));
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

	AddBalanceLine(box, user);
	const auto header = AddCard(
		box,
		st::flashgramCaseCardHeight,
		entry.top.isValid() ? entry.top : QColor(0x4F, 0x8F, 0xE8),
		entry.bottom.isValid() ? entry.bottom : QColor(0x2A, 0x4F, 0xA3),
		entry.name,
		entry.description + u" · "_q + FormatBalance(entry.price),
		false);
	AddCardPreviews(header, session, entry.giftIds);

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

	box->addButton(
		rpl::single(Tr("Open · %1", "Открыть · %1").arg(
			FormatBalance(entry.price))),
		[=] {
			const auto winner = RollGift(entry.giftIds);
			if (!winner) {
				return;
			} else if (!SpendBalance(user, entry.price)) {
				controller->uiShow()->showToast(Tr(
					"Not enough FlashGram Balance.",
					"Недостаточно FlashGram Balance."));
				return;
			}
			const auto owned = OwnedGift{
				.giftId = winner->id,
				.number = RollNumber(*winner),
			};
			AddInventoryGift(user, owned);
			controller->show(
				Box(LocalGiftDetailsBox, controller, owned, true));
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
		? Tr("You got a gift!", "Вы получили подарок!")
		: gift.name));

	const auto previewRow = box->addRow(object_ptr<Ui::RpWidget>(box));
	previewRow->resize(
		previewRow->width(),
		st::flashgramDetailsPreviewSize.height());
	const auto view = Ui::CreateChild<LocalGiftView>(
		previewRow,
		session,
		gift,
		number,
		true);
	view->show();
	previewRow->widthValue() | rpl::on_next([=](int width) {
		const auto size = st::flashgramDetailsPreviewSize;
		view->setGeometry(
			(width - size.width()) / 2,
			0,
			size.width(),
			size.height());
	}, view->lifetime());
	Ui::AddSkip(box->verticalLayout());

	const auto profile = LoadProfile(user);
	const auto isOwned = ranges::contains(
		profile.gifts,
		gift.id,
		&OwnedGift::giftId);
	const auto collectible = (gift.kind == GiftKind::Collectible);
	AddField(box, Tr("Name", "Название"), gift.name);
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
			? Tr("Collectible-style", "Коллекционный стиль")
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
		!isOwned
			? Tr("Not in your collection", "Нет в вашей коллекции")
			: profile.owner
			? (profile.displayName + u" ("_q + profile.flashgramId + ')')
			: profile.displayName);
	AddField(
		box,
		Tr("Source", "Источник"),
		QString::fromLatin1(kLocalSource));
	AddField(box, Tr("Description", "Описание"), gift.description);
	Ui::AddDividerText(
		box->verticalLayout(),
		TrValue(
			"FlashGram Local gift, stored only on this device. It is not "
			"a Telegram Gift, not a Telegram collectible and not a "
			"blockchain asset.",
			"Подарок FlashGram Local, хранится только на этом устройстве. "
			"Это не Telegram Gift, не коллекционный подарок Telegram и "
			"не blockchain-актив."));

	if (justWon) {
		box->addButton(TrValue("My Gifts", "Мои подарки"), [=] {
			box->closeBox();
			controller->show(Box(MyGiftsBox, controller));
		});
	}
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

} // namespace FlashGram
