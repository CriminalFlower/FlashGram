/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

class UserData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// Identity features of a FlashGram profile. Data sources stay separate:
// - usernames and collectible (NFT) username details are official
//   Telegram / Fragment data, never generated here;
// - Major / Hold badges and phone display are FlashGram features;
// - NFT gift purchases open official stores only.
namespace FlashGram {

void AddUsernamesCard(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);

void AddOrgVerificationCard(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);

void PhoneDisplayBox(
	not_null<Ui::GenericBox*> box,
	not_null<UserData*> user);

void NftStoreBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

void OrgVerificationGrantBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

} // namespace FlashGram
