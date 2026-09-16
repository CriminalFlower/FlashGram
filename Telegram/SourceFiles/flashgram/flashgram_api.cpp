/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_api.h"

#include "settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

#if __has_include("flashgram_api_embedded.h")
#include "flashgram_api_embedded.h"
#endif // __has_include("flashgram_api_embedded.h")

#ifndef FLASHGRAM_EMBEDDED_API_ID
#define FLASHGRAM_EMBEDDED_API_ID 0
#define FLASHGRAM_EMBEDDED_API_HASH ""
#endif // !FLASHGRAM_EMBEDDED_API_ID

namespace FlashGram {
namespace {

constexpr auto kFileName = "flashgram_api.json"_cs;
constexpr auto kHashMask = uchar(0x5D);

// Release builds embed the api_id / api_hash from the git-ignored
// flashgram_api.json at configure time (see Telegram/CMakeLists.txt).
// The hash is masked at compile time so the plain value is not stored
// as a string literal in the executable.
template <std::size_t Size>
struct MaskedString {
	constexpr MaskedString(const char (&value)[Size]) {
		for (auto i = std::size_t(); i != Size; ++i) {
			data[i] = char(uchar(value[i]) ^ uchar(kHashMask + i));
		}
	}

	[[nodiscard]] QString unmask() const {
		auto result = QString();
		result.reserve(int(Size));
		for (auto i = std::size_t(); i + 1 < Size; ++i) {
			result.append(QChar(char(uchar(data[i]) ^ uchar(kHashMask + i))));
		}
		return result;
	}

	std::array<char, Size> data = {};
};

constexpr auto kEmbeddedApiId = int32(FLASHGRAM_EMBEDDED_API_ID);
constexpr auto kEmbeddedApiHash = MaskedString(FLASHGRAM_EMBEDDED_API_HASH);

struct Credentials {
	int32 id = 0;
	QString hash;
	QString path;
};

[[nodiscard]] Credentials ReadFrom(const QString &path) {
	auto file = QFile(path);
	if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		LOG(("FlashGram Error: API credentials file is not valid JSON."));
		return {};
	}
	const auto object = document.object();
	const auto idValue = object.value(u"api_id"_q);
	const auto id = idValue.isString()
		? idValue.toString().trimmed().toInt()
		: idValue.toInt();
	const auto hash = object.value(u"api_hash"_q).toString().trimmed();
	static const auto kHashCheck = QRegularExpression(
		u"^[0-9a-fA-F]{32}$"_q);
	if (id <= 0 || !kHashCheck.match(hash).hasMatch()) {
		LOG(("FlashGram Error: API credentials file has invalid values."));
		return {};
	}
	return { .id = id, .hash = hash, .path = path };
}

[[nodiscard]] const Credentials &Loaded() {
	static const auto result = [] {
		const auto name = kFileName.utf16();
		for (const auto &folder : {
				QCoreApplication::applicationDirPath() + '/',
				cWorkingDir() }) {
			if (folder.isEmpty()) {
				continue;
			}
			auto credentials = ReadFrom(folder + name);
			if (credentials.id) {
				return credentials;
			}
		}
		if (kEmbeddedApiId > 0) {
			return Credentials{
				.id = kEmbeddedApiId,
				.hash = kEmbeddedApiHash.unmask(),
			};
		}
		return Credentials{
			.path = QCoreApplication::applicationDirPath() + '/' + name,
		};
	}();
	return result;
}

} // namespace

bool HasApiCredentials() {
	return Loaded().id != 0;
}

int32 ApiId() {
	return Loaded().id;
}

QString ApiHash() {
	return Loaded().hash;
}

QString ApiCredentialsPath() {
	return Loaded().path;
}

} // namespace FlashGram
