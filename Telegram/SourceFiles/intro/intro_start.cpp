/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_start.h"

#include "core/version.h"
#include "flashgram/flashgram_state.h"
#include "lang/lang_keys.h"
#include "intro/intro_qr.h"
#include "intro/intro_phone.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "main/main_account.h"
#include "main/main_app_config.h"

namespace Intro {
namespace details {

StartWidget::StartWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data, true) {
	setMouseTracking(true);
	setTitleText(rpl::single(AppName.utf16()));
	setDescriptionText(FlashGram::TrValue(
		"A fast unofficial Telegram client\nwith extra FlashGram features.",
		"Быстрый неофициальный Telegram-клиент\n"
		"с дополнительными функциями FlashGram."));
	show();
}

void StartWidget::submit() {
	if (!FlashGram::HasApiCredentials()) {
		setDescriptionText(rpl::single(FlashGram::Tr(
			"Create flashgram_api.json next to FlashGram.exe with your "
			"api_id and api_hash from my.telegram.org, then restart.",
			"Создайте flashgram_api.json рядом с FlashGram.exe со своими "
			"api_id и api_hash с my.telegram.org и перезапустите.")));
		return;
	}
	account().destroyStaleAuthorizationKeys();
	goNext<QrWidget>();
}

rpl::producer<QString> StartWidget::nextButtonText() const {
	return tr::lng_start_msgs();
}

rpl::producer<> StartWidget::nextButtonFocusRequests() const {
	return _nextButtonFocusRequests.events();
}

void StartWidget::activate() {
	Step::activate();
	setInnerFocus();
}

void StartWidget::setInnerFocus() {
	_nextButtonFocusRequests.fire({});
}

} // namespace details
} // namespace Intro
