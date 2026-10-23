// SPDX-License-Identifier: MIT
#if ESNE
#pragma once

#include "ApiSystem.h"

#include <map>
#include <mutex>

// ApiSystem implementation for ES-NE (standalone Linux, no Batocera OS).
//
// The base ApiSystem drives the system through "batocera-xxx" scripts. Here:
//  - "batocera-xxx args" is translated to "esctl xxx args" (see translateCommand)
//  - menus that only make sense on the Batocera OS (install, format, plane mode...) are hidden; upgrade only with a
//    helper of the distribution (esctl upgrade)
//  - menus backed by esctl are shown only if "esctl has <command>" succeeds
class LinuxApiSystem : public ApiSystem
{
public:
	LinuxApiSystem();

	bool isScriptingSupported(ScriptId script) override;

	// "batocera-es-swissknife --emukill" -> "esctl swissknife --emukill"
	// Commands that do not start with "batocera-" are returned unchanged.
	static std::string translateCommand(const std::string& command);

	// Absolute path of an executable found in $PATH, or an empty string
	static std::string findInPath(const std::string& name);

	std::string getRunningBoard() override;
	std::string getHostsName() override;
	std::string getFreeSpaceSystemInfo() override;
	std::string getSevenZipCommand() override;
	std::vector<std::string> getAvailableStorageDevices() override;

	// Updates only through the distribution's helper ("esctl upgrade check|run"), otherwise the package manager.
	// canUpdate returns the offered version in output and keeps the installed one (getUpdateInstalledVersion).
	bool canUpdate(std::vector<std::string>& output) override;
	bool canLocalUpdate() override;
	bool canArchitectureUpdate(std::string& architecture) override;
	std::pair<std::string, int> updateSystem(const std::function<void(const std::string)>& func = nullptr, bool fromlocalmedia = false) override;

	bool launchKodi(Window* window) override;
	void suspend() override;

	// True if esctl is installed and reports support for the given command ("esctl has <command>").
	// The answer is cached: menus can ask every time they open without starting a process.
	bool esctlHas(const std::string& command);

	// Setup tool of the distribution ("esctl setup", e.g. rpi4deck-install): label of its main menu entry, or an
	// empty string when there is none. Cached like esctlHas.
	std::string getSetupToolTitle();

	// Gives the screen, sound and pads to the setup tool and takes them back when it ends; then reboots, shuts down or
	// restarts EmulationStation if the tool asks for it (exit codes 10, 11 and 12)
	void runSetupTool(Window* window);

	// Update helper of the distribution ("esctl upgrade", e.g. rpi4deck-update): label of its menu entry (empty: the
	// standard START UPDATE) and installed version read by the last canUpdate (empty if unknown)
	std::string getUpdateTitle();
	std::string getUpdateInstalledVersion();

	// Password of the account running ES ("esctl password", see GuiChangePassword): account name, state ("default"
	// while the distribution's published default password is in use, "changed", or "" when unknown) and change.
	// setPassword hands the password to esctl on its standard input and returns its exit code (0 done, 3 refused).
	std::string getPasswordUser();
	std::string getPasswordState();
	int setPassword(const std::string& password);

protected:
	bool executeScript(const std::string command) override;
	std::pair<std::string, int> executeScript(const std::string command, const std::function<void(const std::string)>& func) override;
	std::vector<std::string> executeEnumerationScript(const std::string command) override;

private:
	std::mutex mEsctlMutex;
	std::map<std::string, bool> mEsctlSupport;
	bool mSetupToolTitleRead = false;
	std::string mSetupToolTitle;
	bool mUpdateTitleRead = false;
	std::string mUpdateTitle;
	std::string mUpdateInstalledVersion;
};

#endif
