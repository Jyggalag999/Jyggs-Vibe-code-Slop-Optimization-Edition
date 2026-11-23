#include "MenuUI.h"
#include "MenuBackend.h"
#include "MenuTheme.h"
#include "PresetsPage.h"
#include "../../../lib/imgui/imgui.h"
#include <set>

// The game headers define min/max macros.
#undef min
#undef max

using ConfigNode = SettingManager::Configuration::ConfigNode;
using NodeType = SettingManager::Configuration::NodeType;

namespace {
	// ------------------------------------------------------------------ data

	struct Item {
		std::string		path;		// "Shaders.Bloom"
		std::string		name;		// "Bloom"
		bool			isShader;
		bool			isWeather;
	};

	struct Category {
		std::string			name;
		std::vector<Item>	items;
	};

	struct Cell {
		bool		present = false;
		ConfigNode	node;
		bool		hasDefault = false;
		bool		preset = false;	// value comes from the Performance preset: shown, not editable
		std::string	def;
		char		text[80] = {};	// edit buffer for string settings
	};

	// One table = sections that share the same keys, shown as one column per section.
	struct Table {
		std::string							title;
		std::vector<std::string>			cols;
		std::vector<std::string>			keys;
		std::vector<std::vector<Cell>>		rows;	// [key][col]
	};

	struct Model {
		std::string			path;
		std::string			description;
		bool				readOnly = false;
		std::vector<Table>	tables;
	};

	struct SearchEntry {
		std::string path;
		std::string blob;	// lower case names of everything below the item
	};

	constexpr const char* kPerformance = "@Performance";
	constexpr const char* kLocations = "@Locations";
	constexpr const char* kPresets = "@Presets";
	constexpr const char* kStyle = "@Style";

	std::vector<Category>		gCategories;
	std::vector<SearchEntry>	gSearchIndex;
	bool						gBuilt = false;
	std::string					gSelected;
	Model						gModel;
	bool						gModelDirty = true;
	bool						gNeedLoad = false;
	char						gSearch[64] = {};
	std::string					gEditId;
	char						gEditBuf[64] = {};
	bool						gEditFocus = false;
	std::set<std::string>		gToggledShaders;			// toggled since the last save/revert
	std::map<std::string, bool>	gShaderEnabled;				// cached for the sidebar
	int							gShaderCacheAge = 1000;

	struct ClickMemory { ImGuiID id = 0; double time = -10; float startValue = 0; } gClick;

	// ------------------------------------------------------------------ helpers

