// Fake IMenuData for the menu preview: settings come from the real defaults file, edits live in memory,
// Locations reads a small sample Data folder through the real LocationConfig / resolver.
#include "PreviewData.h"
#include "../../src/core/Menu/MenuData.h"
#include "../../src/core/Locations/LocationResolver.h"
#include <toml.hpp>

namespace {
	typedef toml::basic_value<toml::preserve_comments, std::map, std::vector> TomlValue;

	std::string Low(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s;
	}

	std::vector<std::string> Split(const std::string& s, char sep) {
		std::vector<std::string> out;
		size_t pos = 0;
		while (true) {
			size_t c = s.find(sep, pos);
			out.push_back(s.substr(pos, c == std::string::npos ? std::string::npos : c - pos));
			if (c == std::string::npos) break;
			pos = c + 1;
		}
		return out;
	}

	const TomlValue* Find(const TomlValue& root, const std::string& path) {
		const TomlValue* cur = &root;
		for (const std::string& k : Split(path, '.')) {
			if (!cur->is_table() || !cur->contains(k)) return nullptr;
			cur = &cur->at(k);
		}
		return cur;
	}

	std::string ValueText(const TomlValue& v) {
		char buf[64];
		if (v.is_boolean()) return v.as_boolean() ? "1" : "0";
		if (v.is_integer()) { snprintf(buf, sizeof(buf), "%d", (int)v.as_integer()); return buf; }
		if (v.is_floating()) { snprintf(buf, sizeof(buf), "%.6g", (double)v.as_floating()); return buf; }
		if (v.is_string()) return std::string(v.as_string());
		return "";
	}

	unsigned TypeOf(const TomlValue& v) {
		if (v.is_boolean()) return MenuNodeType::Boolean;
		if (v.is_integer()) return MenuNodeType::Integer;
		if (v.is_floating()) return MenuNodeType::Float;
		return MenuNodeType::String;
	}

	void Copy(char* dst, size_t size, const std::string& s) {
		strncpy(dst, s.c_str(), size - 1);
		dst[size - 1] = 0;
	}

	class FakeData : public IMenuData {
	public:
		bool Init(const std::string& defaultsPath, const std::string& dataDir) {
			try {
				Defaults = toml::parse<toml::preserve_comments, std::map>(defaultsPath);
			}
			catch (const std::exception& e) {
				Logger::Log("Could not read %s: %s", defaultsPath.c_str(), e.what());
				return false;
			}
			DataDir = dataDir;
			LoadLocations();
			return true;
		}

		void SetTarget(const std::string& target, bool interior) {
			Target = target;
			Interior = interior;
			BuildStatus();
		}

		// ---- tree
		void MenuSections(std::vector<std::string>& out, const char* parent) override {
			out.clear();
			const TomlValue* t = parent ? Find(Defaults, std::string("_") + parent) : &Defaults;
			if (!t || !t->is_table()) return;
			for (const auto& kv : t->as_table()) {
				std::string n = kv.first;
				if (!n.empty() && n[0] == '_') n = n.substr(1);
				if (!parent && n == "Preset") continue;
				out.push_back(n);
			}
		}

		bool FillNode(MenuNode& out, const std::string& section, const std::string& key) override {
			const TomlValue* sec = Find(Defaults, "_" + section);
			if (!sec || !sec->is_table() || !sec->contains(key)) return false;
			const TomlValue& dv = sec->at(key);
			out = MenuNode();
			Copy(out.Section, sizeof(out.Section), section);
			Copy(out.Key, sizeof(out.Key), key);
			out.Type = TypeOf(dv);
			out.Description = dv.comments().size() ? std::string(dv.comments().at(0)) : "";
			std::string value = ValueText(dv);
			auto p = PresetUser.find(section + "|" + key);
			auto u = User.find(section + "|" + key);
			if (IsPresetOverridden(section.c_str(), key.c_str())) {
				const TomlValue* ps = Find(Defaults, "_Preset.Performance." + section);
				value = ValueText(ps->at(key));
				if (p != PresetUser.end()) value = p->second;
			}
			else if (u != User.end()) value = u->second;
			Copy(out.Value, sizeof(out.Value), value);
			return true;
		}

		void MenuSettings(std::vector<MenuNode>& out, const std::string& section) override {
			out.clear();
			const TomlValue* sec = Find(Defaults, "_" + section);
			if (!sec || !sec->is_table()) return;
			for (const auto& kv : sec->as_table()) {
				MenuNode n;
				if (FillNode(n, section, kv.first)) out.push_back(n);
			}
		}

