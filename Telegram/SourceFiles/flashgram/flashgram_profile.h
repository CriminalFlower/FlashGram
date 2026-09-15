/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Ui {
class GenericBox;
class Show;
class VerticalLayout;
} // namespace Ui

namespace FlashGram {

struct OwnedGift;

void AddProfileSection(
	not_null<Ui::VerticalLayout*> container,
	std::shared_ptr<Ui::Show> show,
	not_null<UserData*> user);

void GiftsBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Ui::Show> show,
	not_null<UserData*> user);

void GiftDetailsBox(
	not_null<Ui::GenericBox*> box,
	not_null<UserData*> user,
	OwnedGift owned);

} // namespace FlashGram
