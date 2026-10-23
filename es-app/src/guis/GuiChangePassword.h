// SPDX-License-Identifier: MIT
#if ESNE
#pragma once

#include <string>

class Window;

// Password of the account running ES (esctl password), typed twice on the virtual keyboard (gamepad).
//  - SYSTEM SETTINGS > ADVANCED > SECURITY > CHANGE PASSWORD
//  - at startup, while the distribution's published default password is still in use ("esctl password state")
// The password is handed to esctl on its standard input, never on a command line.
class GuiChangePassword
{
public:
	static constexpr int MIN_LENGTH = 8; // same as esctl password (MIN_LENGTH)

	// true when "esctl password" is available here (menu shown)
	static bool isSupported();

	// keyboard "NEW PASSWORD", then "CONFIRM THE PASSWORD", then the result
	static void open(Window* window);

	// asks in the background and, if the default password is still in use, offers to change it (CHANGE / LATER)
	static void checkAtStartup(Window* window);

	// "default", "changed" or "" (unknown, or no esctl password)
	static std::string getState();

private:
	static void askConfirmation(Window* window, const std::string& password);
	static void apply(Window* window, const std::string& password);
};

#endif
