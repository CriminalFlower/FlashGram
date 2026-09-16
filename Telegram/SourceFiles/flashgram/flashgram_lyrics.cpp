/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_lyrics.h"

#include "core/version.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QTextCodec>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace FlashGram {
namespace {

constexpr auto kMaxTagSize = 16 * 1024 * 1024;
constexpr auto kMaxSidecarSize = 2 * 1024 * 1024;
constexpr auto kPlainLineDuration = crl::time(4000);
constexpr auto kLastLineDuration = crl::time(5000);
constexpr auto kRequestTimeout = 10000;
constexpr auto kDurationTolerance = 6;
constexpr auto kCacheLimit = 128;
constexpr auto kApiUrl = "https://lrclib.net/api/"_cs;

[[nodiscard]] QNetworkAccessManager *Network() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return result;
}

[[nodiscard]] base::flat_map<QString, LyricsResult> &Cache() {
	static auto result = base::flat_map<QString, LyricsResult>();
	return result;
}

[[nodiscard]] uint32 ReadBigEndian(const QByteArray &data, int offset) {
	return (uint32(uchar(data[offset])) << 24)
		| (uint32(uchar(data[offset + 1])) << 16)
		| (uint32(uchar(data[offset + 2])) << 8)
		| uint32(uchar(data[offset + 3]));
}

[[nodiscard]] uint32 ReadSyncSafe(const QByteArray &data, int offset) {
	return (uint32(uchar(data[offset]) & 0x7F) << 21)
		| (uint32(uchar(data[offset + 1]) & 0x7F) << 14)
		| (uint32(uchar(data[offset + 2]) & 0x7F) << 7)
		| uint32(uchar(data[offset + 3]) & 0x7F);
}

[[nodiscard]] QString Decode(int encoding, const QByteArray &bytes) {
	switch (encoding) {
	case 1:
		if (const auto codec = QTextCodec::codecForName("UTF-16")) {
			return codec->toUnicode(bytes);
		}
		break;
	case 2:
		if (const auto codec = QTextCodec::codecForName("UTF-16BE")) {
			return codec->toUnicode(bytes);
		}
		break;
	case 3:
		return QString::fromUtf8(bytes);
	}
	return QString::fromLatin1(bytes);
}

// Returns the offset right after the terminator, or data.size().
[[nodiscard]] int FindStringEnd(
		const QByteArray &data,
		int offset,
		int encoding,
		int *textEnd) {
	const auto wide = (encoding == 1 || encoding == 2);
	if (wide) {
		for (auto i = offset; i + 1 < data.size(); i += 2) {
			if (!data[i] && !data[i + 1]) {
				*textEnd = i;
				return i + 2;
			}
		}
	} else {
		const auto zero = data.indexOf(char(0), offset);
		if (zero >= 0) {
			*textEnd = zero;
			return zero + 1;
		}
	}
	*textEnd = data.size();
	return data.size();
}

[[nodiscard]] QStringList CleanLines(const QString &text) {
	auto result = QStringList();
	auto normalized = text;
	normalized.replace(u"\r\n"_q, u"\n"_q).replace('\r', '\n');
	for (const auto &line : normalized.split('\n')) {
		const auto simplified = line.simplified();
		if (!simplified.isEmpty()) {
			result.push_back(simplified);
		}
	}
	return result;
}

[[nodiscard]] LyricsResult FromSylt(
		std::vector<std::pair<crl::time, QString>> entries) {
	ranges::stable_sort(entries, ranges::less(), [](const auto &entry) {
		return entry.first;
	});
	auto result = LyricsResult{ .synced = true };
	for (auto i = 0; i != int(entries.size()); ++i) {
		const auto text = entries[i].second.simplified();
		if (text.isEmpty()) {
			continue;
		}
		const auto till = (i + 1 < int(entries.size()))
			? entries[i + 1].first
			: (entries[i].first + kLastLineDuration);
		if (till > entries[i].first) {
			result.lines.push_back({
				.from = entries[i].first,
				.till = till,
				.text = text,
			});
		}
	}
	return result;
}

