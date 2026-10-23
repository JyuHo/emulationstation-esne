// SPDX-License-Identifier: MIT
#if ESNE
#include "LinuxApiSystem.h"

#include "LocaleES.h"
#include "Log.h"
#include "Paths.h"
#include "utils/FileSystemUtil.h"
#include "utils/Platform.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
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
		// 'command' is always a constant from this code (isScriptingSupported, GuiMenu), never user input
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
	case ApiSystem::WINDOWSINSTALLER:
		return esctlHas("windows");
	case ApiSystem::BATOCERASTORE:
		return esctlHas("store"); // CONTENT DOWNLOADER: ports of the port-esne catalog
	case ApiSystem::WIFI:
		return esctlHas("wifi"); // NETWORK SETTINGS: NetworkManager or the distribution's helper
	case ApiSystem::BLUETOOTH:
		return esctlHas("bluetooth"); // CONTROLLER SETTINGS > BLUETOOTH: BlueZ (bluetoothctl)
	case ApiSystem::SERVICES:
		return esctlHas("services"); // SYSTEM SETTINGS > SERVICES: only through the distribution's helper
	case ApiSystem::UPGRADE:
		return esctlHas("upgrade"); // UPDATES & DOWNLOADS > SOFTWARE UPDATES: only through the distribution's helper

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

	// Batocera OS only, or handled by the distribution (systemd, pacman...)
	case ApiSystem::DISKFORMAT:
	case ApiSystem::OVERCLOCK:
	case ApiSystem::EVMAPY:
	case ApiSystem::BACKUP:
	case ApiSystem::INSTALL:
	case ApiSystem::SUPPORTFILE:
	case ApiSystem::UPGRADEVIATORRENT:
	case ApiSystem::READPLANEMODE:
	case ApiSystem::WRITEPLANEMODE:
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
// Updates: helper of the distribution (esctl upgrade), otherwise the distribution's package manager
// ---------------------------------------------------------------------------------------------

bool LinuxApiSystem::canUpdate(std::vector<std::string>& output)
{
	if (!esctlHas("upgrade"))
		return false;

	// line 1: offered version, line 2: installed version; exit code 0 when there is an update
	std::vector<std::string> lines;
	FILE* pipe = popen("esctl upgrade check 2>/dev/null", "r");
	if (pipe == nullptr)
		return false;

	char line[1024];
	while (fgets(line, sizeof(line), pipe))
	{
		std::string text = Utils::String::trim(Utils::String::replace(line, "\n", ""));
		if (!text.empty())
			lines.push_back(text);
	}

	int rc = pclose(pipe);
	rc = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : 1;

	{
		std::lock_guard<std::mutex> lock(mEsctlMutex);
		mUpdateInstalledVersion = lines.size() >= 2 ? lines[1] : std::string();
	}

	LOG(LogInfo) << "LinuxApiSystem::canUpdate: esctl upgrade check -> " << rc << (lines.empty() ? "" : " " + lines[0]);

	if (rc != 0 || lines.empty())
		return false;

	output.push_back(lines[0]);
	return true;
}

std::string LinuxApiSystem::getUpdateInstalledVersion()
{
	std::lock_guard<std::mutex> lock(mEsctlMutex);
	return mUpdateInstalledVersion;
}