	std::string Lower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		return s;
	}

	bool IsShaderPath(const std::string& p) { return p.compare(0, 8, "Shaders.") == 0; }
	bool IsWeatherPath(const std::string& p) { return p.compare(0, 9, "Weathers.") == 0; }

	// Interim slider range rule (ranges are not decided yet): everything about a slider's range is in this
	// one function so it can be swapped for the final rule. Typing a value (Ctrl+click or double click)
	// always allows any number.
	void GetSliderRange(const ConfigNode& node, const std::string& def, bool isInt, float& lo, float& hi) {
		const float d = (float)atof(def.empty() ? node.Value : def.c_str());
		const float m = std::max(isInt ? 10.0f : 1.0f, 3.0f * fabsf(d));
		lo = (d < 0.0f) ? -m : 0.0f;
		hi = m;
		const float cur = (float)atof(node.Value); // never pin the slider if the value is already outside
		lo = std::min(lo, cur);
		hi = std::max(hi, cur);
	}

	bool GetDefaultValue(const ConfigNode& node, std::string& out) {
		char path[256] = "_";
		strncat(path, node.Section, sizeof(path) - 2);
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

	bool IsChanged(const Cell& c) {
		if (!c.present || !c.hasDefault) return false;
		if (c.node.Type == NodeType::String) return c.def != c.node.Value;
		const double a = atof(c.node.Value), b = atof(c.def.c_str());
		return fabs(a - b) > 1e-5 * std::max(1.0, fabs(b));
	}

	// Writes a new value into the settings. LoadSettings runs once per frame afterwards (gNeedLoad).
	void Commit(Cell& c, const std::string& value) {
		ConfigNode n = c.node;
		strncpy(n.Value, value.c_str(), sizeof(n.Value) - 1);
		n.Value[sizeof(n.Value) - 1] = 0;
		TheSettingManager->SetSetting(&n);
		if (!strcmp(n.Key, "Quality") || !strcmp(n.Key, "PerformancePreset")) gModelDirty = true; // some sections appear or hide with the quality level
		strcpy(c.node.Value, n.Value);
		strncpy(c.text, n.Value, sizeof(c.text) - 1);
		gNeedLoad = true;
	}

	std::string FormatNumber(double v, bool isInt) {
		char buf[48];
		if (isInt) snprintf(buf, sizeof(buf), "%d", (int)floor(v + 0.5));
		else snprintf(buf, sizeof(buf), "%.6g", v);
		return buf;
	}

	void Tip(const std::string& text) {
		if (text.empty() || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayShort)) return;
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
		ImGui::TextUnformatted(text.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	bool ToggleSwitch(const char* id, bool* value) {
		const float h = ImGui::GetFrameHeight() * 0.80f;
		const float w = h * 1.8f;
		const ImVec2 size(w, ImGui::GetFrameHeight());
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const bool pressed = ImGui::InvisibleButton(id, size);
		if (pressed) *value = !*value;

		const ImVec2 p0(pos.x, pos.y + (size.y - h) * 0.5f);
		const ImVec2 p1(p0.x + w, p0.y + h);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const bool hov = ImGui::IsItemHovered();
		ImVec4 off = hov ? ImVec4(0.26f, 0.29f, 0.35f, 1.0f) : ImVec4(0.21f, 0.23f, 0.28f, 1.0f);
		ImVec4 on = MenuTheme::Accent();
		if (hov) { on.x = std::min(on.x + 0.06f, 1.0f); on.y = std::min(on.y + 0.06f, 1.0f); on.z = std::min(on.z + 0.06f, 1.0f); }
		dl->AddRectFilled(p0, p1, ImGui::GetColorU32(*value ? on : off), h * 0.5f);
		const float r = h * 0.5f - 3.0f;
		const float cx = *value ? p1.x - h * 0.5f : p0.x + h * 0.5f;
		dl->AddCircleFilled(ImVec2(cx, p0.y + h * 0.5f), r, IM_COL32(236, 238, 242, 255));
		return pressed;
	}

	bool IsShaderEnabledCached(const std::string& name) {
		auto it = gShaderEnabled.find(name);
		return it != gShaderEnabled.end() && it->second;
	}

	void RefreshShaderCache() {
		gShaderEnabled.clear();
		for (const Category& c : gCategories)
			if (c.name == "Shaders")
				for (const Item& i : c.items) gShaderEnabled[i.name] = TheSettingManager->GetMenuShaderEnabled(i.name.c_str());
		gShaderCacheAge = 0;
	}

	void SwitchShader(const std::string& name) {
		TheShaderManager->SwitchShaderStatus(name.c_str());
		if (!gToggledShaders.erase(name)) gToggledShaders.insert(name);
		gNeedLoad = true;
		RefreshShaderCache();
	}

	// ------------------------------------------------------------------ model building

	void FillDefaults(Table& t) {
		for (auto& row : t.rows)
			for (Cell& c : row)
				if (c.present) c.hasDefault = GetDefaultValue(c.node, c.def);
	}

	// sections = (display name, full section path). Sections with identical keys share one table.
	void AddTables(Model& m, const std::vector<std::pair<std::string, std::string>>& sections) {
		struct Group { std::string sig; std::vector<std::string> names; std::vector<SettingManager::Configuration::SettingList> lists; };
		std::vector<Group> groups;

		for (const auto& [name, full] : sections) {
			SettingManager::Configuration::SettingList list;
			TheSettingManager->FillMenuSettings(&list, full.c_str());
			if (name == "Status") // the switch in the header replaces Status.Enabled
				list.erase(std::remove_if(list.begin(), list.end(), [](const ConfigNode& n) { return !strcmp(n.Key, "Enabled"); }), list.end());
			if (list.empty()) continue;

			std::string sig;
			for (const ConfigNode& n : list) { sig += n.Key; sig += '|'; }
			auto g = std::find_if(groups.begin(), groups.end(), [&](const Group& x) { return x.sig == sig; });
			if (g == groups.end()) { groups.push_back({ sig, {}, {} }); g = groups.end() - 1; }
			g->names.push_back(name);
			g->lists.push_back(std::move(list));
		}

		for (Group& g : groups) {
			Table t;
			if (g.names.size() == 1) t.title = g.names[0];
			t.cols = g.names;
			for (const ConfigNode& n : g.lists[0]) t.keys.push_back(n.Key);
			t.rows.resize(t.keys.size());
			for (size_t k = 0; k < t.keys.size(); k++) {
				t.rows[k].resize(g.names.size());
				for (size_t c = 0; c < g.names.size(); c++) {
					Cell& cell = t.rows[k][c];
					cell.present = true;
					cell.node = g.lists[c][k];
					cell.preset = TheSettingManager->IsPresetOverridden(cell.node.Section, cell.node.Key);
					strncpy(cell.text, cell.node.Value, sizeof(cell.text) - 1);
				}
			}
			FillDefaults(t);
			m.tables.push_back(std::move(t));
		}
	}

	void BuildItemModel(const std::string& path) {
		gModel = Model();
		gModel.path = path;
		gModel.readOnly = IsWeatherPath(path);

		StringList sections;
		TheSettingManager->FillMenuSections(&sections, path.c_str());
		std::vector<std::pair<std::string, std::string>> secs;
		for (const std::string& s : sections) secs.push_back({ s, path + "." + s });
		AddTables(gModel, secs);

		if (IsShaderPath(path)) {
			ConfigNode status;
			if (TheSettingManager->Config.FillNode(&status, (path + ".Status").c_str(), "Enabled")) gModel.description = status.Description;
		}
		gModelDirty = false;
	}

	void BuildPerformanceModel() {
		gModel = Model();
		gModel.path = kPerformance;
		AddTables(gModel, { { "Switches", "Main.Main.Performance" }, { "Profiler and benchmark", "Main.Main.Profiler" } });
		gModelDirty = false;
	}

	void BuildLocationsModel() {
		gModel = Model();
		gModel.path = kLocations;
		AddTables(gModel, { { "Settings", "Main.Main.Locations" } });
		gModelDirty = false;
	}

	void BuildSidebar() {
		gCategories.clear();
		gSearchIndex.clear();

		StringList top;
		TheSettingManager->FillMenuSections(&top, nullptr);
		for (const std::string& cat : top) {
			Category c;
			c.name = cat;
			StringList items;
			TheSettingManager->FillMenuSections(&items, cat.c_str());
			for (const std::string& i : items)
				c.items.push_back({ cat + "." + i, i, cat == "Shaders", cat == "Weathers" });
			gCategories.push_back(std::move(c));
		}

		// Search index over the defaults (names only, no value lookups).
		try {
			for (const auto& [k1, v1] : TheSettingManager->Config.DefaultConfig.as_table()) {
				if (!v1.is_table() || k1 == "_Preset") continue;
				const std::string cat = (k1.size() && k1[0] == '_') ? k1.substr(1) : k1;
				for (const auto& [k2, v2] : v1.as_table()) {
					if (!v2.is_table()) continue;
					std::string blob = Lower(k2) + "|";
					for (const auto& [k3, v3] : v2.as_table()) {
						blob += Lower(k3) + "|";
						if (v3.is_table())
							for (const auto& [k4, v4] : v3.as_table()) blob += Lower(k4) + "|";
					}
					gSearchIndex.push_back({ cat + "." + k2, blob });
				}
			}
		}
		catch (const std::exception& e) {
			Logger::Log("Menu: could not build the search index: %s", e.what());
		}

		RefreshShaderCache();
		gBuilt = true;
	}

	void Select(const std::string& path) {
		if (gSelected == path) return;
		gSelected = path;
		gModelDirty = true;
		gEditId.clear();
	}

	// ------------------------------------------------------------------ widgets

	// Number editing by typing: Ctrl+click on a slider (ImGui) or double click (here).
	bool DrawNumber(Cell& c, const std::string& id, float width) {
		const bool isInt = c.node.Type == NodeType::Integer;
		bool changed = false;
		ImGui::SetNextItemWidth(width);

		if (gEditId == id) {
			if (gEditFocus) { ImGui::SetKeyboardFocusHere(); gEditFocus = false; }
			const bool enter = ImGui::InputText("##edit", gEditBuf, sizeof(gEditBuf),
				ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
			const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
			if (enter || (ImGui::IsItemDeactivated() && !escape)) {
				char* end = nullptr;
				const double v = strtod(gEditBuf, &end);
				if (end != gEditBuf) { Commit(c, FormatNumber(v, isInt)); changed = true; }
				gEditId.clear();
			}
			else if (escape || ImGui::IsItemDeactivated()) {
				gEditId.clear();
			}
			return changed;
		}

		float lo, hi;
		GetSliderRange(c.node, c.def, isInt, lo, hi);
		const double cur = atof(c.node.Value);

		bool edited;
		float f = (float)cur;
		int iv = (int)floor(cur + 0.5);
		if (isInt) edited = ImGui::SliderInt("##v", &iv, (int)lo, (int)hi);
		else edited = ImGui::SliderFloat("##v", &f, lo, hi, "%.3f");

		const ImGuiID itemId = ImGui::GetItemID();
		const double now = ImGui::GetTime();
		if (ImGui::IsItemActivated()) {
			// keep the value from before the first click of a double click, so the click does not move it
			if (!(gClick.id == itemId && now - gClick.time < ImGui::GetIO().MouseDoubleClickTime)) gClick.startValue = (float)cur;
			gClick.id = itemId;
			gClick.time = now;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && gClick.id == itemId) {
			Commit(c, FormatNumber(gClick.startValue, isInt));
			gEditId = id;
			snprintf(gEditBuf, sizeof(gEditBuf), "%s", c.node.Value);
			gEditFocus = true;
			return true;
		}
		if (edited) { Commit(c, FormatNumber(isInt ? (double)iv : (double)f, isInt)); changed = true; }
		return changed;
	}

	void DrawCell(Cell& c, const std::string& id, bool readOnly) {
		ImGui::PushID(id.c_str());
		const ImGuiStyle& style = ImGui::GetStyle();
		const float resetW = ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f;
		const float width = std::max(40.0f, ImGui::GetContentRegionAvail().x - resetW - style.ItemSpacing.x);

		readOnly = readOnly || c.preset;
		ImGui::BeginDisabled(readOnly);
		switch (c.node.Type) {
		case NodeType::Boolean: {
			bool b = atoi(c.node.Value) != 0;
			if (ToggleSwitch("##v", &b)) Commit(c, b ? "1" : "0");
			break;
		}
		case NodeType::Integer:
		case NodeType::Float:
			DrawNumber(c, id, width);
			break;
		default:
			ImGui::SetNextItemWidth(width);
			ImGui::InputText("##v", c.text, sizeof(c.text));
			if (ImGui::IsItemDeactivatedAfterEdit()) Commit(c, c.text);
			break;
		}
		ImGui::EndDisabled();
		Tip(c.preset ? std::string("Set by the Performance preset. Turn it off to edit.") : c.node.Description);

		ImGui::SameLine();
		if (!readOnly && IsChanged(c)) {
			if (ImGui::Button("Reset")) {
				Commit(c, c.def);
				gEditId.clear();
			}
			Tip("Back to the default value: " + c.def);
		}
		else {
			ImGui::Dummy(ImVec2(resetW, 0));
		}
		ImGui::PopID();
	}

	void DrawTable(Table& t, int tableIndex, bool readOnly) {
		if (!t.title.empty()) ImGui::SeparatorText(t.title.c_str());

		const int ncol = (int)t.cols.size();
		const std::string tid = "##table" + std::to_string(tableIndex);
		if (!ImGui::BeginTable(tid.c_str(), 1 + ncol, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) return;

		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, ncol > 1 ? 0.8f : 0.45f);
		for (const std::string& c : t.cols) ImGui::TableSetupColumn(c.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		if (ncol > 1) ImGui::TableHeadersRow();

		for (size_t k = 0; k < t.keys.size(); k++) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			bool changed = false;
			std::string desc;
			for (const Cell& c : t.rows[k]) {
				changed = changed || IsChanged(c);
				if (desc.empty() && c.present) desc = c.node.Description;
			}

			ImGui::AlignTextToFramePadding();
			const float dot = ImGui::GetFontSize() * 0.55f;
			if (changed) {
				const ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + dot * 0.5f, p.y + ImGui::GetFrameHeight() * 0.5f), dot * 0.32f, ImGui::GetColorU32(MenuTheme::Accent()));
			}
			ImGui::Indent(dot + 4.0f);
			ImGui::TextUnformatted(t.keys[k].c_str());
			Tip(desc);
			ImGui::Unindent(dot + 4.0f);

			for (int c = 0; c < ncol; c++) {
				ImGui::TableSetColumnIndex(1 + c);
				Cell& cell = t.rows[k][c];
				if (cell.present) DrawCell(cell, std::to_string(tableIndex) + "_" + std::to_string(k) + "_" + std::to_string(c), readOnly);
			}
		}
		ImGui::EndTable();
	}

	void DrawPageTitle(const char* title) {
		ImGui::PushFont(MenuBackend::FontSemiBold());
		ImGui::TextUnformatted(title);
		ImGui::PopFont();
	}

	void DrawItem() {
		if (gModelDirty || gModel.path != gSelected) BuildItemModel(gSelected);

		const bool shader = IsShaderPath(gSelected);
		const std::string name = gSelected.substr(gSelected.find('.') + 1);

		DrawPageTitle(name.c_str());
		if (shader) {
			ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight() * 1.45f);
			bool on = IsShaderEnabledCached(name);
			if (ToggleSwitch("##enable", &on)) SwitchShader(name);
			Tip("Turn this effect on or off");
		}
		if (!gModel.description.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(gModel.description.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
		if (gModel.readOnly) {
			if (ImGui::Button("Force this weather")) {
				TESWeather* weather = (TESWeather*)DataHandler->GetFormByName(name.c_str(), TESForm::FormType::kFormType_Weather);
				if (weather && Tes && Tes->sky) Tes->sky->ForceWeather(weather);
			}
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
			ImGui::TextUnformatted("Weather values are shown for reference and are read-only here.");
			ImGui::PopStyleColor();
		}

		ImGui::Spacing();
		ImGui::BeginChild("##scroll", ImVec2(0, 0), ImGuiChildFlags_None);
		for (size_t i = 0; i < gModel.tables.size(); i++) DrawTable(gModel.tables[i], (int)i, gModel.readOnly);
		if (gModel.tables.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
			ImGui::TextUnformatted("No settings here.");
			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
	}

	void DrawLocations() {
		if (gModelDirty || gModel.path != gSelected) BuildLocationsModel();

		DrawPageTitle("Locations");
		ImGui::Spacing();
		ImGui::BeginChild("##scroll", ImVec2(0, 0), ImGuiChildFlags_None);

		const LocationStatus& st = TheLocationManager->GetStatus();
		if (!st.Warning.empty()) {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(MenuTheme::Warning(), "%s", st.Warning.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
		}

		for (size_t i = 0; i < gModel.tables.size(); i++) DrawTable(gModel.tables[i], (int)i, false);

		ImGui::SeparatorText("Current location");
		if (ImGui::Button("Reload presets", ImVec2(150.0f * MenuBackend::Scale(), 0))) TheLocationManager->Reload();
		Tip("Re-reads the Cartographer files and applies them to where you are now.");
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
		ImGui::TextUnformatted("Re-reads Data\\Config\\Cartographer and Data\\KEYWORDS and re-applies (blends if BlendSeconds > 0).");
		ImGui::PopStyleColor();

		if (st.Target.empty()) {
			ImGui::TextUnformatted(st.Enabled ? "No location picked yet." : "Locations is off.");
			ImGui::EndChild();
			return;
		}
		ImGui::Text("%s", st.Interior ? "Interior cell:" : "Worldspace:");
		ImGui::SameLine();
		ImGui::TextColored(MenuTheme::Accent(), "%s", st.Target.c_str());
		std::string kw;
		for (const std::string& k : st.Keywords) kw += (kw.empty() ? "" : ", ") + k;
		ImGui::PushTextWrapPos(0.0f);
		ImGui::Text("Keywords: %s", kw.empty() ? "none" : kw.c_str());
		ImGui::PopTextWrapPos();

		static bool showAll = false;
		ImGui::Checkbox("Show values from the default preset too", &showAll);

		for (int f = 0; f < Locations::FeatureCount; f++) {
			const Locations::Resolution& r = st.Features[f];
			const char* name = TheLocationManager->FeatureName(f);
			ImGui::SeparatorText(name);
			if (!r.valid) {
				ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
				const std::string& why = (f == Locations::FeatureLighting && !st.LightingNote.empty()) ? st.LightingNote : r.reason;
				ImGui::Text("Not used: %s", why.empty() ? "nothing to do" : why.c_str());
				ImGui::PopStyleColor();
				continue;
			}
			std::string stack;
			for (const std::string& p : r.stack) stack += (stack.empty() ? "" : " > ") + p;
			ImGui::PushTextWrapPos(0.0f);
			ImGui::Text("Presets (later wins): %s", stack.c_str());
			ImGui::PopTextWrapPos();
			if (!st.Applied[f]) {
				ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
				ImGui::TextUnformatted("Same presets as before, nothing was written.");
				ImGui::PopStyleColor();
			}
			const std::string id = std::string("##loc") + name;
			if (ImGui::BeginTable(id.c_str(), 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
				ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 2.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("From preset", ImGuiTableColumnFlags_WidthStretch, 1.5f);
				ImGui::TableHeadersRow();
				for (const Locations::ResolvedValue& v : r.values) {
					if (!showAll && v.source == r.defaultPreset) continue;
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::Text("%s : %s", v.section.c_str(), v.key.c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted(v.value.c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted(v.source.c_str());
				}
				ImGui::EndTable();
			}
		}

		ImGui::EndChild();
	}

	// ---- Style page: the menu colors. Each change writes all style keys so the file is complete and the
	// old-default migration in SettingManager never mistakes a half-written section for an untouched one.
	void Revert();
	struct StyleColor { const char* key; const char* label; const char* tip; UInt8* (*get)(); const char* def; };
	UInt8* StyleAccent() { return TheSettingManager->SettingsMain.Menu.AccentColor; }
	UInt8* StyleBackground() { return TheSettingManager->SettingsMain.Menu.BackgroundColor; }
	UInt8* StylePanel() { return TheSettingManager->SettingsMain.Menu.PanelColor; }
	UInt8* StyleFrame() { return TheSettingManager->SettingsMain.Menu.FrameColor; }
	UInt8* StyleText() { return TheSettingManager->SettingsMain.Menu.TextColor; }
	UInt8* StyleCursor() { return TheSettingManager->SettingsMain.Menu.CursorColor; }
	const StyleColor kStyleColors[] = {
		{ "AccentColor", "Accent", "Checkmarks, sliders, selection and highlights.", StyleAccent, "40,140,255" },
		{ "BackgroundColor", "Background", "The menu window.", StyleBackground, "8,9,12" },
		{ "PanelColor", "Panels", "Sidebar, content area and popups.", StylePanel, "14,17,24" },
		{ "FrameColor", "Fields and buttons", "Input fields, buttons and table rows. Hover and pressed shades are derived from it.", StyleFrame, "22,28,40" },
		{ "TextColor", "Text", "Text color. The muted and disabled shades are derived from it.", StyleText, "226,232,242" },
		{ "CursorColor", "Cursor", "Mouse cursor color, used only when 'Cursor matches accent' is off.", StyleCursor, "255,255,255" },
	};

	void CommitStyleString(const char* key, const std::string& value) {
		ConfigNode n;
		if (!TheSettingManager->Config.FillNode(&n, "Main.Menu.Style", key)) return;
		strncpy(n.Value, value.c_str(), sizeof(n.Value) - 1);
		n.Value[sizeof(n.Value) - 1] = 0;
		TheSettingManager->SetSetting(&n);
	}

	std::string RgbString(const UInt8* c) {
		char b[24];
		snprintf(b, sizeof(b), "%d,%d,%d", c[0], c[1], c[2]);
		return b;
	}

	// Writes the current values (with one replaced) and asks for a reload + theme refresh.
	void CommitStyle(const char* changedKey, const std::string& changedValue, bool cursorMatches) {
		for (const StyleColor& s : kStyleColors) CommitStyleString(s.key, !strcmp(s.key, changedKey) ? changedValue : RgbString(s.get()));
		TheSettingManager->SetSetting("Main.Menu.Style", "CursorMatchesAccent", cursorMatches);
		gNeedLoad = true;
	}

	void DrawStyle() {
		DrawPageTitle("Style");
		ImGui::Spacing();
		ImGui::BeginChild("##scroll", ImVec2(0, 0), ImGuiChildFlags_None);
		ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
		ImGui::TextWrapped("Changes show right away. Save writes them to your settings file; Revert goes back to the last saved colors.");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		const float labelW = 190.0f * MenuBackend::Scale();
		bool matches = TheSettingManager->SettingsMain.Menu.CursorMatchesAccent;
		for (const StyleColor& s : kStyleColors) {
			const bool cursorRow = !strcmp(s.key, "CursorColor");
			if (cursorRow) {
				if (ImGui::Checkbox("Cursor matches accent", &matches)) CommitStyle("", "", matches);
				Tip("Draw the mouse cursor in the accent color. Turn off to pick its own color below.");
			}
			UInt8* c = s.get();
			float col[3] = { c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f };
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(s.label);
			Tip(s.tip);
			ImGui::SameLine(labelW);
			ImGui::SetNextItemWidth(-1);
			if (cursorRow && matches) ImGui::BeginDisabled();
			if (ImGui::ColorEdit3((std::string("##") + s.key).c_str(), col, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Uint8)) {
				UInt8 v[3] = { (UInt8)(col[0] * 255.0f + 0.5f), (UInt8)(col[1] * 255.0f + 0.5f), (UInt8)(col[2] * 255.0f + 0.5f) };
				CommitStyle(s.key, RgbString(v), matches);
			}
			if (cursorRow && matches) ImGui::EndDisabled();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		const float bw = 190.0f * MenuBackend::Scale();
		if (ImGui::Button("Reset to black & blue", ImVec2(bw, 0))) {
			for (const StyleColor& s : kStyleColors) CommitStyleString(s.key, s.def);
			TheSettingManager->SetSetting("Main.Menu.Style", "CursorMatchesAccent", true);
			gNeedLoad = true;
		}
		Tip("Puts all menu colors back to the black and blue defaults (not saved until you press Save).");
		ImGui::SameLine();
		if (ImGui::Button("Save", ImVec2(bw * 0.6f, 0))) MenuUI::Save();
		Tip("Writes all settings, including these colors, to your settings file.");
		ImGui::SameLine();
		if (ImGui::Button("Revert", ImVec2(bw * 0.6f, 0))) Revert();
		Tip("Discards every unsaved change and goes back to the last saved settings.");
		ImGui::EndChild();
	}

	void DrawPerformance() {
		if (gModelDirty || gModel.path != gSelected) BuildPerformanceModel();

		DrawPageTitle("Performance");
		ImGui::Spacing();
		ImGui::BeginChild("##scroll", ImVec2(0, 0), ImGuiChildFlags_None);

		for (size_t i = 0; i < gModel.tables.size(); i++) DrawTable(gModel.tables[i], (int)i, false);

		ImGui::SeparatorText("Timings (ms per frame)");
		struct Row { std::string name; float update, render, total; };
		std::vector<Row> rows;
		for (const Category& c : gCategories) {
			if (c.name != "Shaders") continue;
			for (const Item& i : c.items) {
				EffectRecord* e = TheShaderManager->GetEffectByName(i.name.c_str());
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
				rows.push_back({ i.name, update, render, update + render });
			}
		}
		std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.total > b.total; });

		if (ImGui::BeginTable("##timings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
			ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Constants", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Render", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableHeadersRow();
			float sum = 0;
			for (const Row& r : rows) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(r.name.c_str());
				ImGui::TableNextColumn(); ImGui::Text("%.4f", r.update);
				ImGui::TableNextColumn(); ImGui::Text("%.4f", r.render);
				ImGui::TableNextColumn(); ImGui::Text("%.4f", r.total);
				sum += r.total;
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextColored(MenuTheme::Accent(), "Shadow maps");
			ImGui::TableNextColumn(); ImGui::TextUnformatted("");
			ImGui::TableNextColumn(); ImGui::Text("%.4f", TheShadowManager->shadowMapsRenderTime);
			ImGui::TableNextColumn(); ImGui::Text("%.4f", TheShadowManager->shadowMapsRenderTime);
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextColored(MenuTheme::Accent(), "Sum of the above");
			ImGui::TableNextColumn(); ImGui::TextUnformatted("");
			ImGui::TableNextColumn(); ImGui::TextUnformatted("");
			ImGui::TableNextColumn(); ImGui::Text("%.4f", sum);
			ImGui::EndTable();
		}
		ImGui::EndChild();
	}

	// ------------------------------------------------------------------ sidebar

	bool DrawItemRow(const Item& item, bool showSwitch) {
		const bool selected = (gSelected == item.path);
		ImGui::PushID(item.path.c_str());
		bool clicked = false;

		const float switchW = ImGui::GetFrameHeight() * 0.80f * 1.8f;
		const float rowW = ImGui::GetContentRegionAvail().x;

		ImGui::SetNextItemAllowOverlap();
		if (ImGui::Selectable(("##row"), selected, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight()))) clicked = true;
		const ImVec2 after = ImGui::GetCursorPos();

		ImGui::SameLine(ImGui::GetStyle().FramePadding.x + 2.0f);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(item.name.c_str());

		if (showSwitch) {
			ImGui::SameLine(rowW - switchW - 2.0f);
			bool on = IsShaderEnabledCached(item.name);
			if (ToggleSwitch("##sw", &on)) SwitchShader(item.name);
		}
		ImGui::SetCursorPos(after);
		ImGui::PopID();
		if (clicked) Select(item.path);
		return clicked;
	}

	void DrawSidebar() {
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##search", "Search settings", gSearch, sizeof(gSearch));
		ImGui::Spacing();

		ImGui::BeginChild("##list", ImVec2(0, 0), ImGuiChildFlags_None);

		const std::string q = Lower(gSearch);
		if (!q.empty()) {
			bool any = false;
			for (const Category& c : gCategories) {
				for (const Item& i : c.items) {
					bool match = Lower(i.name).find(q) != std::string::npos;
					if (!match && !c.items.empty()) {
						auto e = std::find_if(gSearchIndex.begin(), gSearchIndex.end(), [&](const SearchEntry& s) { return s.path == i.path; });
						match = e != gSearchIndex.end() && e->blob.find(q) != std::string::npos;
					}
					if (!match) continue;
					any = true;
					ImGui::PushID(i.path.c_str());
					if (ImGui::Selectable(("##r"), gSelected == i.path, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight() * 1.6f))) Select(i.path);
					ImGui::SameLine(ImGui::GetStyle().FramePadding.x + 2.0f);
					ImGui::BeginGroup();
					ImGui::TextUnformatted(i.name.c_str());
					ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
					ImGui::TextUnformatted(c.name.c_str());
					ImGui::PopStyleColor();
					ImGui::EndGroup();
					ImGui::PopID();
				}
			}
			if (!any) {
				ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
				ImGui::TextUnformatted("Nothing found.");
				ImGui::PopStyleColor();
			}
			ImGui::EndChild();
			return;
		}

		for (const Category& c : gCategories) {
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			ImGui::PushFont(MenuBackend::FontSemiBold());
			const bool open = ImGui::CollapsingHeader(c.name.c_str());
			ImGui::PopFont();
			if (!open) continue;
			for (const Item& i : c.items) DrawItemRow(i, i.isShader);
			ImGui::Spacing();
		}

		ImGui::PushFont(MenuBackend::FontSemiBold());
		const bool openPerf = ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | (gSelected == kPerformance ? ImGuiTreeNodeFlags_Selected : 0));
		ImGui::PopFont();
		if (ImGui::IsItemClicked()) Select(kPerformance);
		(void)openPerf;

		ImGui::PushFont(MenuBackend::FontSemiBold());
		ImGui::CollapsingHeader("Locations", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | (gSelected == kLocations ? ImGuiTreeNodeFlags_Selected : 0));
		ImGui::PopFont();
		if (ImGui::IsItemClicked()) Select(kLocations);

		ImGui::PushFont(MenuBackend::FontSemiBold());
		ImGui::CollapsingHeader("Style", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | (gSelected == kStyle ? ImGuiTreeNodeFlags_Selected : 0));
		ImGui::PopFont();
		if (ImGui::IsItemClicked()) Select(kStyle);

		if (TheSettingManager->SettingsMain.Locations.PresetEditor) {
			ImGui::PushFont(MenuBackend::FontSemiBold());
			ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | (gSelected == kPresets ? ImGuiTreeNodeFlags_Selected : 0));
			ImGui::PopFont();
			if (ImGui::IsItemClicked()) Select(kPresets);
		}

		ImGui::EndChild();
	}

	void Revert() {
		TheSettingManager->Config.Init();
		for (const std::string& name : gToggledShaders) TheShaderManager->SwitchShaderStatus(name.c_str()); // puts the live effect back
		gToggledShaders.clear();
		TheSettingManager->LoadSettings();
		TheSettingManager->hasUnsavedChanges = false;
		gNeedLoad = false;
		MenuBackend::RequestStyleRefresh();
		gModelDirty = true;
		gEditId.clear();
		RefreshShaderCache();
	}

}

void MenuUI::Save() {
	TheSettingManager->SaveSettings();
	gToggledShaders.clear();
	InterfaceManager->ShowMessage("Settings saved.");
}

void MenuUI::OnOpened() {
	gBuilt = false;
	gModelDirty = true;
	gEditId.clear();
	if (gSelected.empty()) gSelected = "";
}

void MenuUI::Draw() {
	if (!gBuilt) BuildSidebar();
	if (gSelected.empty()) {
		for (const Category& c : gCategories)
			if (!c.items.empty()) { gSelected = c.items[0].path; break; }
	}
	if (++gShaderCacheAge > 30) RefreshShaderCache();

	ImGuiIO& io = ImGui::GetIO();
	const float s = MenuBackend::Scale();

	// Escape closes the menu unless a text field is in use.
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput && gEditId.empty()) {
		TheGameMenuManager->Enabled = false;
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.60f, io.DisplaySize.y * 0.72f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f * s, 420.0f * s), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

	ImGui::Begin(TitleMenu, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	const ImGuiStyle& style = ImGui::GetStyle();
	const bool unsaved = TheSettingManager->hasUnsavedChanges;
	const float barH = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float bodyH = avail.y - (unsaved ? barH + style.ItemSpacing.y : 0.0f);
	const float sideW = std::max(230.0f * s, avail.x * 0.27f);

	ImGui::BeginChild("##side", ImVec2(sideW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
	DrawSidebar();
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("##content", ImVec2(0, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
	if (gSelected == kPerformance) DrawPerformance();
	else if (gSelected == kLocations) DrawLocations();
	else if (gSelected == kPresets) PresetsPage::Draw();
	else if (gSelected == kStyle) DrawStyle();
	else if (!gSelected.empty()) DrawItem();
	ImGui::EndChild();

	if (unsaved) {
		ImGui::BeginChild("##bar", ImVec2(0, barH), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(MenuTheme::Warning(), "You have unsaved changes.");
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
		ImGui::TextUnformatted("Unsaved changes are lost when the game closes.");
		ImGui::PopStyleColor();

		const float bw = 90.0f * s;
		ImGui::SameLine(ImGui::GetContentRegionMax().x - bw * 2.0f - style.ItemSpacing.x);
		if (ImGui::Button("Revert", ImVec2(bw, 0))) Revert();
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Button, MenuTheme::AccentDim());
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, MenuTheme::Accent());
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, MenuTheme::Accent());
		if (ImGui::Button("Save", ImVec2(bw, 0))) MenuUI::Save();
		ImGui::PopStyleColor(3);
		ImGui::EndChild();
	}

	ImGui::End();

	// Apply the values written this frame once (rebuilds the settings structs the effects read).
	if (gNeedLoad) {
		TheSettingManager->LoadSettings();
		gNeedLoad = false;
		MenuBackend::RequestStyleRefresh(); // menu colors may have changed
	}
}
