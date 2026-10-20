#pragma once

// ES-NE: developer keys (ScreenScraper, TheGamesDB, HFS, RetroAchievements).
//  - build-time defaults: the file named by ESNE_KEYS_FILE at configure time is masked into EsneEmbeddedKeys.h
//    (tools/esne_embed_keys.py, generated in the build folder, never committed); without it nothing is embedded;
//  - runtime: <config>/keys.conf (~/.config/esne/keys.conf) takes precedence, key by key.
// Format of both files: one "NAME=value" per line, '#' starts a comment, optional quotes around the value.
// keys.conf is read very early (static initialisation of the scraper list), so problems go to stderr, not to the log.
// Names: SCREENSCRAPER_SOFTNAME, SCREENSCRAPER_DEV_LOGIN (devid=...&devpassword=...), GAMESDB_APIKEY,
// HFS_DEV_LOGIN (user:password), CHEEVOS_DEV_LOGIN (z=user&y=api key).
#if ESNE

#include <string>
#include <map>
#include <fstream>
#include <sys/stat.h>
#include "Paths.h"
#include "EsneEmbeddedKeys.h" // generated at configure time
#include <cstdio>

namespace EsneKeys
{
	inline std::string trim(const std::string& s)
	{
		const char* ws = " \t\r\n";
		size_t b = s.find_first_not_of(ws);
		if (b == std::string::npos)
			return "";
		size_t e = s.find_last_not_of(ws);
		return s.substr(b, e - b + 1);
	}

	inline std::map<std::string, std::string> load(const std::string& file)
	{
		std::map<std::string, std::string> keys;

		std::ifstream in(file);
		if (!in.is_open())
			return keys;

		struct stat st;
		if (stat(file.c_str(), &st) == 0 && (st.st_mode & 077) != 0)
			fprintf(stderr, "ES-NE: %s is readable by other users: run chmod 600 on it\n", file.c_str());

		std::string line;
		while (std::getline(in, line))
		{
			line = trim(line);
			if (line.empty() || line[0] == '#')
				continue;

			size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;

			std::string name = trim(line.substr(0, eq));
			std::string value = trim(line.substr(eq + 1));
			if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
				value = value.substr(1, value.size() - 2);

			if (!name.empty() && !value.empty())
				keys[name] = value;
		}

		return keys;
	}

	// Value of a key, empty when it is not set.
	inline const std::string& get(const std::string& name)
	{
		static const std::map<std::string, std::string> keys = [] {
			std::map<std::string, std::string> all;

			// build-time defaults
			for (const embedded::Entry* e = embedded::entries; e->name != nullptr; e++)
			{
				std::string value;
				for (size_t i = 0; i < e->size; i++)
					value.push_back((char) (e->data[i] ^ embedded::pad[i % sizeof(embedded::pad)]));
				all[e->name] = value;
			}

			// keys.conf takes precedence
			for (const auto& kv : load(Paths::getUserEmulationStationPath() + "/keys.conf"))
				all[kv.first] = kv.second;

			return all;
		}();
		static const std::string empty;

		auto it = keys.find(name);
		return it == keys.end() ? empty : it->second;
	}

	inline std::string softName()
	{
		// copy: get() returns a reference into a static map, but GCC 13+ (-Wdangling-reference) cannot see it
		// and warns because the argument is a temporary std::string
		std::string value = get("SCREENSCRAPER_SOFTNAME");
		return value.empty() ? std::string("ES-NE") : value;
	}

	inline const std::string& screenscraperDevLogin() { return get("SCREENSCRAPER_DEV_LOGIN"); }
	inline const std::string& gamesDbApiKey() { return get("GAMESDB_APIKEY"); }
	inline const std::string& hfsDevLogin() { return get("HFS_DEV_LOGIN"); }
	inline const std::string& cheevosDevLogin() { return get("CHEEVOS_DEV_LOGIN"); }
}

#endif
