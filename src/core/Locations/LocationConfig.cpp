#include "LocationConfig.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace Locations {

std::string Lower(const std::string& s) {
	std::string r = s;
	for (auto& c : r) c = (char)tolower((unsigned char)c);
	return r;
}

static std::string Trim(const std::string& s) {
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
	return s.substr(b, e - b);
}

// Like Sv_Split: every character in "delims" is a separator (so ", " splits on commas and on spaces).
// Empty pieces are kept; they never match anything.
static std::vector<std::string> Split(const std::string& s, const char* delims) {
	std::vector<std::string> out;
	if (s.empty()) return out;
	size_t pos = 0;
	while (true) {
		size_t f = s.find_first_of(delims, pos);
		if (f == std::string::npos) { out.push_back(s.substr(pos)); break; }
		out.push_back(s.substr(pos, f - pos));
		pos = f + 1;
	}
	return out;
}

/* ---------------------------------------------------------------- IniFile */

bool IniFile::Load(const std::string& path) {
	sections.clear();
	index.clear();
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	std::stringstream ss;
	ss << f.rdbuf();
	Parse(ss.str());
	return true;
}

void IniFile::Parse(const std::string& text) {
	sections.clear();
	index.clear();
	Section* cur = nullptr;
	size_t pos = 0;
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) pos = 3;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		std::string line = Trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
		pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
		if (line.empty() || line[0] == ';') continue;
		if (line[0] == '[') {
			size_t close = line.find(']');
			if (close == std::string::npos) continue;
			sections.push_back(Section());
			sections.back().name = Trim(line.substr(1, close - 1));
			cur = &sections.back();
			continue;
		}
		if (!cur) continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		Entry e;
		e.key = Trim(line.substr(0, eq));
		e.value = Trim(line.substr(eq + 1));
		if (e.key.empty()) continue;
		cur->entries.push_back(e);
	}
	for (size_t i = 0; i < sections.size(); i++) {
		auto ins = index.insert({ Lower(sections[i].name), Index() });
		if (!ins.second) continue; // first section with a name wins
		ins.first->second.section = i;
		for (size_t j = 0; j < sections[i].entries.size(); j++) ins.first->second.keys.insert({ Lower(sections[i].entries[j].key), j });
	}
}

const IniFile::Section* IniFile::FindSection(const std::string& name) const {
	auto it = index.find(Lower(name));
	return it == index.end() ? nullptr : &sections[it->second.section];
}

const std::string* IniFile::Get(const std::string& section, const std::string& key) const {
	auto it = index.find(Lower(section));
	if (it == index.end()) return nullptr;
	auto k = it->second.keys.find(Lower(key));
	if (k == it->second.keys.end()) return nullptr;
	return &sections[it->second.section].entries[k->second].value;
}

/* ---------------------------------------------------------------- loading */

static std::vector<std::string> ListFiles(const std::string& dir, const char* ext) {
	std::vector<std::string> out;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*" + ext).c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	std::string extLower = Lower(ext);
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		std::string name = fd.cFileName;
		// FindFirstFile can also match short names; only accept an exact extension.
		if (name.size() <= extLower.size() || Lower(name.substr(name.size() - extLower.size())) != extLower) continue;
		out.push_back(name);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return out;
}

LocationConfig::LocationConfig() {
	static const char* names[FeatureCount][4] = {
		{ "NVR",          "bNVR",              "NVR.ini",          "NVRPresets" },
		{ "Lighting",     "bInteriorLighting", "Lighting.ini",     "LightingPresets" },
		{ "Fog",          "bFogControl",       "Fog.ini",          "FogPresets" },
		{ "GameSettings", "bGameSettings",     "GameSettings.ini", "GameSettingsPresets" },
	};
	for (int i = 0; i < FeatureCount; i++) {
		Features[i].Name = names[i][0];
		Features[i].SwitchKey = names[i][1];
		Features[i].IniName = names[i][2];
		Features[i].PresetFolder = names[i][3];
	}
}

bool LocationConfig::PresetExists(Feature f, const std::string& name) const {
	return Features[f].Presets.count(Lower(name)) != 0;
}

bool LocationConfig::HasKeyword(const std::string& editorId, const std::string& keywordLower) const {
	auto it = Keywords.find(Lower(editorId));
	return it != Keywords.end() && it->second.count(keywordLower) != 0;
}

std::vector<PresetUse> LocationConfig::FindPresetUses(Feature f, const std::string& presetName) const {
	std::vector<PresetUse> out;
	const FeatureConfig& fc = Features[f];
	std::string pl = Lower(presetName);
	if (!fc.Presets.count(pl)) return out;
	if (const IniFile::Section* s = fc.Ini.FindSection("Overrides")) {
		for (auto& e : s->entries) {
			for (auto& t : Split(e.value, ",")) {
				if (Lower(t) != pl) continue;
				out.push_back({ e.key, PresetUse::Override, fc.IniName });
				break;
			}
		}
	}
	auto source = [&](const std::string& id, const std::string& kw) {
		auto a = KeywordSource.find(id);
		if (a == KeywordSource.end()) return std::string();
		auto b = a->second.find(kw);
		return b == a->second.end() ? std::string() : b->second;
	};
	std::vector<std::string> aliasKeys;
	std::unordered_map<std::string, std::string> aliasName;
	for (auto& a : Aliases) {
		for (auto& t : a.targets) if (Lower(t) == pl) { aliasKeys.push_back(Lower(a.name)); aliasName[Lower(a.name)] = a.name; break; }
	}
	std::vector<std::string> ids;
	for (auto& kv : Keywords) ids.push_back(kv.first);
	std::sort(ids.begin(), ids.end());
	for (auto& id : ids) {
		const auto& kws = Keywords.at(id);
		if (kws.count(pl)) out.push_back({ id, PresetUse::Keyword, source(id, pl) });
		for (auto& ak : aliasKeys)
			if (kws.count(ak)) out.push_back({ id, PresetUse::AliasKeyword, aliasName[ak] + " (" + source(id, ak) + ")" });
	}
	return out;
}

