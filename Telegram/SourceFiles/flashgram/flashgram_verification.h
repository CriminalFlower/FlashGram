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

// FlashGram Verification is a FlashGram-only badge ("FG" mark), granted
// by FlashGram admins on the FlashGram server. It is not Telegram
// verification: it never uses the Telegram verified icon and never
// changes anything on Telegram servers.
namespace FlashGram {

void VerificationBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

void VerificationRequestsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

void PaintVerificationBadge(
	QPainter &p,
	const QRectF &rect,
	const QString &level);
[[nodiscard]] int VerificationBadgeWidth(int height);
[[nodiscard]] QString VerificationLevelName(const QString &level);

} // namespace FlashGram