		void SearchIndex(std::vector<std::pair<std::string, std::string>>& out) override {
			out.clear();
			for (const auto& [k1, v1] : Defaults.as_table()) {
				if (!v1.is_table() || k1 == "_Preset") continue;
				const std::string cat = (k1.size() && k1[0] == '_') ? k1.substr(1) : k1;
				for (const auto& [k2, v2] : v1.as_table()) {
					if (!v2.is_table()) continue;
					std::string blob = Low(k2) + "|";
					for (const auto& [k3, v3] : v2.as_table()) {
						blob += Low(k3) + "|";
						if (v3.is_table())
							for (const auto& [k4, v4] : v3.as_table()) blob += Low(k4) + "|";
					}
					out.push_back({ cat + "." + k2, blob });
				}
			}
		}

		bool DefaultValue(const MenuNode& node, bool preset, std::string& out) override {
			const TomlValue* sec = Find(Defaults, std::string("_") + (preset ? "Preset.Performance." : "") + node.Section);
			if (!sec || !sec->is_table() || !sec->contains(node.Key)) return false;
			out = ValueText(sec->at(node.Key));
			return true;
		}

		bool IsPresetOverridden(const char* section, const char* key) override {
			if (!PresetOn) return false;
			const TomlValue* ps = Find(Defaults, std::string("_Preset.Performance.") + section);
			return ps && ps->is_table() && ps->contains(key);
		}

		// ---- writes
		void SetSetting(const MenuNode& n) override { User[std::string(n.Section) + "|" + n.Key] = n.Value; Unsaved = true; }
		void SetPresetSetting(const MenuNode& n) override { PresetUser[std::string(n.Section) + "|" + n.Key] = n.Value; Unsaved = true; }
		void RemovePresetSetting(const char* s, const char* k) override { PresetUser.erase(std::string(s) + "|" + k); Unsaved = true; }
		void SetBool(const char* s, const char* k, bool v) override { User[std::string(s) + "|" + k] = v ? "1" : "0"; Unsaved = true; }
		void ApplyChanges() override {}
		bool HasUnsaved() override { return Unsaved; }
		void Save() override { Unsaved = false; }
		void Revert(const std::set<std::string>&) override { User.clear(); PresetUser.clear(); Unsaved = false; }

		// ---- shaders
		bool ShaderEnabled(const std::string& name) override {
			MenuNode n;
			if (!FillNode(n, "Shaders." + name + ".Status", "Enabled")) return false;
			return atoi(n.Value) != 0;
		}
		void ToggleShader(const std::string& name) override {
			User["Shaders." + name + ".Status|Enabled"] = ShaderEnabled(name) ? "0" : "1";
		}
		void Timings(std::vector<MenuTiming>& rows, float& shadowMaps) override {
			rows.clear();
			std::vector<std::string> names;
			MenuSections(names, "Shaders");
			unsigned seed = 7;
			for (const std::string& n : names) {
				if (!ShaderEnabled(n)) continue;
				seed = seed * 1103515245u + 12345u;
				const float update = 0.002f + (seed >> 16 & 0xFF) / 255.0f * 0.05f;
				seed = seed * 1103515245u + 12345u;
				const float render = 0.02f + (seed >> 16 & 0xFF) / 255.0f * 1.2f;
				rows.push_back({ n, update, render, update + render });
			}
			shadowMaps = 0.41f;
		}
		std::string OcclusionStatus() override { return "Occlusion culling: preview, not running."; }
		void ForceWeather(const std::string& name) override { Logger::Log("ForceWeather(%s) ignored in the preview", name.c_str()); }

		// ---- style
		void StyleRgb(const char* key, unsigned char out[3]) override {
			MenuNode n;
			out[0] = out[1] = out[2] = 128;
			if (!FillNode(n, "Main.Menu.Style", key)) return;
			int r, g, b;
			if (sscanf(n.Value, "%d,%d,%d", &r, &g, &b) == 3) { out[0] = (unsigned char)r; out[1] = (unsigned char)g; out[2] = (unsigned char)b; }
		}
		bool CursorMatchesAccent() override {
			MenuNode n;
			return !FillNode(n, "Main.Menu.Style", "CursorMatchesAccent") || atoi(n.Value) != 0;
		}

