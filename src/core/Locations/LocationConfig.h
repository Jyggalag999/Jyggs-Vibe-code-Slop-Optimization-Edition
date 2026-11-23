#pragma once
/*
* Locations (built-in Cartographer): configuration files.
*
* Reads the same files Cartographer 1.2.0 reads, so existing preset mods (Mojave Nights etc.) work unchanged:
*   Data\Config\Cartographer\Settings.ini                      feature switches + [Keywords] aliases
*   Data\Config\Cartographer\{NVR,Lighting,Fog,GameSettings}.ini
*   Data\Config\Cartographer\{NVR,Lighting,Fog,GameSettings}Presets\*.ini
*   Data\KEYWORDS\*.ini                                        cell / worldspace keyword lists
*
* This file has no engine dependencies (std + Win32 file listing only) so the offline test program can link it.
*/

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace Locations {

std::string Lower(const std::string& s);

/*
* Minimal INI parser that follows GetPrivateProfileString for what Cartographer's data relies on:
* ';' starts a comment only at the start of a line, keys and values are trimmed, section and key
* names compare case-insensitively, and with duplicate keys the first one wins.
*/
class IniFile {
public:
	struct Entry { std::string key, value; };
	struct Section { std::string name; std::vector<Entry> entries; };

	std::vector<Section> sections;

	// lowercase section -> (index of the first section with that name, lowercase key -> index of its first entry)
	struct Index { size_t section; std::unordered_map<std::string, size_t> keys; };
	std::unordered_map<std::string, Index> index;

	bool Load(const std::string& path);
	void Parse(const std::string& text);
	const Section* FindSection(const std::string& name) const;						// first section with that name
	const std::string* Get(const std::string& section, const std::string& key) const;	// first matching key, nullptr if absent
};

enum Feature { FeatureNVR = 0, FeatureLighting, FeatureFog, FeatureGameSettings, FeatureCount };

struct Preset {
	std::string name;	// file name without ".ini", original case
	std::string path;	// full path the file was read from (dataDir\Config\Cartographer\<folder>\<name>.ini)
	IniFile ini;
};

struct FeatureConfig {
	const char*	Name;			// "NVR", "Lighting", "Fog", "GameSettings"
	const char*	SwitchKey;		// key in Settings.ini [General]
	const char*	IniName;		// feature ini file name
	const char*	PresetFolder;	// preset folder name

	bool		SwitchOn = false;	// Settings.ini says the feature is on
	bool		HasIni = false;		// the feature ini exists
	std::string	IniPath;			// full path of the feature ini (set even when the file is missing)
	IniFile		Ini;
	std::unordered_map<std::string, Preset> Presets;	// by lowercase name
	std::vector<std::string> KeywordOrder;				// lowercase preset names, in the order Cartographer appends keyword presets
	std::vector<std::string> Excludes;					// Lighting only: [Exclude] editor IDs, lowercase

	bool Active() const { return SwitchOn && HasIni; }
};

struct Alias {
	std::string name;						// the keyword that triggers the alias
	std::vector<std::string> targets;		// preset names, split on ","
};

// One place a preset is used from (reverse lookup, see LocationConfig::FindPresetUses).
struct PresetUse {
	enum Kind { Override, Keyword, AliasKeyword };
	std::string editorId;	// cell / worldspace editor ID (lowercase for keyword uses, as written for overrides)
	Kind kind = Override;
	std::string via;		// Override: feature ini name; Keyword: KEYWORDS file; AliasKeyword: "alias (file)"
};

class LocationConfig {
public:
	typedef std::function<void(const std::string&)> LogFn;

	FeatureConfig Features[FeatureCount];
	std::vector<Alias> Aliases;
	// lowercase editor ID -> lowercase keywords (only keywords that are an alias or a preset name are kept)
	std::unordered_map<std::string, std::unordered_set<std::string>> Keywords;
	// lowercase editor ID -> (lowercase keyword -> KEYWORDS file name that first gave it)
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> KeywordSource;
	std::string Root;				// <dataDir>\Config\Cartographer
	std::string DataDir;
	std::string SettingsPath;		// Root\Settings.ini
	unsigned KeywordFiles = 0;
	unsigned KeywordLines = 0;

	LocationConfig();
	// dataDir: the game's "Data" folder (or any folder holding Config\Cartographer and KEYWORDS).
	void Load(const std::string& dataDir, const LogFn& log);
	bool PresetExists(Feature f, const std::string& name) const;
	// Every editor ID that ends up with the preset in its stack for a reason other than the Interior/Exterior/
	// DefaultPreset defaults: [Overrides] entries, a KEYWORDS keyword equal to the preset name, or an alias
	// (Settings.ini [Keywords]) whose targets include it. Built on demand, not at load.
	std::vector<PresetUse> FindPresetUses(Feature f, const std::string& presetName) const;
	// The files that gave this editor ID its keywords (KEYWORDS file names), deduplicated.
	std::vector<std::string> KeywordFilesFor(const std::string& editorId) const;
	bool HasKeyword(const std::string& editorId, const std::string& keywordLower) const;
};

}
