// SPDX-License-Identifier: MIT
#if ESNE
#include "guis/GuiWindowsInstaller.h"

#include "ApiSystem.h"
#include "AudioManager.h"
#include "GuiLoading.h"
#include "LocaleES.h"
#include "Paths.h"
#include "Settings.h"
#include "VolumeControl.h"
#include "Window.h"
#include "components/OptionListComponent.h"
#include "guis/GuiFileBrowser.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiSettings.h"
#include "guis/GuiTextEditPopup.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "views/ViewController.h"

#include <memory>

namespace
{
	struct Source
	{
		std::string type;   // cdrom, image, installer, folder
		std::string id;     // what "esctl windows prepare" takes
		std::string name;   // proposed game name
		std::string detail; // disc label or file name
	};

	std::string quote(const std::string& value)
	{
		return "'" + Utils::String::replace(value, "'", "'\\''") + "'";
	}

	// Same rule as esctl: the name is used for files (shortcut, disc image) and for the Wine prefix
	bool isValidName(const std::string& name)
	{
		if (Utils::String::trim(name).empty() || name.size() > 100 || name[0] == '.')
			return false;

		for (unsigned char c : name)
			if (c < 0x20 || std::string("/\\:*?\"<>|").find(c) != std::string::npos)
				return false;

		return true;
	}

	std::vector<std::string> split(const std::string& line)
	{
		return Utils::String::split(line, '\t', false);
	}

	void showError(Window* window, const std::string& message)
	{
		window->pushGui(new GuiMsgBox(window, _("AN ERROR OCCURRED") + ":\r\n" + Utils::String::trim(message), _("OK"), nullptr, GuiMsgBoxIcon::ICON_ERROR));
	}

	// Closes every menu, reloads the game lists (the new game appears in WINDOWS) and tells which game was installed.
	// Posted to the UI thread: reloadAllGames deletes every GUI, including the one whose callback called us.
	void finished(Window* window, const std::string& name)
	{
		window->postToUiThread([window, name]
		{
			ViewController::reloadAllGames(window, true, true);
			window->pushGui(new GuiMsgBox(window, Utils::String::format(_("%s IS INSTALLED. YOU WILL FIND IT IN THE WINDOWS SYSTEM.").c_str(), name.c_str()), _("OK"), nullptr));
		});
	}

	void pickProgram(Window* window, const std::string& name, const std::vector<std::string>& lines);
	void prepare(Window* window, const Source& source, const std::string& name, const std::string& mode, const std::string& winver, bool silent);

	// The installer is a Windows program with its own window: ES steps aside as for a game (same steps as FileData::launchGame)
	void runInstaller(Window* window, const Source& source, const std::string& name, const std::string& mode, const std::string& winver, bool silent)
	{
		std::vector<std::string> lines;

		AudioManager::getInstance()->deinit();
		VolumeControl::getInstance()->deinit();

		bool hideWindow = Settings::getInstance()->getBool("HideWindow");
		window->deinit(hideWindow);

		auto result = ApiSystem::getInstance()->esctlWindows("install --name " + quote(name) + (winver.empty() ? "" : " --winver " + quote(winver)) + (silent ? "" : " --silent no"),
			[&lines](const std::string& line) { lines.push_back(line); });

		window->init(hideWindow);
		VolumeControl::getInstance()->init();
		AudioManager::getInstance()->init();
		window->normalizeNextUpdate();

		if (result.second == 0)
			finished(window, name);
		else if (result.second == 3)
			pickProgram(window, name, lines);
		else if (mode == "auto" && (source.type == "cdrom" || source.type == "image"))
		{
			// old InstallShield installers often fail under Wine: the game can be copied from the disc instead (es-genconfig --disc)
			window->pushGui(new GuiMsgBox(window, _("AN ERROR OCCURRED") + ":\r\n" + Utils::String::trim(result.first) + "\r\n" +
				_("TRY AGAIN BY COPYING THE GAME FROM THE DISC?"),
				_("YES"), [window, source, name] { prepare(window, source, name, "disc", "", false); },
				_("NO"), nullptr, GuiMsgBoxIcon::ICON_ERROR));
		}
		else
			showError(window, result.first);
	}