[[nodiscard]] LyricsResult ReadLocal(const QString &path, crl::time duration) {
	if (path.isEmpty()) {
		return {};
	}
	for (const auto &sidecar : FindTimedTextFiles(path)) {
		auto file = QFile(sidecar);
		if (file.size() > kMaxSidecarSize
			|| !file.open(QIODevice::ReadOnly)) {
			continue;
		}
		auto lines = ParseTimedText(file.readAll());
		if (!lines.empty()) {
			return { .lines = std::move(lines), .synced = true };
		}
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto header = file.read(10);
	if (header.size() < 10 || !header.startsWith("ID3")) {
		return {};
	}
	const auto size = int(ReadSyncSafe(header, 6));
	if (size <= 0 || size > kMaxTagSize) {
		return {};
	}
	auto result = ParseId3Lyrics(header + file.read(size));
	if (!result.synced && !result.lines.empty() && duration > 0) {
		// Plain USLT text is spread over the real track length.
		auto text = QStringList();
		for (const auto &line : result.lines) {
			text.push_back(line.text);
		}
		result = LyricsFromText(text.join('\n'), duration);
	}
	return result;
}

[[nodiscard]] QString CleanTitle(QString title) {
	static const auto brackets = QRegularExpression(
		u"\\s*[\\(\\[][^\\)\\]]*[\\)\\]]"_q);
	static const auto featuring = QRegularExpression(
		u"\\s+(feat\\.?|ft\\.?|prod\\.?)\\s.*$"_q,
		QRegularExpression::CaseInsensitiveOption);
	title.remove(brackets);
	title.remove(featuring);
	return title.simplified();
}

[[nodiscard]] LyricsResult FromApiObject(
		const QJsonObject &object,
		crl::time duration) {
	if (object.value(u"instrumental"_q).toBool()) {
		return {};
	}
	const auto synced = object.value(u"syncedLyrics"_q).toString();
	if (!synced.isEmpty()) {
		auto lines = ParseTimedText(synced.toUtf8());
		if (!lines.empty()) {
			return { .lines = std::move(lines), .synced = true };
		}
	}
	const auto plain = object.value(u"plainLyrics"_q).toString();
	if (!plain.isEmpty()) {
		const auto length = duration > 0
			? duration
			: crl::time(object.value(u"duration"_q).toDouble() * 1000);
		return LyricsFromText(plain, length);
	}
	return {};
}

void Request(
		const QString &method,
		QUrlQuery query,
		Fn<void(int, QByteArray)> done) {
	auto url = QUrl(kApiUrl.utf16() + method);
	url.setQuery(query);
	auto request = QNetworkRequest(url);
	request.setRawHeader(
		"User-Agent",
		"FlashGram/" + FlashGramVersionStr.utf8()
			+ " (https://github.com/CriminalFlower/FlashGram)");
	request.setRawHeader("Accept", "application/json");
	request.setTransferTimeout(kRequestTimeout);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = Network()->get(request);
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		reply->deleteLater();
		const auto code = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		done(code, reply->readAll());
	});
}

void Search(
		const QString &title,
		const QString &performer,
		crl::time duration,
		Fn<void(LyricsResult)> done) {
	auto query = QUrlQuery();
	if (performer.isEmpty()) {
		query.addQueryItem(u"q"_q, title);
	} else {
		query.addQueryItem(u"track_name"_q, title);
		query.addQueryItem(u"artist_name"_q, performer);
	}
	Request(u"search"_q, query, [=](int code, QByteArray bytes) {
		if (code != 200) {
			done({});
			return;
		}
		const auto list = QJsonDocument::fromJson(bytes).array();
		const auto seconds = duration / 1000;
		auto best = LyricsResult();
		auto bestScore = -1;
		for (const auto &value : list) {
			const auto object = value.toObject();
			const auto length = int(object.value(u"duration"_q).toDouble());
			const auto close = (seconds <= 0)
				|| (std::abs(length - seconds) <= kDurationTolerance);
			const auto hasSynced = !object.value(
				u"syncedLyrics"_q).toString().isEmpty();
			const auto score = (close ? 2 : 0) + (hasSynced ? 1 : 0);
			if (score <= bestScore || (!close && seconds > 0)) {
				continue;
			}
			auto result = FromApiObject(object, duration);
			if (!result.lines.empty()) {
				best = std::move(result);
				bestScore = score;
			}
		}
		done(std::move(best));
	});
}

void FetchOnline(const LyricsQuery &query, Fn<void(LyricsResult)> done) {
	auto title = query.title.trimmed();
	auto performer = query.performer.trimmed();
	if (title.isEmpty() && !query.fileName.isEmpty()) {
		auto name = query.fileName;
		const auto dot = name.lastIndexOf('.');
		if (dot > 0) {
			name = name.left(dot);
		}
		name.replace('_', ' ');
		const auto dash = name.indexOf(u" - "_q);
		if (dash > 0) {
			performer = name.left(dash).trimmed();
			title = name.mid(dash + 3).trimmed();
		} else {
			title = name.trimmed();
		}
	}
	title = CleanTitle(title);
	if (title.isEmpty()) {
		done({});
		return;
	}
	const auto duration = query.duration;
	const auto key = performer.toLower()
		+ '\n' + title.toLower()
		+ '\n' + QString::number(duration / 1000);
	const auto &cache = Cache();
	if (const auto i = cache.find(key); i != end(cache)) {
		done(i->second);
		return;
	}
	const auto finish = [=](LyricsResult result) {
		auto &cache = Cache();
		if (cache.size() >= kCacheLimit) {
			cache.erase(begin(cache));
		}
		cache[key] = result;
		done(std::move(result));
	};
	if (performer.isEmpty()) {
		Search(title, QString(), duration, finish);
		return;
	}
	auto params = QUrlQuery();
	params.addQueryItem(u"track_name"_q, title);
	params.addQueryItem(u"artist_name"_q, performer);
	if (duration > 0) {
		params.addQueryItem(
			u"duration"_q,
			QString::number(duration / 1000));
	}
	Request(u"get"_q, params, [=](int code, QByteArray bytes) {
		if (code == 200) {
			auto result = FromApiObject(
				QJsonDocument::fromJson(bytes).object(),
				duration);
			if (!result.lines.empty()) {
				finish(std::move(result));
				return;
			}
		}
		Search(title, performer, duration, finish);
	});
}

} // namespace