std::string LinuxApiSystem::getUpdateTitle()
{
	if (!esctlHas("upgrade"))
		return std::string();

	if (!mUpdateTitleRead)
	{
		auto lines = executeEnumerationScript("esctl upgrade title");
		mUpdateTitle = lines.empty() ? std::string() : Utils::String::trim(lines[0]);
		mUpdateTitleRead = true;
	}

	return mUpdateTitle;
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
	// never the batocera-upgrade script downloaded by ApiSystem::updateSystem: only the distribution's helper
	if (fromlocalmedia || !esctlHas("upgrade"))
		return std::pair<std::string, int>("Updates are handled by the system package manager", 1);

	// progress lines ("text >>> NN%") for the notification, copied to esctl-upgrade.log
	FILE* pipe = popen("esctl upgrade run 2>&1", "r");
	if (pipe == nullptr)
		return std::pair<std::string, int>("Cannot call update command", -1);

	FILE* flog = fopen(Utils::FileSystem::combine(Paths::getLogPath(), "esctl-upgrade.log").c_str(), "w");

	std::string last;
	char line[1024];
	while (fgets(line, sizeof(line), pipe))
	{
		std::string text = Utils::String::replace(line, "\n", "");
		if (flog != nullptr)
			fprintf(flog, "%s\n", text.c_str());

		if (Utils::String::trim(text).empty())
			continue;

		last = text;
		if (func != nullptr)
			func(text);
	}

	int rc = pclose(pipe);
	rc = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : 1;

	if (flog != nullptr)
	{
		fprintf(flog, "Exit code : %d\n", rc);
		fclose(flog);
	}

	LOG(LogInfo) << "LinuxApiSystem::updateSystem: esctl upgrade run -> " << rc;
	return std::pair<std::string, int>(last, rc);
}

// ---------------------------------------------------------------------------------------------
// Password of the account running ES (esctl password)
// ---------------------------------------------------------------------------------------------

std::string LinuxApiSystem::getPasswordUser()
{
	auto lines = executeEnumerationScript("esctl password user");
	return lines.empty() ? std::string() : Utils::String::trim(lines[0]);
}

std::string LinuxApiSystem::getPasswordState()
{
	auto lines = executeEnumerationScript("esctl password state 2>/dev/null");
	std::string state = lines.empty() ? std::string() : Utils::String::trim(lines[0]);
	return (state == "default" || state == "changed") ? state : std::string();
}

int LinuxApiSystem::setPassword(const std::string& password)
{
	if (password.empty() || password.find('\n') != std::string::npos)
		return 3;

	// esctl may exit before reading (no helper): no SIGPIPE for ES
	struct sigaction ignore = {}, previous = {};
	ignore.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &ignore, &previous);

	int rc = 1;
	FILE* pipe = popen("esctl password set", "w");
	if (pipe != nullptr)
	{
		std::string line = password + "\n";
		fwrite(line.data(), 1, line.size(), pipe);
		std::fill(line.begin(), line.end(), '\0');
		int status = pclose(pipe);
		rc = (status != -1 && WIFEXITED(status)) ? WEXITSTATUS(status) : 1;
	}

	sigaction(SIGPIPE, &previous, nullptr);
	return rc;
}

// ---------------------------------------------------------------------------------------------
// Setup tool of the distribution (esctl setup)
// ---------------------------------------------------------------------------------------------

std::string LinuxApiSystem::getSetupToolTitle()
{
	if (!esctlHas("setup"))
		return std::string();

	if (!mSetupToolTitleRead)
	{
		auto lines = executeEnumerationScript("esctl setup title");
		mSetupToolTitle = lines.empty() ? std::string() : Utils::String::trim(lines[0]);
		if (mSetupToolTitle.empty())
			mSetupToolTitle = _("SYSTEM TOOLS");
		mSetupToolTitleRead = true;
	}

	return mSetupToolTitle;
}

void LinuxApiSystem::runSetupTool(Window* window)
{
	LOG(LogInfo) << "LinuxApiSystem::runSetupTool";

	launchExternalWindow_before(window);

	int rc = system("esctl setup run");
	rc = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : 1;

	launchExternalWindow_after(window);

	LOG(LogInfo) << "LinuxApiSystem::runSetupTool: exit code " << rc;

	switch (rc)
	{
	case 10:
		Utils::Platform::quitES(Utils::Platform::QuitMode::REBOOT);
		break;
	case 11:
		Utils::Platform::quitES(Utils::Platform::QuitMode::SHUTDOWN);
		break;
	case 12:
		Utils::Platform::quitES(Utils::Platform::QuitMode::RESTART);
		break;
	}
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
