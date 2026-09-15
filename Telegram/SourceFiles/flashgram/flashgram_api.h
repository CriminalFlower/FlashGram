/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// FlashGram does not embed Telegram API credentials into the executable.
// Each user provides their own api_id / api_hash from my.telegram.org in
// flashgram_api.json next to FlashGram.exe (or in the working folder).
namespace FlashGram {

[[nodiscard]] bool HasApiCredentials();
[[nodiscard]] int32 ApiId();
[[nodiscard]] QString ApiHash();
[[nodiscard]] QString ApiCredentialsPath();

} // namespace FlashGram
