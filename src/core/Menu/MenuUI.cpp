#include "MenuUI.h"
#include "MenuBackend.h"
#include "MenuTheme.h"
#include "MenuData.h"
#include "MenuWidgets.h"
#include "../../../lib/imgui/imgui.h"
#include <set>
#include <map>
#include <algorithm>
#include <cmath>

// The game headers define min/max macros.
#undef min
#undef max

using ConfigNode = MenuNode;
namespace NodeType = MenuNodeType;
using MenuWidgets::Tip;
using MenuWidgets::ToggleSwitch;

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
		bool								anyPreset = false;	// some value comes from the Performance preset: every value gets the badge slot
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
	constexpr const char* kStyle = "@Style";
	constexpr const char* kKeybinds = "@Keybinds";

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

	bool GetDefaultValue(const ConfigNode& node, std::string& out, bool preset = false) { return Data().DefaultValue(node, preset, out); }

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
		if (c.preset) Data().SetPresetSetting(n);
		else Data().SetSetting(n);
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

	bool IsShaderEnabledCached(const std::string& name) {
		auto it = gShaderEnabled.find(name);
		return it != gShaderEnabled.end() && it->second;
	}

	void RefreshShaderCache() {
		gShaderEnabled.clear();
		for (const Category& c : gCategories)
			if (c.name == "Shaders")
				for (const Item& i : c.items) gShaderEnabled[i.name] = Data().ShaderEnabled(i.name);
		gShaderCacheAge = 0;
	}

	void SwitchShader(const std::string& name) {
		Data().ToggleShader(name);
		if (!gToggledShaders.erase(name)) gToggledShaders.insert(name);
		gNeedLoad = true;
		RefreshShaderCache();
	}

	// ------------------------------------------------------------------ model building

	void FillDefaults(Table& t) {
		for (auto& row : t.rows)
			for (Cell& c : row)
				if (c.present) c.hasDefault = GetDefaultValue(c.node, c.def, c.preset);
	}

	// sections = (display name, full section path). Sections with identical keys share one table.
	void AddTables(Model& m, const std::vector<std::pair<std::string, std::string>>& sections) {
		struct Group { std::string sig; std::vector<std::string> names; std::vector<std::vector<MenuNode>> lists; };
		std::vector<Group> groups;

		for (const auto& [name, full] : sections) {
			std::vector<MenuNode> list;
			Data().MenuSettings(list, full);
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
					cell.preset = Data().IsPresetOverridden(cell.node.Section, cell.node.Key);
					strncpy(cell.text, cell.node.Value, sizeof(cell.text) - 1);
				}
			}
			FillDefaults(t);
			for (const auto& row : t.rows) for (const Cell& c : row) t.anyPreset = t.anyPreset || c.preset;
			m.tables.push_back(std::move(t));
		}
	}

	void BuildItemModel(const std::string& path) {
		gModel = Model();
		gModel.path = path;
		gModel.readOnly = IsWeatherPath(path);

		std::vector<std::string> sections;
		Data().MenuSections(sections, path.c_str());
		std::vector<std::pair<std::string, std::string>> secs;
		for (const std::string& s : sections) secs.push_back({ s, path + "." + s });
		AddTables(gModel, secs);

		if (IsShaderPath(path)) {
			ConfigNode status;
			if (Data().FillNode(status, path + ".Status", "Enabled")) gModel.description = status.Description;
		}
		gModelDirty = false;
	}

	void BuildPerformanceModel() {
		gModel = Model();
		gModel.path = kPerformance;
		AddTables(gModel, { { "Switches", "Main.Main.Performance" }, { "Occlusion culling", "Main.Main.Occlusion" }, { "Profiler and benchmark", "Main.Main.Profiler" } });
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

		std::vector<std::string> top;
		Data().MenuSections(top, nullptr);
		for (const std::string& cat : top) {
			Category c;
			c.name = cat;
			std::vector<std::string> items;
			Data().MenuSections(items, cat.c_str());
			for (const std::string& i : items)
				c.items.push_back({ cat + "." + i, i, cat == "Shaders", cat == "Weathers" });
			gCategories.push_back(std::move(c));
		}

		// Search index over the defaults (names only, no value lookups).
		std::vector<std::pair<std::string, std::string>> idx;
		Data().SearchIndex(idx);
		for (auto& e : idx) gSearchIndex.push_back({ e.first, e.second });

		RefreshShaderCache();
		gBuilt = true;
	}

	void StopRebind();

	void Select(const std::string& path) {
		if (gSelected == path) return;
		StopRebind();
		gSelected = path;
		gModelDirty = true;
		gEditId.clear();
	}

	// ------------------------------------------------------------------ widgets

	float NumberTextWidth() { return ImGui::CalcTextSize("-00000.000").x; }

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

		// The value is written in its own field after the slider, so the text never sits under the grab.
		const float valW = NumberTextWidth();
		bool edited;
		float f = (float)cur;
		int iv = (int)floor(cur + 0.5);
		ImGui::SetNextItemWidth(std::max(30.0f, width - valW - ImGui::GetStyle().ItemSpacing.x));
		if (isInt) edited = ImGui::SliderInt("##v", &iv, (int)lo, (int)hi, "");
		else edited = ImGui::SliderFloat("##v", &f, lo, hi, "");

		const ImGuiID itemId = ImGui::GetItemID();
		const bool itemActivated = ImGui::IsItemActivated();
		const bool itemHovered = ImGui::IsItemHovered();
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(FormatNumber(isInt ? (double)iv : (double)f, isInt).c_str());
		const double now = ImGui::GetTime();
		if (itemActivated) {
			// keep the value from before the first click of a double click, so the click does not move it
			if (!(gClick.id == itemId && now - gClick.time < ImGui::GetIO().MouseDoubleClickTime)) gClick.startValue = (float)cur;
			gClick.id = itemId;
			gClick.time = now;
		}
		if (itemHovered && ImGui::IsMouseDoubleClicked(0) && gClick.id == itemId) {
			Commit(c, FormatNumber(gClick.startValue, isInt));
			gEditId = id;
			snprintf(gEditBuf, sizeof(gEditBuf), "%s", c.node.Value);
			gEditFocus = true;
			return true;
		}
		if (edited) { Commit(c, FormatNumber(isInt ? (double)iv : (double)f, isInt)); changed = true; }
		return changed;
	}

	void DrawCell(Cell& c, const std::string& id, bool readOnly, bool badgeSlot) {
		ImGui::PushID(id.c_str());
		const ImGuiStyle& style = ImGui::GetStyle();
		const float resetW = ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f;
		const float badgeW = MenuWidgets::BadgeWidth("PRESET");
		const float x0 = ImGui::GetCursorPosX();
		const float availW = ImGui::GetContentRegionAvail().x;
		const float width = std::max(40.0f, availW - resetW - style.ItemSpacing.x - (badgeSlot ? badgeW + style.ItemSpacing.x : 0.0f));

		if (badgeSlot) {
			// Small outlined badge: this row's value belongs to the Performance preset. Every row of a table that
			// has one keeps the slot, so the values line up.
			if (c.preset) MenuWidgets::Badge("PRESET"); else ImGui::Dummy(ImVec2(badgeW, 0));
			ImGui::SameLine();
		}

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
		Tip(c.preset ? std::string("Value used while the Performance preset is on. Edits change the preset's value; Reset goes back to the preset default.\n\n") + c.node.Description : c.node.Description);

		// The Reset slot sits at the right edge of the column and is always reserved, so controls start at the
		// same x in every row and nothing jumps when a row changes.
		ImGui::SameLine(x0 + availW - resetW);
		if (!readOnly && IsChanged(c)) {
			if (ImGui::Button("Reset", ImVec2(resetW, 0))) {
				if (c.preset) {
					// Back to the preset's own value: drop the user's override instead of storing the default.
					Data().RemovePresetSetting(c.node.Section, c.node.Key);
					strncpy(c.node.Value, c.def.c_str(), sizeof(c.node.Value) - 1);
					strncpy(c.text, c.node.Value, sizeof(c.text) - 1);
					gNeedLoad = true;
				}
				else Commit(c, c.def);
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

		const float s = MenuBackend::Scale();
		const ImGuiStyle& style = ImGui::GetStyle();
		const int ncol = (int)t.cols.size();
		const bool multi = ncol > 1;
		const float total = ImGui::GetContentRegionAvail().x;
		const float pad = style.CellPadding.x * 2.0f;
		const float dot = ImGui::GetFontSize() * 0.55f + 4.0f;

		// Label column: as wide as the longest key (plus the changed-dot), 30-50% of the width.
		float keyW = 0.0f;
		for (const std::string& k : t.keys) keyW = std::max(keyW, ImGui::CalcTextSize(k.c_str()).x);
		const float labelW = std::min(std::max(keyW + dot + pad, total * 0.30f), total * 0.50f);
		// Value columns: never narrower than 170*scale; when they do not fit the table scrolls sideways.
		const float overhead = ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x * 2.0f + pad
			+ (t.anyPreset ? MenuWidgets::BadgeWidth("PRESET") + style.ItemSpacing.x : 0.0f);
		bool anyNumber = false;
		for (const auto& row : t.rows) for (const Cell& c : row) anyNumber = anyNumber || (c.present && c.node.Type != NodeType::Boolean);
		const float valueMin = anyNumber ? 100.0f * s + NumberTextWidth() + style.ItemSpacing.x : MenuWidgets::ToggleSwitchWidth();
		const float colW = multi ? std::max(valueMin + overhead, (total - labelW - pad) / ncol) : 0.0f;
		ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingFixedFit;
		if (multi && labelW + colW * ncol > total - pad + 0.5f) flags |= ImGuiTableFlags_ScrollX;

		const std::string tid = "##table" + std::to_string(tableIndex);
		// A table that scrolls sideways gets exactly the height of its rows plus the scroll bar, so it never scrolls
		// vertically inside the page's own scroll area.
		ImVec2 outer(0.0f, 0.0f);
		if (flags & ImGuiTableFlags_ScrollX) {
			const float rowH = ImGui::GetFrameHeight() + style.CellPadding.y * 2.0f;
			outer.y = (ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f) + rowH * (float)t.keys.size() + style.ScrollbarSize + 3.0f;
		}
		if (!ImGui::BeginTable(tid.c_str(), 1 + ncol, flags, outer)) return;

		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, labelW);
		for (const std::string& c : t.cols) {
			if (multi) ImGui::TableSetupColumn(c.c_str(), ImGuiTableColumnFlags_WidthFixed, colW);
			else ImGui::TableSetupColumn(c.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		}
		if (flags & ImGuiTableFlags_ScrollX) ImGui::TableSetupScrollFreeze(1, 0);
		if (multi) ImGui::TableHeadersRow();

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
			if (changed) {
				const ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + dot * 0.4f, p.y + ImGui::GetFrameHeight() * 0.5f), dot * 0.28f, ImGui::GetColorU32(MenuTheme::Accent()));
			}
			ImGui::Indent(dot);
			const bool cut = MenuWidgets::Ellipsized(t.keys[k]);
			Tip(cut ? t.keys[k] + (desc.empty() ? "" : "\n\n" + desc) : desc);
			ImGui::Unindent(dot);

			for (int c = 0; c < ncol; c++) {
				ImGui::TableSetColumnIndex(1 + c);
				Cell& cell = t.rows[k][c];
				if (cell.present) DrawCell(cell, std::to_string(tableIndex) + "_" + std::to_string(k) + "_" + std::to_string(c), readOnly, t.anyPreset);
			}
		}
		ImGui::EndTable();
	}

	void DrawPageTitle(const char* title) {
		MenuWidgets::PageHeader(title);
	}

	void DrawItem() {
		if (gModelDirty || gModel.path != gSelected) BuildItemModel(gSelected);

		const bool shader = IsShaderPath(gSelected);
		const std::string name = gSelected.substr(gSelected.find('.') + 1);

		DrawPageTitle(name.c_str());
		if (shader) {
			ImGui::SameLine(ImGui::GetContentRegionMax().x - MenuWidgets::ToggleSwitchWidth());
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
				Data().ForceWeather(name);
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

		ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
		ImGui::Text("Presets and teleport: press %s for the Locations window.", Data().LocationsKeyName().c_str());
		ImGui::PopStyleColor();
		ImGui::Spacing();

		const LocationStatus& st = Data().LocStatus();
		if (!st.Warning.empty()) {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(MenuTheme::Warning(), "%s", st.Warning.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
		}

		for (size_t i = 0; i < gModel.tables.size(); i++) DrawTable(gModel.tables[i], (int)i, false);

		ImGui::EndChild();
	}

	// ---- Style page: the menu colors. Each change writes all style keys so the file is complete and the
	// old-default migration in SettingManager never mistakes a half-written section for an untouched one.
	struct StyleColor { const char* key; const char* label; const char* tip; const char* def; };
	const StyleColor kStyleColors[] = {
		{ "AccentColor", "Accent", "Checkmarks, sliders, selection and highlights.", "40,140,255" },
		{ "BackgroundColor", "Background", "The menu window.", "8,9,12" },
		{ "PanelColor", "Panels", "Sidebar, content area and popups.", "14,17,24" },
		{ "FrameColor", "Fields and buttons", "Input fields, buttons and table rows. Hover and pressed shades are derived from it.", "22,28,40" },
		{ "TextColor", "Text", "Text color. The muted and disabled shades are derived from it.", "226,232,242" },
		{ "CursorColor", "Cursor", "Mouse cursor color, used only when 'Cursor matches accent' is off.", "255,255,255" },
	};

	void CommitStyleString(const char* key, const std::string& value) {
		ConfigNode n;
		if (!Data().FillNode(n, "Main.Menu.Style", key)) return;
		strncpy(n.Value, value.c_str(), sizeof(n.Value) - 1);
		n.Value[sizeof(n.Value) - 1] = 0;
		Data().SetSetting(n);
	}

	std::string RgbString(const unsigned char* c) {
		char b[24];
		snprintf(b, sizeof(b), "%d,%d,%d", c[0], c[1], c[2]);
		return b;
	}

	// Writes the current values (with one replaced) and asks for a reload + theme refresh.
	void CommitStyle(const char* changedKey, const std::string& changedValue, bool cursorMatches) {
		for (const StyleColor& s : kStyleColors) {
			unsigned char cur[3];
			Data().StyleRgb(s.key, cur);
			CommitStyleString(s.key, !strcmp(s.key, changedKey) ? changedValue : RgbString(cur));
		}
		Data().SetBool("Main.Menu.Style", "CursorMatchesAccent", cursorMatches);
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
		bool matches = Data().CursorMatchesAccent();
		for (const StyleColor& s : kStyleColors) {
			const bool cursorRow = !strcmp(s.key, "CursorColor");
			if (cursorRow) {
				if (ImGui::Checkbox("Cursor matches accent", &matches)) CommitStyle("", "", matches);
				Tip("Draw the mouse cursor in the accent color. Turn off to pick its own color below.");
			}
			unsigned char c[3];
			Data().StyleRgb(s.key, c);
			float col[3] = { c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f };
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(s.label);
			Tip(s.tip);
			ImGui::SameLine(labelW);
			ImGui::SetNextItemWidth(-1);
			if (cursorRow && matches) ImGui::BeginDisabled();
			if (ImGui::ColorEdit3((std::string("##") + s.key).c_str(), col, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Uint8)) {
				unsigned char v[3] = { (unsigned char)(col[0] * 255.0f + 0.5f), (unsigned char)(col[1] * 255.0f + 0.5f), (unsigned char)(col[2] * 255.0f + 0.5f) };
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
			Data().SetBool("Main.Menu.Style", "CursorMatchesAccent", true);
			gNeedLoad = true;
		}
		Tip("Puts all menu colors back to the black and blue defaults (not saved until you press Save).");
		ImGui::TextDisabled("Save and Revert are in the bar at the top.");
		ImGui::EndChild();
	}

	// ---- Keybinds page: every setting that holds a DirectInput scancode. Rebind captures the next key press from the
	// same DirectInput snapshot the game reads, so the stored numbers are exactly what the settings expect.
	struct Binding { const char* group; const char* section; const char* key; const char* tip; bool canClear; };
	const Binding kBindings[] = {
		{ "Menu", "Main.Menu.Keys", "KeyEnable", "Opens and closes this menu.", false },	// 0 would lock the menu away
		{ "Menu", "Main.Menu.Keys", "KeyLocations", "Opens and closes the Locations window.", true },
		{ "Menu", "Main.Menu.Keys", "KeySave", "Saves the settings while the menu is open.", true },
		{ "Occlusion culling", "Main.Main.Occlusion", "ToggleKey", "Turns occlusion culling on and off while you play.", true },
		{ "Occlusion culling", "Main.Main.OcclusionTuning", "CompareKey", "With DevTools: switches one occlusion feature at a time.", true },
		{ "Tools", "Main.Main.Misc", "ScreenshotKey", "Screenshot without the HUD (saved as jpg).", true },
		{ "Tools", "Main.Develop.Main", "TraceShaders", "With DebugMode: prints the used shaders to the log.", true },
		{ "Tools", "Main.FlyCam.Main", "KeyAdd", "Fly camera: raise the speed step.", true },
		{ "Tools", "Main.FlyCam.Main", "KeySubtract", "Fly camera: lower the speed step.", true },
	};
	constexpr int kBindingCount = (int)(sizeof(kBindings) / sizeof(kBindings[0]));
	int gRebind = -1;	// binding waiting for a key

	int BindingValue(const Binding& b) {
		ConfigNode n;
		return Data().FillNode(n, b.section, b.key) ? atoi(n.Value) : 0;
	}

	int BindingDefault(const Binding& b) {
		ConfigNode n;
		std::string d;
		if (!Data().FillNode(n, b.section, b.key) || !Data().DefaultValue(n, false, d)) return 0;
		return atoi(d.c_str());
	}

	void SetBinding(const Binding& b, int value) {
		ConfigNode n;
		if (!Data().FillNode(n, b.section, b.key)) return;
		snprintf(n.Value, sizeof(n.Value), "%d", value);
		Data().SetSetting(n);
		gNeedLoad = true;
	}

	void StopRebind() {
		if (gRebind >= 0) Data().CancelKeyCapture();
		gRebind = -1;
	}

	void DrawKeybinds() {
		if (gRebind >= 0) {
			const int k = Data().PollCapturedKey();
			if (k >= 0) {
				if (k != 1) SetBinding(kBindings[gRebind], k);	// 1 = Esc: cancel
				gRebind = -1;
			}
		}

		MenuWidgets::PageHeader("Keybinds", "Rebind, then press the new key (Esc cancels). Keys apply at once; Save keeps them.");
		ImGui::Spacing();
		ImGui::BeginChild("##scroll", ImVec2(0, 0), ImGuiChildFlags_None);

		const ImGuiStyle& style = ImGui::GetStyle();
		const float s = MenuBackend::Scale();
		const float btnPad = style.FramePadding.x * 2.0f + 8.0f * s;
		const float rebindW = ImGui::CalcTextSize("Rebind").x + btnPad;
		const float clearW = ImGui::CalcTextSize("Clear").x + btnPad;
		const float resetW = ImGui::CalcTextSize("Reset").x + btnPad;
		float nameW = 0.0f;
		for (const Binding& b : kBindings) nameW = std::max(nameW, ImGui::CalcTextSize(b.key).x);

		int value[kBindingCount];
		for (int i = 0; i < kBindingCount; i++) value[i] = BindingValue(kBindings[i]);

		const char* group = "";
		for (int i = 0; i < kBindingCount; i++) {
			const Binding& b = kBindings[i];
			if (strcmp(group, b.group)) {
				group = b.group;
				ImGui::SeparatorText(group);
			}
			const bool capturing = gRebind == i;
			const int def = BindingDefault(b);
			ImGui::PushID(i);

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(b.key);
			ImGui::SameLine(nameW + 28.0f * s);

			// key name
			ImGui::AlignTextToFramePadding();
			const float keyX = nameW + 28.0f * s;
			const float keyW = 150.0f * s;
			if (capturing) ImGui::TextColored(MenuTheme::Accent(), "Press a key...");
			else if (value[i] == 0) { ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted()); ImGui::TextUnformatted("None"); ImGui::PopStyleColor(); }
			else ImGui::Text("%s", Data().KeyName(value[i]).c_str());
			ImGui::SameLine(keyX + keyW);
			if (!capturing && value[i] != 0) {
				ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
				ImGui::Text("(%d)", value[i]);
				ImGui::PopStyleColor();
				ImGui::SameLine(keyX + keyW + 56.0f * s);
			}
			else ImGui::SameLine(keyX + keyW + 56.0f * s);

			// buttons
			if (capturing) {
				if (ImGui::Button("Cancel", ImVec2(rebindW, 0))) StopRebind();
			}
			else if (ImGui::Button("Rebind", ImVec2(rebindW, 0))) {
				StopRebind();
				gRebind = i;
				Data().BeginKeyCapture();
			}
			Tip(std::string(b.tip) + "\n\nSetting: " + b.section + "." + b.key);
			ImGui::SameLine();
			ImGui::BeginDisabled(!b.canClear || value[i] == 0 || capturing);
			if (ImGui::Button("Clear", ImVec2(clearW, 0))) SetBinding(b, 0);
			ImGui::EndDisabled();
			Tip(b.canClear ? "Unbind this key (0)." : "This key cannot be cleared: without it the menu could not be opened.");
			ImGui::SameLine();
			ImGui::BeginDisabled(value[i] == def || capturing);
			if (ImGui::Button("Reset", ImVec2(resetW, 0))) SetBinding(b, def);
			ImGui::EndDisabled();
			Tip("Back to the default: " + (def ? Data().KeyName(def) : std::string("None")));

			// the same key on another binding: allowed, but said out loud
			std::string also;
			if (value[i] != 0)
				for (int j = 0; j < kBindingCount; j++)
					if (j != i && value[j] == value[i]) also += (also.empty() ? "" : ", ") + std::string(kBindings[j].key);
			if (!also.empty()) {
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(MenuTheme::Warning(), "Also used by %s", also.c_str());
				Tip("Both bindings react to this key. That is allowed, but usually not what you want.");
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	void DrawPerformance() {
		if (gModelDirty || gModel.path != gSelected) BuildPerformanceModel();

		DrawPageTitle("Performance");
		ImGui::Spacing();
		ImGui::BeginChild("##scroll", ImVec2(0, 0), ImGuiChildFlags_None);

		for (size_t i = 0; i < gModel.tables.size(); i++) {
			DrawTable(gModel.tables[i], (int)i, false);
			if (gModel.tables[i].title == "Occlusion culling") {	// state of the built-in module (Enabled and the Throttle switches need a restart)
				ImGui::TextDisabled("%s Enabled needs a restart; the other rows apply at once.", Data().OcclusionStatus().c_str());
			}
		}

		ImGui::SeparatorText("Timings (ms per frame)");
		std::vector<MenuTiming> rows;
		float shadowMaps = 0;
		Data().Timings(rows, shadowMaps);
		std::sort(rows.begin(), rows.end(), [](const MenuTiming& a, const MenuTiming& b) { return a.total > b.total; });

		if (ImGui::BeginTable("##timings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
			const float numW = ImGui::CalcTextSize("0.0000").x + ImGui::GetStyle().CellPadding.x * 2.0f + 28.0f * MenuBackend::Scale();
			ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Constants", ImGuiTableColumnFlags_WidthFixed, numW);
			ImGui::TableSetupColumn("Render", ImGuiTableColumnFlags_WidthFixed, numW);
			ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, numW);
			ImGui::TableHeadersRow();
			auto num = [](float v) {
				char buf[24];
				snprintf(buf, sizeof(buf), "%.4f", v);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(buf).x));
				ImGui::TextUnformatted(buf);
			};
			float sum = 0;
			for (const MenuTiming& r : rows) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(r.name.c_str());
				ImGui::TableNextColumn(); num(r.update);
				ImGui::TableNextColumn(); num(r.render);
				ImGui::TableNextColumn(); num(r.total);
				sum += r.total;
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextColored(MenuTheme::Accent(), "Shadow maps");
			ImGui::TableNextColumn();
			ImGui::TableNextColumn(); num(shadowMaps);
			ImGui::TableNextColumn(); num(shadowMaps);
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextColored(MenuTheme::Accent(), "Sum of the above");
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();
			ImGui::TableNextColumn(); num(sum);
			ImGui::EndTable();
		}
		ImGui::EndChild();
	}

	// ------------------------------------------------------------------ sidebar

	bool DrawItemRow(const Item& item, bool showSwitch) {
		const bool selected = (gSelected == item.path);
		ImGui::PushID(item.path.c_str());
		bool clicked = false;

		const float switchW = MenuWidgets::ToggleSwitchWidth();
		const float rowW = ImGui::GetContentRegionAvail().x;
		const float pad = ImGui::GetStyle().FramePadding.x + 2.0f;

		const float x0 = ImGui::GetCursorPosX();	// SameLine offsets count from the window edge, not from the padding
		ImGui::SetNextItemAllowOverlap();
		if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight()))) clicked = true;
		const ImVec2 after = ImGui::GetCursorPos();

		ImGui::SameLine(x0 + pad);
		ImGui::AlignTextToFramePadding();
		const float textW = rowW - pad - 4.0f - (showSwitch ? switchW + ImGui::GetStyle().ItemSpacing.x : 0.0f);
		if (MenuWidgets::Ellipsized(item.name, textW)) Tip(item.name);

		if (showSwitch) {
			ImGui::SameLine(x0 + rowW - switchW - 2.0f);
			bool on = IsShaderEnabledCached(item.name);
			if (ToggleSwitch("##sw", &on)) SwitchShader(item.name);
		}
		ImGui::SetCursorPos(after);
		ImGui::PopID();
		if (clicked) Select(item.path);
		return clicked;
	}

	void DrawSidebar() {
		const ImGuiStyle& style = ImGui::GetStyle();
		// The three general pages stay in a footer so they never scroll away.
		const float footerH = ImGui::GetFrameHeight() * 4.0f + style.ItemSpacing.y * 4.0f + 2.0f;
		ImGui::BeginChild("##list", ImVec2(0, -footerH), ImGuiChildFlags_None);

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
					if (ImGui::Selectable("##r", gSelected == i.path, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight() * 1.6f))) Select(i.path);
					ImGui::SameLine(style.FramePadding.x + 2.0f);
					ImGui::BeginGroup();
					MenuWidgets::Ellipsized(i.name, ImGui::GetContentRegionAvail().x - 4.0f);
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
		}
		else {
			for (const Category& c : gCategories) {
				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				ImGui::PushFont(MenuBackend::FontSemiBold());
				const bool open = ImGui::CollapsingHeader(c.name.c_str());
				ImGui::PopFont();
				if (!open) continue;
				for (const Item& i : c.items) DrawItemRow(i, i.isShader);
				ImGui::Spacing();
			}
		}
		ImGui::EndChild();

		ImGui::Separator();
		static const Item pinned[4] = {
			{ kPerformance, "Performance", false, false },
			{ kLocations, "Locations", false, false },
			{ kKeybinds, "Keybinds", false, false },
			{ kStyle, "Style", false, false },
		};
		for (const Item& i : pinned) DrawItemRow(i, false);
	}

	void Revert() {
		Data().Revert(gToggledShaders);
		gToggledShaders.clear();
		gNeedLoad = false;
		gModelDirty = true;
		gEditId.clear();
		RefreshShaderCache();
	}

}