		// ---- menu
		void CloseMenu() override { CloseRequested = true; }
		std::string LocationsKeyName() override { return "End"; }
		std::string KeyName(int dik) override {
			static const char* rows[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
			static const int start[3] = { 16, 30, 44 };
			for (int r = 0; r < 3; r++) if (dik >= start[r] && dik < start[r] + (int)strlen(rows[r])) return std::string(1, rows[r][dik - start[r]]);
			if (dik >= 2 && dik <= 10) return std::to_string(dik - 1);
			if (dik == 11) return "0";
			if (dik >= 59 && dik <= 68) return "F" + std::to_string(dik - 58);
			switch (dik) {
			case 1: return "Esc"; case 28: return "Enter"; case 57: return "Spacebar"; case 87: return "F11"; case 88: return "F12";
			case 74: return "Numpad -"; case 78: return "Numpad +"; case 199: return "Home"; case 207: return "End";
			case 210: return "Insert"; case 211: return "Delete"; case 201: return "PgUp"; case 209: return "PgDown";
			}
			return std::to_string(dik);
		}
		// Capture in the preview: the first key pressed in Windows (mapped to a scancode) after Begin.
		void BeginKeyCapture() override { Capturing = true; CapFresh = true; Captured = -1; }
		void CancelKeyCapture() override { Capturing = false; Captured = -1; }
		bool KeyCaptureActive() override { return Capturing; }
		int PollCapturedKey() override {
			if (!Capturing) return -1;
			bool now[256] = {};
			for (int vk = 7; vk < 255; vk++) now[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
			if (CapFresh) { memcpy(CapPrev, now, sizeof(now)); CapFresh = false; return -1; }
			for (int vk = 7; vk < 255; vk++) {
				if (now[vk] && !CapPrev[vk]) {
					UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
					switch (vk) { case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT: case VK_RCONTROL: case VK_RMENU: case VK_DIVIDE: sc |= 0x80; }
					memcpy(CapPrev, now, sizeof(now));
					Capturing = false;
					return (int)sc;
				}
			}
			memcpy(CapPrev, now, sizeof(now));
			return -1;
		}
		float SettingF(const char* s, const char* k) override {
			MenuNode n;
			return FillNode(n, s, k) ? (float)atof(n.Value) : 0.0f;
		}
		int SettingType(const char* s, const char* k) override {
			MenuNode n;
			return FillNode(n, s, k) ? (int)n.Type : -1;
		}

		// ---- Locations
		bool PresetEditor() override { return true; }
		const LocationStatus& LocStatus() override { return Status; }
		const Locations::LocationConfig& LocConfig() override { return Config; }
		void LocReload() override { LoadLocations(); }
		const MenuInstalled* FindInstalled(const std::string& id) override {
			static const char* worlds[] = { "goodsprings", "nvstrip", "wastelandnv" };
			static const char* cells[] = { "vault21", "gssaloon", "gssaloonint", "nvstripcasinocell", "gssaloon_interior" };
			const std::string l = Low(id);
			bool found = false, world = false;
			for (const char* w : worlds) if (l == w) { found = true; world = true; }
			for (const char* c : cells) if (l == c) found = true;
			if (l == "gssaloon" "interior") found = true;
			if (!found) return nullptr;
			MenuInstalled& m = Installed[l];
			m.worldspace = world;
			m.plugin = world ? "FalloutNV.esm" : "Mojave Nights.esp";
			return &m;
		}
		bool CanGoTo() override { return true; }
		bool RequestGoTo(const std::string& id) override { Go = "Would travel to " + id + " (preview)."; return true; }
		std::string GoStatus() override { return Go; }
		const char* FeatureName(int f) override { return Config.Features[f].Name; }

		bool				CloseRequested = false;
		void				SetUser(const std::string& s, const std::string& k, const std::string& v) { User[s + "|" + k] = v; }
		bool				PresetOn = true;
		bool				Capturing = false, CapFresh = false;
		int					Captured = -1;
		bool				CapPrev[256] = {};

	private:
		void LoadLocations() {
			Config = Locations::LocationConfig();
			Config.Load(DataDir, [](const std::string& m) { Logger::Log("[Locations] %s", m.c_str()); });
			BuildStatus();
		}

		void BuildStatus() {
			Status = LocationStatus();
			Status.Enabled = true;
			Status.Target = Target;
			Status.Interior = Interior;
			for (int f = 0; f < Locations::FeatureCount; f++) {
				if (!Config.Features[f].Active()) {
					Status.Features[f].valid = false;
					Status.Features[f].reason = std::string(Config.Features[f].SwitchOn ? "feature file missing" : "switched off in Settings.ini");
					continue;
				}
				Status.Features[f] = Locations::Resolve(Config, (Locations::Feature)f, Target, Interior);
				Status.Applied[f] = Status.Features[f].valid;
				for (const std::string& k : Status.Features[f].keywords) {
					if (std::find(Status.Keywords.begin(), Status.Keywords.end(), k) == Status.Keywords.end()) Status.Keywords.push_back(k);
				}
			}
		}

		TomlValue									Defaults;
		std::map<std::string, std::string>			User, PresetUser;
		bool										Unsaved = false;
		std::string									DataDir, Target = "Goodsprings", Go;
		bool										Interior = false;
		Locations::LocationConfig					Config;
		LocationStatus								Status;
		std::map<std::string, MenuInstalled>		Installed;
	};

	FakeData gData;
}

IMenuData& Data() { return gData; }

namespace Preview {
	bool InitData(const std::string& defaultsPath, const std::string& dataDir, const std::string& target, bool interior) {
		if (!gData.Init(defaultsPath, dataDir)) return false;
		gData.SetTarget(target, interior);
		return true;
	}
	bool CloseRequested() { return gData.CloseRequested; }
	void SetUserValue(const std::string& s, const std::string& k, const std::string& v) { gData.SetUser(s, k, v); }
}
