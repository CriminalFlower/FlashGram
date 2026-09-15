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

namespace FlashGram {
namespace {

constexpr auto kFileName = "flashgram_api.json"_cs;

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