void MenuUI::Save() {
	Data().Save();
	gToggledShaders.clear();
}

void MenuUI::BeginRebind(const char* key) {
	Select(kKeybinds);
	for (int i = 0; i < kBindingCount; i++)
		if (!strcmp(kBindings[i].key, key)) { StopRebind(); gRebind = i; Data().BeginKeyCapture(); }
}

void MenuUI::SelectPage(const char* path) {
	Select(path);
}

void MenuUI::OnOpened() {
	StopRebind();
	gBuilt = false;
	gModelDirty = true;
	gEditId.clear();
	if (gSelected.empty()) gSelected = "";
}

namespace {
	// Fixed bar across the top: title, search, unsaved indicator, Revert and Save (always there, greyed out when
	// nothing changed), so the body below never changes size.
	void DrawHeaderBar(bool unsaved) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const float s = MenuBackend::Scale();
		const float barH = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
		ImGui::BeginChild("##hdr", ImVec2(0, barH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGui::AlignTextToFramePadding();
		ImGui::PushFont(MenuBackend::FontSemiBold());
		ImGui::TextUnformatted("Settings");
		ImGui::PopFont();

		const float total = ImGui::GetContentRegionMax().x;
		const float bw = std::max(ImGui::CalcTextSize("Revert").x, ImGui::CalcTextSize("Save").x) + style.FramePadding.x * 2.0f + 24.0f * s;
		const char* unsavedText = "Unsaved changes";
		const float dotW = ImGui::GetFontSize() * 0.9f;
		const float unsavedW = dotW + ImGui::CalcTextSize(unsavedText).x;
		const float right = bw * 2.0f + style.ItemSpacing.x + (unsaved ? unsavedW + style.ItemSpacing.x * 2.0f : 0.0f);

		ImGui::SameLine(0.0f, 28.0f * s);
		const float searchW = std::max(120.0f * s, std::min(320.0f * s, total - ImGui::GetCursorPosX() - right - 20.0f * s));
		ImGui::SetNextItemWidth(searchW);
		ImGui::InputTextWithHint("##search", "Search settings", gSearch, sizeof(gSearch));

		ImGui::SameLine(total - right);
		if (unsaved) {
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + dotW * 0.4f, p.y + ImGui::GetFrameHeight() * 0.5f), dotW * 0.22f, ImGui::GetColorU32(MenuTheme::Warning()));
			ImGui::Dummy(ImVec2(dotW, 0));
			ImGui::SameLine(0, 0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(MenuTheme::Warning(), "%s", unsavedText);
			MenuWidgets::Tip("Unsaved changes are lost when the game closes.");
			ImGui::SameLine(0, style.ItemSpacing.x * 2.0f);
		}
		const char* labels[2] = { "Revert", "Save" };
		const bool enabled[2] = { unsaved, unsaved };
		// two fixed-width buttons at the right end
		for (int i = 0; i < 2; i++) {
			if (i) ImGui::SameLine();
			ImGui::BeginDisabled(!enabled[i]);
			if (i == 1) {
				ImGui::PushStyleColor(ImGuiCol_Button, MenuTheme::AccentDim());
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, MenuTheme::Accent());
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, MenuTheme::Accent());
			}
			if (ImGui::Button(labels[i], ImVec2(bw, 0))) { if (i == 0) Revert(); else MenuUI::Save(); }
			if (i == 1) ImGui::PopStyleColor(3);
			ImGui::EndDisabled();
			MenuWidgets::Tip(i == 0 ? "Discards every unsaved change and goes back to the last saved settings." : "Writes all settings to your settings file.");
		}
		ImGui::EndChild();
	}

	float gSideUnits = 260.0f;	// sidebar width at 1080p, kept across frames (the splitter changes it)
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
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput && gEditId.empty() && !Data().KeyCaptureActive()) {
		Data().CloseMenu();
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.70f, io.DisplaySize.y * 0.80f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(900.0f * s, io.DisplaySize.x), std::min(560.0f * s, io.DisplaySize.y)), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

	ImGui::Begin("New Vegas Reloaded - Settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	DrawHeaderBar(Data().HasUnsaved());

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float maxSide = std::max(120.0f, std::min(420.0f * s, avail.x * 0.5f));
	const float minSide = std::min(200.0f * s, maxSide);
	float sideW = std::max(minSide, std::min(maxSide, gSideUnits * s));

	ImGui::BeginChild("##side", ImVec2(sideW, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding);
	DrawSidebar();
	ImGui::EndChild();

	if (MenuWidgets::SplitterV("##split", &sideW, minSide, maxSide, avail.y)) gSideUnits = sideW / s;

	ImGui::BeginChild("##content", ImVec2(0, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding);
	if (gSelected == kPerformance) DrawPerformance();
	else if (gSelected == kLocations) DrawLocations();
	else if (gSelected == kKeybinds) DrawKeybinds();
	else if (gSelected == kStyle) DrawStyle();
	else if (!gSelected.empty()) DrawItem();
	ImGui::EndChild();

	ImGui::End();

	// Apply the values written this frame once (rebuilds the settings structs the effects read).
	if (gNeedLoad) {
		Data().ApplyChanges();
		gNeedLoad = false;
	}
}
