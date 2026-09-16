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

struct Cached {
	QImage image;
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
	if (!cached.loaded) {
		cached.loaded = true;
		const auto path = CoverPath(id);
		if (QFile::exists(path)) {
			cached.image = ReadImage(path);
		}
	}
	return cached.image;
}

bool HasProfileCover(not_null<PeerData*> peer) {
	return !ProfileCover(peer).isNull();
}

void ChooseProfileCover(not_null<Window::SessionController*> controller) {
	const auto id = controller->session().user()->id.value;
	const auto filter = FileDialog::ImagesOrAllFilter();
	const auto done = [=](FileDialog::OpenResult &&result) {
		const auto path = result.paths.isEmpty()
			? QString()
			: result.paths.front();
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
	Cache()[id] = Cached{ .loaded = true };
	ChangesStream().fire({});
}

rpl::producer<> ProfileCoverChanges() {
	return rpl::single(rpl::empty) | rpl::then(ChangesStream().events());
}

} // namespace FlashGram
