/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "flashgram/flashgram_state.h"
#include "ui/layers/generic_box.h"

namespace Window {
class SessionController;
} // namespace Window

// Roulette and cases are a local cosmetic feature: they only spend the
// local FlashGram Balance (FG) and only grant FlashGram Local gifts.
// No real money, no Telegram Stars and no server requests are involved.
namespace FlashGram {

void LootBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

void RouletteBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

void CaseBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	QString caseId);

void CasesBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

void SellGiftBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	OwnedGift owned);

void TransferGiftBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	OwnedGift owned);

void MyGiftsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

void LocalGiftDetailsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	OwnedGift owned,
	bool justWon);

} // namespace FlashGram