LyricsResult ParseId3Lyrics(const QByteArray &data) {
	if (data.size() < 10 || !data.startsWith("ID3")) {
		return {};
	}
	const auto major = int(uchar(data[3]));
	if (major != 3 && major != 4) {
		return {};
	}
	const auto flags = uchar(data[5]);
	const auto end = std::min(
		int(ReadSyncSafe(data, 6)) + 10,
		int(data.size()));
	auto position = 10;
	if ((flags & 0x40) && position + 4 <= end) {
		position += (major == 4)
			? int(ReadSyncSafe(data, position))
			: int(ReadBigEndian(data, position)) + 4;
	}
	auto plain = QString();
	auto synced = std::vector<std::pair<crl::time, QString>>();
	while (position + 10 <= end) {
		if (!data[position]) {
			break; // Padding.
		}
		const auto id = data.mid(position, 4);
		const auto size = int((major == 4)
			? ReadSyncSafe(data, position + 4)
			: ReadBigEndian(data, position + 4));
		const auto frameFlags = uchar(data[position + 9]);
		const auto bodyStart = position + 10;
		if (size <= 0 || bodyStart + size > end) {
			break;
		}
		position = bodyStart + size;
		const auto compressed = (major == 4)
			? (frameFlags & 0x0F)
			: (frameFlags & 0xC0);
		if (compressed) {
			continue;
		}
		const auto body = data.mid(bodyStart, size);
		const auto encoding = int(uchar(body[0]));
		if (id == "USLT" && plain.isEmpty() && body.size() > 4) {
			auto textEnd = 0;
			const auto textStart = FindStringEnd(body, 4, encoding, &textEnd);
			plain = Decode(encoding, body.mid(textStart));
		} else if (id == "SYLT" && synced.empty() && body.size() > 6) {
			const auto milliseconds = (uchar(body[4]) == 2);
			auto textEnd = 0;
			auto offset = FindStringEnd(body, 6, encoding, &textEnd);
			while (milliseconds && offset < body.size()) {
				const auto start = offset;
				offset = FindStringEnd(body, offset, encoding, &textEnd);
				if (offset + 4 > body.size()) {
					break;
				}
				const auto text = Decode(
					encoding,
					body.mid(start, textEnd - start));
				synced.emplace_back(
					crl::time(ReadBigEndian(body, offset)),
					text);
				offset += 4;
			}
		}
	}
	if (!synced.empty()) {
		auto result = FromSylt(std::move(synced));
		if (!result.lines.empty()) {
			return result;
		}
	}
	return plain.isEmpty() ? LyricsResult() : LyricsFromText(plain, 0);
}

LyricsResult LyricsFromText(const QString &text, crl::time duration) {
	static const auto stamp = QRegularExpression(
		u"\\[\\d{1,3}:\\d{1,2}"_q);
	if (stamp.match(text).hasMatch()) {
		auto lines = ParseTimedText(text.toUtf8());
		if (!lines.empty()) {
			return { .lines = std::move(lines), .synced = true };
		}
	}
	const auto list = CleanLines(text);
	if (list.isEmpty()) {
		return {};
	}
	const auto count = int(list.size());
	const auto step = (duration > 0)
		? std::max(duration / count, crl::time(1))
		: kPlainLineDuration;
	auto result = LyricsResult{ .synced = false };
	for (auto i = 0; i != count; ++i) {
		result.lines.push_back({
			.from = i * step,
			.till = (i + 1) * step,
			.text = list[i],
		});
	}
	return result;
}

void ResolveLyrics(LyricsQuery query, Fn<void(LyricsResult)> done) {
	crl::async([=] {
		auto local = ReadLocal(query.localPath, query.duration);
		crl::on_main([=, local = std::move(local)]() mutable {
			if (!local.lines.empty()) {
				done(std::move(local));
			} else {
				FetchOnline(query, done);
			}
		});
	});
}

} // namespace FlashGram
