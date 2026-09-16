/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Window {
class SessionController;
} // namespace Window

namespace FlashGram {

// Picker of the animated FlashGram themes: Orange, Green, Red, Blue and
// Black, each with a live flowing preview.
void ThemesBox(not_null<Ui::GenericBox*> box);

// The user's own Telegram contacts laid out in alphabet blocks with
// phone numbers (when the contact shares it) and search.
void ContactsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

} // namespace FlashGram