	void pickProgram(Window* window, const std::string& name, const std::vector<std::string>& lines)
	{
		auto s = new GuiSettings(window, _("CHOOSE THE GAME PROGRAM"));
		s->setSubTitle(name);

		for (auto line : lines)
		{
			auto fields = split(line);
			if (fields.size() < 3 || fields[0] != "candidate")
				continue;

			std::string program = fields[1];
			std::string label = program + "  (" + fields[2] + " KB)";

			s->addEntry(label, false, [window, s, name, program]
			{
				auto result = ApiSystem::getInstance()->esctlWindows("link " + quote(name) + " " + quote(program));
				if (result.second != 0)
				{
					showError(window, result.first);
					return;
				}

				Window* w = window;
				std::string n = name;
				delete s; // also deletes this lambda: only locals below
				finished(w, n);
			});
		}

		window->pushGui(s);
	}

	void prepare(Window* window, const Source& source, const std::string& name, const std::string& mode, const std::string& winver, bool silent)
	{
		std::string args = "prepare " + quote(source.id) + " --name " + quote(name) + " --mode " + mode;

		window->pushGui(new GuiLoading<std::pair<std::string, int>>(window, _("PREPARING THE INSTALLATION"),
			[args](IGuiLoadingHandler* gui)
			{
				return ApiSystem::getInstance()->esctlWindows(args, [gui](const std::string& line)
				{
					if (Utils::String::startsWith(line, ">>> "))
						gui->setText(line.substr(4));
				});
			},
			[window, source, name, mode, winver, silent](std::pair<std::string, int> result)
			{
				std::string last = Utils::String::trim(result.first);

				if (result.second != 0)
				{
					showError(window, last);
					return;
				}

				if (Utils::String::startsWith(last, "done"))
				{
					finished(window, name);
					return;
				}

				// "setup<TAB>inno|nsis|msi|unknown": a recognized installer runs without any window when silent is on
				auto fields = split(last);
				if (silent && fields.size() > 1 && fields[1] != "unknown")
				{
					runInstaller(window, source, name, mode, winver, silent);
					return;
				}

				window->pushGui(new GuiMsgBox(window,
					_("THE WINDOWS INSTALLER WILL NOW OPEN. A MOUSE AND KEYBOARD ARE RECOMMENDED. EMULATIONSTATION COMES BACK WHEN IT IS CLOSED."),
					_("CONTINUE"), [window, source, name, mode, winver, silent] { runInstaller(window, source, name, mode, winver, silent); },
					_("CANCEL"), [name] { ApiSystem::getInstance()->esctlWindows("cancel " + quote(name)); }));
			}));
	}

