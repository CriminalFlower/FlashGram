/*
This file is part of FlashGram,
a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "flashgram/flashgram_cover.h"

#include "core/file_utilities.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "flashgram/flashgram_state.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtGui/QImageReader>

namespace FlashGram {
namespace {

constexpr auto kMaxSide = 1920;
constexpr auto kMaxVideoSize = 200 * 1024 * 1024;

[[nodiscard]] QStringList VideoExtensions() {
	return {
		u"mp4"_q, u"m4v"_q, u"mov"_q, u"webm"_q,
		u"mkv"_q, u"avi"_q, u"gif"_q,
	};
}

struct Cached {
	QImage image;
	QString video;
	bool loaded = false;
};

[[nodiscard]] base::flat_map<uint64, Cached> &Cache() {
	static auto result = base::flat_map<uint64, Cached>();
	return result;
}

[[nodiscard]] rpl::event_stream<> &ChangesStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] QString CoverPath(uint64 userId) {
	return cWorkingDir()
		+ u"tdata/flashgram/cover_%1.png"_q.arg(userId);
}

[[nodiscard]] QImage ReadImage(const QString &path);

// Reads the largest track size from an MP4 / MOV "tkhd" box.
[[nodiscard]] QSize Mp4VideoSize(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto data = file.read(8 * 1024 * 1024);
	auto result = QSize();
	auto from = 0;
	while (true) {
		const auto index = data.indexOf("tkhd", from);
		if (index < 4) {
			break;
		}
		from = index + 4;
		const auto size = (uint32(uchar(data[index - 4])) << 24)
			| (uint32(uchar(data[index - 3])) << 16)
			| (uint32(uchar(data[index - 2])) << 8)
			| uint32(uchar(data[index - 1]));
		const auto end = index - 4 + int(size);
		if (size < 16 || end > data.size()) {
			continue;
		}
		const auto read = [&](int offset) {
			return int(((uint32(uchar(data[offset])) << 24)
				| (uint32(uchar(data[offset + 1])) << 16)
				| (uint32(uchar(data[offset + 2])) << 8)
				| uint32(uchar(data[offset + 3]))) >> 16);
		};
		const auto width = read(end - 8);
		const auto height = read(end - 4);
		if (width * height > result.width() * result.height()) {
			result = QSize(width, height);
		}
	}
	return result;
}

[[nodiscard]] QString VideoPrefix(uint64 userId) {
	return u"cover_%1_video."_q.arg(userId);
}

[[nodiscard]] QString FindVideo(uint64 userId) {
	const auto dir = QDir(cWorkingDir() + u"tdata/flashgram/"_q);
	const auto prefix = VideoPrefix(userId);
	for (const auto &name : dir.entryList(
			{ prefix + u"*"_q },
			QDir::Files)) {
		return dir.filePath(name);
	}
	return QString();
}

void RemoveVideos(uint64 userId) {
	while (true) {
		const auto path = FindVideo(userId);
		if (path.isEmpty() || !QFile::remove(path)) {
			break;
		}
	}
}

void EnsureLoaded(uint64 id, Cached &cached) {
	if (cached.loaded) {
		return;
	}
	cached.loaded = true;
	cached.video = FindVideo(id);
	const auto path = CoverPath(id);
	if (cached.video.isEmpty() && QFile::exists(path)) {
		cached.image = ReadImage(path);
	}
}

[[nodiscard]] QImage ReadImage(const QString &path) {
	auto reader = QImageReader(path);
	reader.setAutoTransform(true);
	auto image = reader.read();
	if (image.isNull()) {
		return {};
	}
	if (std::max(image.width(), image.height()) > kMaxSide) {
		image = image.scaled(
			kMaxSide,
			kMaxSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace

QImage ProfileCover(not_null<PeerData*> peer) {
	if (!peer->isSelf()) {
		return {};
	}
	const auto id = peer->id.value;
	auto &cached = Cache()[id];
	EnsureLoaded(id, cached);
	return cached.image;
}

QString ProfileCoverVideo(not_null<PeerData*> peer) {
	if (!peer->isSelf()) {
		return {};
	}
	const auto id = peer->id.value;
	auto &cached = Cache()[id];
	EnsureLoaded(id, cached);
	return cached.video;
}

bool HasProfileCover(not_null<PeerData*> peer) {
	return !ProfileCover(peer).isNull()
		|| !ProfileCoverVideo(peer).isEmpty();
}

void ChooseProfileCover(not_null<Window::SessionController*> controller) {
	const auto id = controller->session().user()->id.value;
	const auto filter = u"Images and videos ("_q
		+ u"*.jpg *.jpeg *.png *.webp *.bmp "_q
		+ u"*.mp4 *.m4v *.mov *.webm *.mkv *.avi *.gif);;"_q
		+ FileDialog::AllFilesFilter();
	const auto done = [=](FileDialog::OpenResult &&result) {
		const auto path = result.paths.isEmpty()
			? QString()
			: result.paths.front();
		const auto extension = QFileInfo(path).suffix().toLower();
		if (!path.isEmpty() && VideoExtensions().contains(extension)) {
			if (QFileInfo(path).size() > kMaxVideoSize) {
				controller->uiShow()->showToast(Tr(
					"The video is too big, up to 200 MB.",
					"Видео слишком большое, до 200 МБ."));
				return;
			}
			const auto size = Mp4VideoSize(path);
			if (size.width() * size.height() > 1920 * 1088) {
				controller->uiShow()->showToast(Tr(
					"Videos up to Full HD (1920x1080) are supported.",
					"Поддерживается видео до Full HD (1920x1080)."));
				return;
			}
			RemoveVideos(id);
			QFile::remove(CoverPath(id));
			const auto target = cWorkingDir()
				+ u"tdata/flashgram/"_q
				+ VideoPrefix(id)
				+ extension;
			QDir().mkpath(QFileInfo(target).absolutePath());
			if (!QFile::copy(path, target)) {
				LOG(("FlashGram Error: Could not copy the profile video."));
				return;
			}
			Cache()[id] = Cached{ .video = target, .loaded = true };
			ChangesStream().fire({});
			return;
		}
		auto image = path.isEmpty() ? QImage() : ReadImage(path);
		if (image.isNull()) {
			if (!result.remoteContent.isEmpty()) {
				image = QImage::fromData(result.remoteContent);
			}
			if (image.isNull()) {
				return;
			}
		}
		const auto target = CoverPath(id);
		QDir().mkpath(QFileInfo(target).absolutePath());
		RemoveVideos(id);
		if (!image.save(target, "PNG")) {
			LOG(("FlashGram Error: Could not save the profile cover."));
			return;
		}
		Cache()[id] = Cached{ .image = std::move(image), .loaded = true };
		ChangesStream().fire({});
	};
	FileDialog::GetOpenPath(
		controller->widget().get(),
		Tr("Choose profile background", "Выберите фон профиля"),
		filter,
		crl::guard(controller->widget().get(), done));
}

void RemoveProfileCover(not_null<PeerData*> peer) {
	const auto id = peer->id.value;
	QFile::remove(CoverPath(id));
	RemoveVideos(id);
	Cache()[id] = Cached{ .loaded = true };
	ChangesStream().fire({});
}

rpl::producer<> ProfileCoverChanges() {
	return rpl::single(rpl::empty) | rpl::then(ChangesStream().events());
}

} // namespace FlashGram
