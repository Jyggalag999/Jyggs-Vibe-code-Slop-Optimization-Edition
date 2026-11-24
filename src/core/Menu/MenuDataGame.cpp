#include "MenuData.h"
#include "MenuBackend.h"
#include "MenuInput.h"
#include "../Occlusion/Occlusion.h"
#include "../Locations/LocationApply.h"
#include "../GameMenuManager.h"
#include <algorithm>

// The game headers define min/max macros.
#undef min
#undef max

// The menu's data from the running game: the only menu file that talks to the engine singletons.

using ConfigNode = SettingManager::Configuration::ConfigNode;

namespace {
	std::string Low(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s;
	}

	MenuNode ToMenu(const ConfigNode& n) {
		MenuNode m;
		strncpy(m.Section, n.Section, sizeof(m.Section) - 1);
		strncpy(m.Key, n.Key, sizeof(m.Key) - 1);
		strncpy(m.Value, n.Value, sizeof(m.Value) - 1);
		m.Description = n.Description;
		m.Type = n.Type;
		return m;
	}

	// The engine node for a menu node (looked up again so every internal field is right).
	ConfigNode ToConfig(const MenuNode& m) {
		ConfigNode n = ConfigNode();
		if (!TheSettingManager->Config.FillNode(&n, m.Section, m.Key)) {
			strncpy(n.Section, m.Section, sizeof(n.Section) - 1);
			strncpy(n.Key, m.Key, sizeof(n.Key) - 1);
			n.Type = m.Type;
		}
		strncpy(n.Value, m.Value, sizeof(n.Value) - 1);
		n.Value[sizeof(n.Value) - 1] = 0;
		return n;
	}

	class GameData : public IMenuData {
	public:
		void MenuSections(std::vector<std::string>& out, const char* parent) override {
			out.clear();
			StringList l;
			TheSettingManager->FillMenuSections(&l, parent);
			out.assign(l.begin(), l.end());
		}

		void MenuSettings(std::vector<MenuNode>& out, const std::string& section) override {
			out.clear();
			SettingManager::Configuration::SettingList list;
			TheSettingManager->FillMenuSettings(&list, section.c_str());
			for (const ConfigNode& n : list) out.push_back(ToMenu(n));
		}

		bool FillNode(MenuNode& out, const std::string& section, const std::string& key) override {
			ConfigNode n;
			if (!TheSettingManager->Config.FillNode(&n, section.c_str(), key.c_str())) return false;
			out = ToMenu(n);
			return true;
		}

