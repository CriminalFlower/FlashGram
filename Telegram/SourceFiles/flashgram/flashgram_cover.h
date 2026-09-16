/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace FlashGram {

// A local picture behind the name in your own profile. It is stored on
// this device only, other people don't see it.
[[nodiscard]] QImage ProfileCover(not_null<PeerData*> peer);
[[nodiscard]] bool HasProfileCover(not_null<PeerData*> peer);
void ChooseProfileCover(not_null<Window::SessionController*> controller);
void RemoveProfileCover(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<> ProfileCoverChanges();

} // namespace FlashGram
