/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace FlashGram {

void AddProfileSection(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);

} // namespace FlashGram
