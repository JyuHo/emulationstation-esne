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
//  - menus that only make sense on the Batocera OS (upgrade, install, format, wifi...) are hidden
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

	// No OS updates: the system is managed by the distribution's package manager
	bool canUpdate(std::vector<std::string>& output) override;
	bool canLocalUpdate() override;
	bool canArchitectureUpdate(std::string& architecture) override;
	std::pair<std::string, int> updateSystem(const std::function<void(const std::string)>& func = nullptr, bool fromlocalmedia = false) override;

	bool launchKodi(Window* window) override;
	void suspend() override;

protected:
	bool executeScript(const std::string command) override;
	std::pair<std::string, int> executeScript(const std::string command, const std::function<void(const std::string)>& func) override;
	std::vector<std::string> executeEnumerationScript(const std::string command) override;

private:
	// True if esctl is installed and reports support for the given command ("esctl has <command>")
	bool esctlHas(const std::string& command);

	std::mutex mEsctlMutex;
	std::map<std::string, bool> mEsctlSupport;
};

#endif
