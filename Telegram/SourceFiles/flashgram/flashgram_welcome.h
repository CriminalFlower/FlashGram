/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "flashgram/flashgram_server.h"
#include "ui/layers/generic_box.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace FlashGram {

void OnApplicationStarted(not_null<Window::Controller*> window);

void RegistrationBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	Fn<void()> registered,
	Fn<void()> dismissed);

void WelcomeBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	Fn<void()> done);
void EmojiSetsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void UpdateBox(not_null<Ui::GenericBox*> box, Server::Release release);

} // namespace FlashGram