std::vector<std::string> LocationConfig::KeywordFilesFor(const std::string& editorId) const {
	std::vector<std::string> out;
	auto it = KeywordSource.find(Lower(editorId));
	if (it == KeywordSource.end()) return out;
	for (auto& kv : it->second)
		if (std::find(out.begin(), out.end(), kv.second) == out.end()) out.push_back(kv.second);
	std::sort(out.begin(), out.end());
	return out;
}

void LocationConfig::Load(const std::string& dataDir, const LogFn& log) {
	std::string root = dataDir + "\\Config\\Cartographer";
	Aliases.clear();
	Keywords.clear();
	KeywordFiles = KeywordLines = 0;
	std::unordered_set<std::string> wanted;

	IniFile settings;
	bool haveSettings = settings.Load(root + "\\Settings.ini");
	if (!haveSettings) log("Settings.ini not found, all features stay off.");

	if (const IniFile::Section* s = settings.FindSection("Keywords")) {
		for (auto& e : s->entries) {
			Alias a;
			a.name = e.key;
			a.targets = Split(e.value, ",");
			wanted.insert(Lower(a.name));
			Aliases.push_back(a);
		}
	}

	for (int i = 0; i < FeatureCount; i++) {
		FeatureConfig& fc = Features[i];
		fc.Presets.clear();
		fc.KeywordOrder.clear();
		fc.Excludes.clear();
		fc.Ini = IniFile();
		const std::string* sw = settings.Get("General", fc.SwitchKey);
		fc.SwitchOn = sw && atof(sw->c_str()) != 0.0;
		fc.IniPath = root + "\\" + fc.IniName;
		fc.HasIni = fc.Ini.Load(fc.IniPath);
		if (fc.SwitchOn && !fc.HasIni) log(std::string(fc.Name) + " is switched on but " + fc.IniName + " is missing.");
		if (!fc.Active()) continue;

		for (auto& file : ListFiles(root + "\\" + fc.PresetFolder, ".ini")) {
			Preset p;
			p.name = file.substr(0, file.size() - 4);
			p.path = root + "\\" + fc.PresetFolder + "\\" + file;
			if (!p.ini.Load(p.path)) continue;
			std::string lower = Lower(p.name);
			fc.KeywordOrder.push_back(lower);
			wanted.insert(lower);
			fc.Presets[lower] = p;
		}
		// Cartographer sorts the file names descending and appends them walking from the end, which gives
		// ascending order: a later name in the alphabet is added later and wins a tie.
		std::sort(fc.KeywordOrder.begin(), fc.KeywordOrder.end());

		if (i == FeatureLighting) {
			if (const IniFile::Section* s = fc.Ini.FindSection("Exclude"))
				for (auto& e : s->entries) fc.Excludes.push_back(Lower(e.value));
		}
	}

	// KEYWORDS: [0] EditorID=Kw1, Kw2   [1] Kw=ID1, ID2   [AnyName] (2+ chars) every value is an ID given the keyword.
	std::string curFile;
	auto add = [&](const std::string& id, const std::string& kw) {
		std::string k = Lower(kw);
		if (id.empty() || !wanted.count(k)) return;
		Keywords[Lower(id)].insert(k);
		KeywordSource[Lower(id)].insert({ k, curFile });
	};
	for (auto& file : ListFiles(dataDir + "\\KEYWORDS", ".ini")) {
		IniFile ini;
		if (!ini.Load(dataDir + "\\KEYWORDS\\" + file)) continue;
		KeywordFiles++;
		curFile = file;
		for (auto& sec : ini.sections) {
			for (auto& e : sec.entries) {
				KeywordLines++;
				if (sec.name == "0") {
					if (e.value.empty()) continue;
					for (auto& kw : Split(e.value, ", ")) add(e.key, kw);
				} else if (sec.name == "1") {
					if (e.value.empty()) continue;
					for (auto& id : Split(e.value, ", ")) add(id, e.key);
				} else if (sec.name.size() >= 2) {
					add(e.value, sec.name);
				}
			}
		}
	}

	std::ostringstream ss;
	ss << "Loaded: " << Aliases.size() << " alias(es), " << KeywordFiles << " KEYWORDS file(s) (" << Keywords.size() << " ids with a preset keyword)";
	for (int i = 0; i < FeatureCount; i++)
		ss << ", " << Features[i].Name << (Features[i].Active() ? ":" : ":off,") << (Features[i].Active() ? std::to_string(Features[i].Presets.size()) + " presets" : "");
	log(ss.str());
}

}
