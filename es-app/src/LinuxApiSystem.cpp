// SPDX-License-Identifier: MIT
#if ESNE
#include "LinuxApiSystem.h"

#include "Log.h"
#include "Paths.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

LinuxApiSystem::LinuxApiSystem() : ApiSystem()
{
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

std::string LinuxApiSystem::translateCommand(const std::string& command)
{
	static const char* prefixes[] = { "/usr/bin/batocera-", "batocera-" };

	for (const char* prefix : prefixes)
	{
		if (!Utils::String::startsWith(command, prefix))
			continue;

		std::string rest = command.substr(strlen(prefix));

		// batocera-es-swissknife -> swissknife, batocera-es-thebezelproject -> thebezelproject
		if (Utils::String::startsWith(rest, "es-"))
			rest = rest.substr(3);

		return "esctl " + rest;
	}

	return command;
}

std::string LinuxApiSystem::findInPath(const std::string& name)
{
	if (name.empty())
		return std::string();

	if (name.find('/') != std::string::npos)
		return (access(name.c_str(), X_OK) == 0) ? name : std::string();

	const char* env = getenv("PATH");
	const std::string path = (env != nullptr && env[0] != '\0') ? env : "/usr/local/bin:/usr/bin:/bin";

	size_t start = 0;
	while (start <= path.size())
	{
		size_t end = path.find(':', start);
		if (end == std::string::npos)
			end = path.size();

		if (end > start)
		{
			const std::string candidate = path.substr(start, end - start) + "/" + name;

			struct stat st;
			if (stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(candidate.c_str(), X_OK) == 0)
				return candidate;
		}

		start = end + 1;
	}

	return std::string();
}

bool LinuxApiSystem::esctlHas(const std::string& command)
{
	std::lock_guard<std::mutex> lock(mEsctlMutex);

	auto it = mEsctlSupport.find(command);
	if (it != mEsctlSupport.end())
		return it->second;

	bool supported = false;

	if (!findInPath("esctl").empty())
	{
		// 'command' always comes from the constants in isScriptingSupported, never from user input
		int rc = system(("esctl has " + command + " >/dev/null 2>&1").c_str());
		supported = (rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0);
	}

	LOG(LogInfo) << "LinuxApiSystem: esctl has " << command << " -> " << (supported ? "yes" : "no");

	mEsctlSupport[command] = supported;
	return supported;
}

// ---------------------------------------------------------------------------------------------
// Command execution: batocera-xxx -> esctl xxx
// ---------------------------------------------------------------------------------------------

bool LinuxApiSystem::executeScript(const std::string command)
{
	return ApiSystem::executeScript(translateCommand(command));
}

std::pair<std::string, int> LinuxApiSystem::executeScript(const std::string command, const std::function<void(const std::string)>& func)
{
	return ApiSystem::executeScript(translateCommand(command), func);
}

std::vector<std::string> LinuxApiSystem::executeEnumerationScript(const std::string command)
{
	return ApiSystem::executeEnumerationScript(translateCommand(command));
}

// ---------------------------------------------------------------------------------------------
// Which menus are available
// ---------------------------------------------------------------------------------------------

bool LinuxApiSystem::isScriptingSupported(ScriptId script)
{
	switch (script)
	{
	// Provided by esctl: shown only if esctl is installed and implements the command
	case ApiSystem::RESOLUTION:
		return esctlHas("resolution");
	case ApiSystem::BIOSINFORMATION:
		return esctlHas("systems");
	case ApiSystem::PADSINFO:
		return esctlHas("padsinfo");
	case ApiSystem::TIMEZONES:
		return esctlHas("timezone");
	case ApiSystem::AUDIODEVICE:
		return esctlHas("audio");
	case ApiSystem::THEBEZELPROJECT:
		return esctlHas("thebezelproject");
	case ApiSystem::VERSIONINFO:
		return esctlHas("version");
	case ApiSystem::BATOCERAPREGAMELISTSHOOK:
		return esctlHas("preupdate-gamelists-hook");

	// Standard Linux tools
	case ApiSystem::KODI:
		return false; // ES-NE does not ship Kodi: hide its menu entries even if kodi is installed
	case ApiSystem::NETPLAY:
		return !getSevenZipCommand().empty();
	case ApiSystem::PDFEXTRACTION:
		return !findInPath("pdftoppm").empty() && !findInPath("pdfinfo").empty();
	case ApiSystem::SUSPEND:
		return !findInPath("systemctl").empty();

	// Resources: show the menu only if the folders exist (same rule as the Windows build)
	case ApiSystem::DECORATIONS:
		return Utils::FileSystem::exists(Paths::getUserDecorationsPath()) || Utils::FileSystem::exists(Paths::getDecorationsPath());
	case ApiSystem::SHADERS:
		return Utils::FileSystem::exists(Paths::getUserShadersPath()) || Utils::FileSystem::exists(Paths::getShadersPath());

	// Batocera OS only, or handled by the distribution (NetworkManager, BlueZ, systemd, pacman...)
	case ApiSystem::WIFI:
	case ApiSystem::BLUETOOTH:
	case ApiSystem::DISKFORMAT:
	case ApiSystem::OVERCLOCK:
	case ApiSystem::BATOCERASTORE:
	case ApiSystem::EVMAPY:
	case ApiSystem::BACKUP:
	case ApiSystem::INSTALL:
	case ApiSystem::SUPPORTFILE:
	case ApiSystem::UPGRADE:
	case ApiSystem::UPGRADEVIATORRENT:
	case ApiSystem::READPLANEMODE:
	case ApiSystem::WRITEPLANEMODE:
	case ApiSystem::SERVICES:
	case ApiSystem::BACKGLASS:
	case ApiSystem::NFC:
		return false;

	default:
		break;
	}

	// THEMESDOWNLOADER, RETROACHIVEMENTS, GAMESETTINGS, VIDEOFILTERS, PADSINFO...: base behaviour
	return ApiSystem::isScriptingSupported(script);
}

// ---------------------------------------------------------------------------------------------
// System information
// ---------------------------------------------------------------------------------------------

std::string LinuxApiSystem::getRunningBoard()
{
	// Batocera reads /boot/boot/batocera.board; a regular Linux install has no such notion
	return std::string();
}

std::string LinuxApiSystem::getHostsName()
{
	char name[256];
	if (gethostname(name, sizeof(name)) == 0)
	{
		name[sizeof(name) - 1] = '\0';
		if (name[0] != '\0')
			return std::string(name);
	}

	return "127.0.0.1";
}

std::string LinuxApiSystem::getFreeSpaceSystemInfo()
{
	// Batocera reports /boot; here the system partition is the root filesystem
	return getFreeSpaceInfo("/");
}

std::vector<std::string> LinuxApiSystem::getAvailableStorageDevices()
{
	// Batocera's "storage" setting (boot device / share partition) does not exist here
	return std::vector<std::string>();
}

std::string LinuxApiSystem::getSevenZipCommand()
{
	// Arch: 7zip provides 7zz and 7z, p7zip provides 7z/7za/7zr
	static const char* candidates[] = { "7zz", "7z", "7za", "7zr" };

	for (const char* candidate : candidates)
		if (!findInPath(candidate).empty())
			return candidate;

	return std::string();
}

// ---------------------------------------------------------------------------------------------
// Updates: handled by the distribution's package manager
// ---------------------------------------------------------------------------------------------

bool LinuxApiSystem::canUpdate(std::vector<std::string>& output)
{
	return false;
}

bool LinuxApiSystem::canLocalUpdate()
{
	return false;
}

bool LinuxApiSystem::canArchitectureUpdate(std::string& architecture)
{
	return false;
}

std::pair<std::string, int> LinuxApiSystem::updateSystem(const std::function<void(const std::string)>& func, bool fromlocalmedia)
{
	return std::pair<std::string, int>("Updates are handled by the system package manager", 1);
}

// ---------------------------------------------------------------------------------------------
// Power & external applications
// ---------------------------------------------------------------------------------------------

void LinuxApiSystem::suspend()
{
	LOG(LogDebug) << "LinuxApiSystem::suspend";
	executeScript("systemctl suspend");
}

bool LinuxApiSystem::launchKodi(Window* window)
{
	// Kodi is not part of ES-NE (isScriptingSupported(KODI) is false, so no menu reaches this)
	LOG(LogDebug) << "LinuxApiSystem::launchKodi: not supported";
	return false;
}

#endif