	void openOptions(Window* window, const Source& source)
	{
		auto s = new GuiSettings(window, _("INSTALL A WINDOWS GAME"));
		s->setSubTitle(source.type == "cdrom" ? _("DISC IN THE DRIVE") + (source.detail.empty() ? "" : ": " + source.detail) : source.id);

		auto name = std::make_shared<std::string>(source.name);
		s->addInputTextRow(_("GAME NAME"), *name, false,
			[name](Window* w, std::string title, std::string /*value*/, const std::function<void(std::string)>& onsave)
			{
				if (Settings::getInstance()->getBool("UseOSK"))
					w->pushGui(new GuiTextEditPopupKeyboard(w, title, *name, onsave, false));
				else
					w->pushGui(new GuiTextEditPopup(w, title, *name, onsave, false));
			},
			[name](std::string value) { *name = Utils::String::trim(value); });

		auto mode = std::make_shared<OptionListComponent<std::string>>(window, _("METHOD"), false);
		mode->add(_("AUTOMATIC"), "auto", true);
		mode->add(_("RUN THE INSTALLER"), "setup", false);
		if (source.type == "cdrom" || source.type == "image")
			mode->add(_("COPY THE GAME FROM THE DISC"), "disc", false);
		s->addWithDescription(_("METHOD"), _("COPY FROM THE DISC: FOR OLD GAMES WHOSE INSTALLER DOES NOT WORK; THE GAME RUNS WITH ITS DISC IMAGE."), mode);

		auto winver = std::make_shared<OptionListComponent<std::string>>(window, _("WINDOWS VERSION"), false);
		winver->add(_("AUTO"), "", true);
		for (auto version : { "win10", "win7", "winxp", "win2k", "win98", "win95" })
			winver->add(version, version, false);
		s->addWithDescription(_("WINDOWS VERSION"), _("DURING THE INSTALLATION. FOR AN OLD INSTALLER THAT STOPS BEFORE THE END: WINXP OR WIN98."), winver);

		auto silent = std::make_shared<OptionListComponent<std::string>>(window, _("SILENT INSTALLATION"), false);
		silent->add(_("AUTOMATIC"), "auto", true);
		silent->add(_("NO"), "no", false);
		s->addWithDescription(_("SILENT INSTALLATION"), _("WHEN THE INSTALLER IS RECOGNIZED (INNO SETUP, NSIS, MSI): NO WINDOW, DEFAULT FOLDER AND CHOICES."), silent);

		s->addEntry(_("INSTALL"), false, [window, s, source, name, mode, winver, silent]
		{
			if (!isValidName(*name))
			{
				window->pushGui(new GuiMsgBox(window, _("INVALID GAME NAME") + "\r\n" + "/ \\ : * ? \" < > |", _("OK"), nullptr));
				return;
			}

			Window* w = window;
			std::string m = mode->getSelected();
			std::string v = winver->getSelected();
			bool quiet = silent->getSelected() == "auto";
			Source src = source;
			std::string n = *name;
			delete s; // also deletes this lambda: only locals below
			prepare(w, src, n, m, v, quiet);
		});

		window->pushGui(s);
	}

	Source fromPath(const std::string& path)
	{
		Source source;
		source.id = path;
		source.detail = Utils::FileSystem::getFileName(path);

		std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(path));
		source.type = Utils::FileSystem::isDirectory(path) ? "folder" : (ext == ".iso" || ext == ".img" || ext == ".bin") ? "image" : "installer";

		std::string stem = Utils::FileSystem::isDirectory(path) ? source.detail : Utils::FileSystem::getStem(path);
		std::string lower = Utils::String::toLower(stem);
		if (lower == "setup" || lower == "install" || lower == "installer" || lower == "autorun")
			stem = Utils::FileSystem::getFileName(Utils::FileSystem::getParent(path));

		source.name = Utils::String::replace(stem, "_", " ");
		return source;
	}

	void openSources(Window* window, const std::vector<std::string>& lines)
	{
		auto s = new GuiSettings(window, _("WINDOWS GAMES"));
		s->addGroup(_("INSTALL A GAME FROM"));

		for (auto line : lines)
		{
			auto fields = split(line);
			if (fields.size() < 4)
				continue;

			Source source { fields[0], fields[1], fields[2], fields[3] };

			std::string label;
			if (source.type == "cdrom")
				label = _("DISC IN THE DRIVE") + (source.detail.empty() ? "" : ": " + source.detail);
			else
				label = source.detail;

			std::string description = source.type == "cdrom" ? Utils::String::replace(source.id, "cdrom:", "") : Utils::FileSystem::getParent(source.id);

			s->getMenu().addWithDescription(label, description, nullptr, [window, source] { openOptions(window, source); }, "", false, false);
		}

		s->addEntry(_("BROWSE..."), true, [window]
		{
			window->pushGui(new GuiFileBrowser(window, Paths::getHomePath(), "", GuiFileBrowser::FILES,
				[window](const std::string& path)
				{
					if (!path.empty())
						openOptions(window, fromPath(path));
				}, _("CHOOSE AN INSTALLER OR A DISC IMAGE")));
		});

		window->pushGui(s);
	}
}

void GuiWindowsInstaller::open(Window* window)
{
	window->pushGui(new GuiLoading<std::vector<std::string>>(window, _("SEARCHING FOR DISCS AND INSTALLERS"),
		[](IGuiLoadingHandler*) { return ApiSystem::getInstance()->getWindowsInstallerSources(); },
		[window](std::vector<std::string> lines) { openSources(window, lines); }));
}

#endif