		void SearchIndex(std::vector<std::pair<std::string, std::string>>& out) override {
			out.clear();
			try {
				for (const auto& [k1, v1] : TheSettingManager->Config.DefaultConfig.as_table()) {
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
			catch (const std::exception& e) {
				Logger::Log("Menu: could not build the search index: %s", e.what());
			}
		}

		bool DefaultValue(const MenuNode& node, bool preset, std::string& out) override {
			char path[256] = "_";
			if (preset) strcat(path, "Preset.Performance.");
			strncat(path, node.Section, sizeof(path) - strlen(path) - 1);
			StringList keys;
			SettingManager::SplitString(path, ".", &keys);
			tomlValue* section = TheSettingManager->Config.FindSection(&TheSettingManager->Config.DefaultConfig, &keys);
			if (!section) return false;
			try {
				const tomlValue& v = section->at(node.Key);
				char buf[64];
				if (v.is_boolean()) { out = v.as_boolean() ? "1" : "0"; return true; }
				if (v.is_integer()) { snprintf(buf, sizeof(buf), "%d", (int)v.as_integer()); out = buf; return true; }
				if (v.is_floating()) { snprintf(buf, sizeof(buf), "%.6g", (double)v.as_floating()); out = buf; return true; }
				if (v.is_string()) { out = std::string(v.as_string()); return true; }
			}
			catch (const std::exception&) {}
			return false;
		}

		bool IsPresetOverridden(const char* section, const char* key) override { return TheSettingManager->IsPresetOverridden(section, key); }

		void SetSetting(const MenuNode& node) override { ConfigNode n = ToConfig(node); TheSettingManager->SetSetting(&n); }
		void SetPresetSetting(const MenuNode& node) override { ConfigNode n = ToConfig(node); TheSettingManager->SetPresetSetting(&n); }
		void RemovePresetSetting(const char* section, const char* key) override { TheSettingManager->RemovePresetSetting(section, key); }
		void SetBool(const char* section, const char* key, bool value) override { TheSettingManager->SetSetting(section, key, value); }

		void ApplyChanges() override {
			TheSettingManager->LoadSettings();
			MenuBackend::RequestStyleRefresh(); // menu colors may have changed
		}

		bool HasUnsaved() override { return TheSettingManager->hasUnsavedChanges; }

		void Save() override {
			TheSettingManager->SaveSettings();
			InterfaceManager->ShowMessage("Settings saved.");
		}

		void Revert(const std::set<std::string>& toggledShaders) override {
			TheSettingManager->Config.Init();
			for (const std::string& name : toggledShaders) TheShaderManager->SwitchShaderStatus(name.c_str()); // puts the live effect back
			TheSettingManager->LoadSettings();
			TheSettingManager->hasUnsavedChanges = false;
			MenuBackend::RequestStyleRefresh();
		}

		bool ShaderEnabled(const std::string& name) override { return TheSettingManager->GetMenuShaderEnabled(name.c_str()); }
		void ToggleShader(const std::string& name) override { TheShaderManager->SwitchShaderStatus(name.c_str()); }

		void Timings(std::vector<MenuTiming>& rows, float& shadowMaps) override {
			rows.clear();
			StringList names;
			TheSettingManager->FillMenuSections(&names, "Shaders");
			for (const std::string& n : names) {
				EffectRecord* e = TheShaderManager->GetEffectByName(n.c_str());
				if (!e || !e->Enabled) continue;
				float update = std::max(e->constantUpdateTime, 0.0f);
				float render = std::max(e->renderTime, 0.0f);
				// the sun, point and shadow map passes are drawn for the shadow effects
				if ((e == TheShaderManager->Effects.ShadowsExteriors && TheShaderManager->GameState.isExterior) ||
					(e == TheShaderManager->Effects.ShadowsInteriors && !TheShaderManager->GameState.isExterior)) {
					render += TheShaderManager->Effects.PointShadows->renderTime + TheShaderManager->Effects.PointShadows2->renderTime +
						TheShaderManager->Effects.SunShadows->renderTime + TheShadowManager->shadowMapsRenderTime;
				}
				if (!TheSettingManager->SettingsMain.Main.RenderEffects) render = 0;
				rows.push_back({ n, update, render, update + render });
			}
			shadowMaps = TheShadowManager->shadowMapsRenderTime;
		}

		std::string OcclusionStatus() override { return Occlusion::StatusText(); }

		void ForceWeather(const std::string& name) override {
			TESWeather* weather = (TESWeather*)DataHandler->GetFormByName(name.c_str(), TESForm::FormType::kFormType_Weather);
			if (weather && Tes && Tes->sky) Tes->sky->ForceWeather(weather);
		}

		void StyleRgb(const char* key, unsigned char out[3]) override {
			const auto& m = TheSettingManager->SettingsMain.Menu;
			const UInt8* c = m.AccentColor;
			if (!strcmp(key, "BackgroundColor")) c = m.BackgroundColor;
			else if (!strcmp(key, "PanelColor")) c = m.PanelColor;
			else if (!strcmp(key, "FrameColor")) c = m.FrameColor;
			else if (!strcmp(key, "TextColor")) c = m.TextColor;
			else if (!strcmp(key, "CursorColor")) c = m.CursorColor;
			memcpy(out, c, 3);
		}
		bool CursorMatchesAccent() override { return TheSettingManager->SettingsMain.Menu.CursorMatchesAccent; }

		void CloseMenu() override { TheGameMenuManager->Enabled = false; }
		std::string KeyName(int scancode) override { return TheGameMenuManager->GetKeyName(scancode); }
		void BeginKeyCapture() override { MenuInput::BeginKeyCapture(); }
		void CancelKeyCapture() override { MenuInput::CancelKeyCapture(); }
		bool KeyCaptureActive() override { return MenuInput::KeyCaptureActive(); }
		int PollCapturedKey() override { return MenuInput::PollCapturedKey(); }
		std::string LocationsKeyName() override { return TheGameMenuManager->GetKeyName(TheSettingManager->SettingsMain.Menu.KeyLocations); }
		float SettingF(const char* section, const char* key) override { return TheSettingManager->GetSettingF(section, key); }
		int SettingType(const char* section, const char* key) override { return TheSettingManager->GetSettingType(section, key); }

		bool PresetEditor() override { return TheSettingManager->SettingsMain.Locations.PresetEditor; }
		const LocationStatus& LocStatus() override { return TheLocationManager->GetStatus(); }
		const Locations::LocationConfig& LocConfig() override { return TheLocationManager->GetConfig(); }
		void LocReload() override { TheLocationManager->Reload(); }

		const MenuInstalled* FindInstalled(const std::string& editorId) override {
			const LocationManager::InstalledInfo* i = TheLocationManager->FindInstalled(editorId);
			if (!i) return nullptr;
			MenuInstalled& m = Installed[Low(editorId)];
			m.worldspace = i->Worldspace;
			m.plugin = i->Plugin;
			return &m;
		}
		bool CanGoTo() override { return LocationManager::CanGoTo(); }
		bool RequestGoTo(const std::string& editorId) override { return TheLocationManager->RequestGoTo(editorId); }
		std::string GoStatus() override { return TheLocationManager->GoStatus(); }
		const char* FeatureName(int feature) override { return TheLocationManager->FeatureName(feature); }

	private:
		std::map<std::string, MenuInstalled> Installed;
	};
}

IMenuData& Data() {
	static GameData data;
	return data;
}
