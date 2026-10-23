// SPDX-License-Identifier: MIT
#if ESNE
#include "guis/GuiChangePassword.h"

#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditPopup.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "LinuxApiSystem.h"
#include "LocaleES.h"
#include "Log.h"
#include "Settings.h"
#include "utils/StringUtil.h"
#include "Window.h"

#include <functional>
#include <thread>

static LinuxApiSystem* api()
{
	return static_cast<LinuxApiSystem*>(ApiSystem::getInstance());
}

// characters, not bytes (UTF-8)
static size_t utf8Length(const std::string& text)
{
	size_t length = 0;
	for (unsigned char c : text)
		if ((c & 0xC0) != 0x80)
			length++;

	return length;
}

// virtual keyboard when it is enabled (default), plain text field otherwise; an empty value (BACK, RESET) cancels
static void editText(Window* window, const std::string& title, const std::function<void(const std::string&)>& onOk)
{
	auto onValue = [onOk](const std::string& value) { if (!value.empty()) onOk(value); };

	if (Settings::getInstance()->getBool("UseOSK"))
		window->pushGui(new GuiTextEditPopupKeyboard(window, title, "", onValue, false));
	else
		window->pushGui(new GuiTextEditPopup(window, title, "", onValue, false));
}

bool GuiChangePassword::isSupported()
{
	return api()->esctlHas("password");
}

std::string GuiChangePassword::getState()
{
	return isSupported() ? api()->getPasswordState() : std::string();
}

void GuiChangePassword::open(Window* window)
{
	editText(window, _("NEW PASSWORD"), [window](const std::string& password)
	{
		if (utf8Length(password) < MIN_LENGTH)
		{
			window->pushGui(new GuiMsgBox(window, Utils::String::format(_("THE PASSWORD MUST HAVE AT LEAST %d CHARACTERS.").c_str(), MIN_LENGTH),
				_("OK"), [window] { open(window); }, ICON_WARNING));
			return;
		}

		askConfirmation(window, password);
	});
}

void GuiChangePassword::askConfirmation(Window* window, const std::string& password)
{
	editText(window, _("CONFIRM THE PASSWORD"), [window, password](const std::string& confirmation)
	{
		if (confirmation != password)
		{
			window->pushGui(new GuiMsgBox(window, _("THE TWO PASSWORDS ARE DIFFERENT."), _("OK"), [window] { open(window); }, ICON_WARNING));
			return;
		}

		apply(window, password);
	});
}

void GuiChangePassword::apply(Window* window, const std::string& password)
{
	int rc = api()->setPassword(password);
	LOG(LogInfo) << "GuiChangePassword: esctl password set -> " << rc;

	if (rc == 0)
	{
		std::string user = api()->getPasswordUser();
		window->pushGui(new GuiMsgBox(window, Utils::String::format(_("THE PASSWORD OF THE USER '%s' HAS BEEN CHANGED.").c_str(), user.c_str()),
			_("OK"), nullptr, ICON_INFORMATION));
	}
	else if (rc == 3)
		window->pushGui(new GuiMsgBox(window, _("THE PASSWORD WAS REFUSED."), _("OK"), [window] { open(window); }, ICON_ERROR));
	else
		window->pushGui(new GuiMsgBox(window, _("THE PASSWORD COULD NOT BE CHANGED."), _("OK"), nullptr, ICON_ERROR));
}

void GuiChangePassword::checkAtStartup(Window* window)
{
	// esctl and sudo take a moment: ES keeps starting meanwhile
	std::thread([window]
	{
		if (getState() != "default")
			return;

		std::string user = api()->getPasswordUser();
		window->postToUiThread([window, user]
		{
			window->pushGui(new GuiMsgBox(window,
				Utils::String::format(_("THE DEFAULT PASSWORD OF THE USER '%s' IS STILL IN USE: ANYONE ON THE NETWORK CAN LOG IN WITH IT. CHANGE IT NOW?").c_str(), user.c_str()),
				_("CHANGE"), [window] { open(window); },
				_("LATER"), nullptr, ICON_WARNING));
		});
	}).detach();
}

#endif
