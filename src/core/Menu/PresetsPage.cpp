#include "PresetsPage.h"
#include "MenuBackend.h"
#include "MenuTheme.h"
#include "MenuData.h"
#include "MenuWidgets.h"
#include "../Locations/IniDocument.h"
#include "../../../lib/imgui/imgui.h"
#include <map>
#include <set>
#include <algorithm>

// The game headers define min/max macros.
#undef min
#undef max

using namespace Locations;
using MenuWidgets::Tip;
using MenuWidgets::Muted;

namespace {
	struct OpenFile {
		std::string		path;
		IniDocument		doc;		// the edited copy
		IniDocument		orig;		// as on disk
		bool			missing = false;
		bool			Dirty() const { return doc.Text() != orig.Text(); }
	};

	struct Selection {
		std::string		path;		// empty: nothing selected
		std::string		label;
		int				feature = -1;			// -1 for Settings.ini
		std::string		preset;					// preset name when this is a preset file, else empty
	};

	std::map<std::string, OpenFile>	gOpen;		// by lowercase path
	Selection						gSel;
	int								gFeature = 0;
	char							gFilter[64] = {};
	char							gUseFilter[64] = {};	// "where it is used" list filter
	bool							gShowMissing = false;	// show locations not in the load order
	std::string						gStatus;
	bool							gStatusError = false;
	char							gNewName[64] = {};
	int								gNewFrom = 0;
	bool							gOpenNew = false;
	bool							gOpenDelete = false;
	bool							gAddOpen = false;
	char							gAddSection[64] = {};
	char							gAddKey[64] = {};
	char							gAddValue[128] = {};

	void SetStatus(const std::string& s, bool error) { gStatus = s; gStatusError = error; }

	OpenFile& Open(const std::string& path) {
		std::string k = Lower(path);
		auto it = gOpen.find(k);
		if (it != gOpen.end()) return it->second;
		OpenFile& f = gOpen[k];
		f.path = path;
		f.missing = !f.orig.Load(path);
		f.doc = f.orig;
		return f;
	}

	void Select(const std::string& path, const std::string& label, int feature, const std::string& preset) {
		gSel.path = path; gSel.label = label; gSel.feature = feature; gSel.preset = preset;
		gAddSection[0] = gAddKey[0] = gAddValue[0] = 0;
	}

	float Btn() { return 84.0f * MenuBackend::Scale(); }	// common button width

	std::string FileName(const std::string& p) {
		size_t s = p.find_last_of("\\/");
		return s == std::string::npos ? p : p.substr(s + 1);
	}

	bool ValidName(const char* n) {
		if (!*n) return false;
		for (const char* c = n; *c; c++) {
			if (!(isalnum((unsigned char)*c) || *c == '_' || *c == '-' || *c == ' ' || *c == '.')) return false;
		}
		return n[0] != ' ' && n[0] != '.' && n[strlen(n) - 1] != ' ' && n[strlen(n) - 1] != '.';
	}

	void ReloadConfig() {
		Data().LocReload();
	}

	void SaveFile(OpenFile& f) {
		std::string err;
		if (!f.doc.Save(f.path, &err)) { SetStatus("Save failed: " + err, true); return; }
		f.orig = f.doc;
		f.missing = false;
		ReloadConfig();
		SetStatus("Saved " + FileName(f.path) + " and reloaded the presets.", false);
		Logger::Log("[Presets] saved %s", f.path.c_str());
	}

	void RevertFile(OpenFile& f) {
		f.missing = !f.orig.Load(f.path);
		f.doc = f.orig;
		SetStatus("Reverted " + FileName(f.path) + ".", false);
	}

	std::string PresetPath(const LocationConfig& cfg, int feature, const std::string& name) {
		return cfg.Root + "\\" + cfg.Features[feature].PresetFolder + "\\" + name + ".ini";
	}

	void CreatePreset(const LocationConfig& cfg, int f, const std::string& name, const Preset* from) {
		std::string path = PresetPath(cfg, f, name);
		CreateDirectoryA(cfg.Root.c_str(), nullptr);
		CreateDirectoryA((cfg.Root + "\\" + cfg.Features[f].PresetFolder).c_str(), nullptr);
		if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) { SetStatus("A file named " + FileName(path) + " already exists.", true); return; }
		IniDocument doc;
		if (from) {
			if (!doc.Load(from->path)) { SetStatus("Could not read " + from->path, true); return; }
		} else {
			doc.Parse("");
		}
		std::string err;
		if (!doc.Save(path, &err)) { SetStatus("Could not create the preset: " + err, true); return; }
		Logger::Log("[Presets] created %s%s%s", path.c_str(), from ? " from " : "", from ? from->name.c_str() : "");
		ReloadConfig();
		OpenFile& o = Open(path);
		(void)o;
		Select(path, name, f, name);
		SetStatus("Created " + name + ". Add it to a location with the button on the right.", false);
	}

	void DeletePreset(const LocationConfig& cfg, int f, const Preset& p) {
		std::string target = p.path + ".nvrdeleted";
		if (!MoveFileExA(p.path.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) { SetStatus("Could not rename " + FileName(p.path) + " (error " + std::to_string(GetLastError()) + ").", true); return; }
		Logger::Log("[Presets] deleted %s (renamed to .nvrdeleted)", p.path.c_str());
		gOpen.erase(Lower(p.path));
		gSel = Selection();
		ReloadConfig();
		SetStatus("Renamed to " + FileName(target) + ". It no longer loads.", false);
		(void)cfg; (void)f;
	}

	// Adds the preset to the current location's [Overrides] line in the feature ini.
	void AddToCurrentLocation(const LocationConfig& cfg, int f, const std::string& preset) {
		const LocationStatus& st = Data().LocStatus();
		if (st.Target.empty()) { SetStatus("No current location.", true); return; }
		const FeatureConfig& fc = cfg.Features[f];
		OpenFile& file = Open(fc.IniPath);
		std::vector<std::string> list;
		const std::string* cur = file.doc.Get("Overrides", st.Target);
		std::string value;
		if (cur) {
			value = *cur;
			bool have = false;
			size_t pos = 0;
			while (pos <= value.size()) {
				size_t c = value.find(',', pos);
				std::string t = value.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
				while (!t.empty() && t.front() == ' ') t.erase(t.begin());
				while (!t.empty() && t.back() == ' ') t.pop_back();
				if (Lower(t) == Lower(preset)) have = true;
				if (c == std::string::npos) break;
				pos = c + 1;
			}
			if (have) { SetStatus(preset + " is already in the Overrides of " + st.Target + ".", false); return; }
			if (!value.empty()) value += ",";
			value += preset;
		} else {
			// start from what the location has now so nothing changes except the addition
			const Resolution& r = st.Features[f];
			for (const std::string& s : r.stack) {
				if (!r.defaultPreset.empty() && Lower(s) == Lower(r.defaultPreset)) continue;
				if (Lower(s) == Lower(preset)) continue;
				value += s + ",";
			}
			value += preset;
		}
		file.doc.Set("Overrides", st.Target, value);
		Select(fc.IniPath, fc.IniName, f, "");
		SetStatus("Added " + preset + " to [Overrides] " + st.Target + " in " + fc.IniName + ". Save it to apply.", false);
	}

	/* ------------------------------------------------------------ layout state */

	float		gLeftUnits = 300.0f;		// left pane width at 1080p (the splitter changes it)
	int			gSubTab = 0;				// right pane: 0 = Values, 1 = Used by
	std::string	gAddKeyIn;					// lowercase section whose "Add key" row is open
	bool		gAddSectionOpen = false;

	ImVec2 DisplayCenter() {
		const ImVec2 d = ImGui::GetIO().DisplaySize;
		return ImVec2(d.x * 0.5f, d.y * 0.5f);
	}

	// A table cell with one line of text, cut with "..." (full text as tooltip when cut).
	void CellText(const std::string& text) {
		if (MenuWidgets::Ellipsized(text)) Tip(text);
	}

	/* ------------------------------------------------------------ left pane */

	void DrawFileLink(const std::string& label, const std::string& path, int feature, const std::string& preset, const char* tag) {
		const bool sel = Lower(gSel.path) == Lower(path);
		std::string text = label;
		if (tag && *tag) text += std::string("  (") + tag + ")";
		auto it = gOpen.find(Lower(path));
		if (it != gOpen.end() && it->second.Dirty()) text += "  *";
		if (ImGui::Selectable((text + "##f" + path).c_str(), sel)) Select(path, label, feature, preset);
	}

	// One selectable row per preset type with the preset count on the right. Types that do nothing are muted
	// and say why in a tooltip.
	void DrawFeatureList(const LocationConfig& cfg) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const float pad = style.FramePadding.x + 2.0f;
		for (int f = 0; f < FeatureCount; f++) {
			const FeatureConfig& fc = cfg.Features[f];
			const bool act = fc.Active();
			ImGui::PushID(f);
			const float x0 = ImGui::GetCursorPosX();
			const float rowW = ImGui::GetContentRegionAvail().x;
			ImGui::SetNextItemAllowOverlap();
			if (ImGui::Selectable("##feat", gFeature == f, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight()))) gFeature = f;
			if (!act) Tip(!fc.SwitchOn ? std::string(fc.Name) + " is switched off in Settings.ini (" + fc.SwitchKey + ")."
				: std::string(fc.IniName) + " is missing, so " + fc.Name + " does nothing.");
			const ImVec2 after = ImGui::GetCursorPos();
			ImGui::SameLine(x0 + pad);
			ImGui::AlignTextToFramePadding();
			if (!act) ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
			ImGui::TextUnformatted(fc.Name);
			if (!act) ImGui::PopStyleColor();
			char count[24];
			snprintf(count, sizeof(count), "%d", act ? (int)fc.Presets.size() : 0);
			ImGui::SameLine(x0 + rowW - ImGui::CalcTextSize(count).x - pad);
			ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
			ImGui::TextUnformatted(count);
			ImGui::PopStyleColor();
			ImGui::SetCursorPos(after);
			ImGui::PopID();
		}
	}

	void DrawModals(const LocationConfig& cfg, const std::vector<const Preset*>& list) {
		const float s = MenuBackend::Scale();
		const FeatureConfig& fc = cfg.Features[gFeature];

		if (gOpenNew) { ImGui::OpenPopup("New preset"); gOpenNew = false; }
		ImGui::SetNextWindowPos(DisplayCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(420.0f * s, 0.0f), ImGuiCond_Always);
		if (ImGui::BeginPopupModal("New preset", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
			ImGui::Text("New %s preset", fc.Name);
			ImGui::TextUnformatted("Name");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
			const bool enter = ImGui::InputText("##name", gNewName, sizeof(gNewName), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::TextUnformatted("Copy from");
			std::string cur = gNewFrom <= 0 || gNewFrom > (int)list.size() ? "(empty)" : list[gNewFrom - 1]->name;
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo("##from", cur.c_str())) {
				if (ImGui::Selectable("(empty)", gNewFrom == 0)) gNewFrom = 0;
				for (size_t i = 0; i < list.size(); i++) if (ImGui::Selectable(list[i]->name.c_str(), gNewFrom == (int)i + 1)) gNewFrom = (int)i + 1;
				ImGui::EndCombo();
			}
			const bool valid = ValidName(gNewName) && !cfg.PresetExists((Feature)gFeature, gNewName);
			if (*gNewName && !valid) Muted("Use letters, digits, space, _ - . and a name that is not taken.");
			ImGui::Spacing();
			const char* labels[2] = { "Create", "Cancel" };
			const bool enabled[2] = { valid, true };
			const int clicked = MenuWidgets::ToolbarButtons(labels, 2, enabled, 0);
			if ((clicked == 0 || (enter && valid))) {
				CreatePreset(cfg, gFeature, gNewName, gNewFrom > 0 ? list[gNewFrom - 1] : nullptr);
				ImGui::CloseCurrentPopup();
			}
			else if (clicked == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (gOpenDelete) { ImGui::OpenPopup("Delete preset"); gOpenDelete = false; }
		ImGui::SetNextWindowPos(DisplayCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(420.0f * s, 0.0f), ImGuiCond_Always);
		if (ImGui::BeginPopupModal("Delete preset", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
			ImGui::TextWrapped("Delete %s?", gSel.preset.c_str());
			Muted("The file is renamed to .nvrdeleted, not erased. Locations that name it fall back to their other presets.");
			ImGui::Spacing();
			const char* labels[2] = { "Delete", "Cancel" };
			const int clicked = MenuWidgets::ToolbarButtons(labels, 2, nullptr, 0);
			if (clicked == 0 || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
				auto it = fc.Presets.find(Lower(gSel.preset));
				if (it != fc.Presets.end()) { Preset copy = it->second; DeletePreset(cfg, gFeature, copy); }
				ImGui::CloseCurrentPopup();
			}
			else if (clicked == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	void DrawLeft(const LocationConfig& cfg) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const float pad = style.FramePadding.x + 2.0f;

		MenuWidgets::PageHeader("Preset types");
		DrawFeatureList(cfg);
		ImGui::Separator();

		const FeatureConfig& fc = cfg.Features[gFeature];
		if (!fc.Active()) {
			Muted(!fc.SwitchOn ? (std::string(fc.Name) + " is switched off in Settings.ini (" + fc.SwitchKey + ").").c_str()
				: (std::string(fc.IniName) + " is missing, so " + fc.Name + " does nothing.").c_str());
			DrawFileLink("Settings.ini", cfg.SettingsPath, -1, "", "");
			return;
		}

		DrawFileLink(fc.IniName, fc.IniPath, gFeature, "", "feature file");

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##filter", "Filter presets", gFilter, sizeof(gFilter));
		Tip("Type part of a preset name. [n] = place in this location's stack, later wins.");
		const std::string filter = Lower(gFilter);

		const Resolution& r = Data().LocStatus().Features[gFeature];
		std::vector<const Preset*> list;
		for (const auto& kv : fc.Presets) list.push_back(&kv.second);
		std::sort(list.begin(), list.end(), [](const Preset* a, const Preset* b) { return Lower(a->name) < Lower(b->name); });

		ImGui::BeginChild("##plist", ImVec2(0, -(ImGui::GetFrameHeight() + style.ItemSpacing.y)), ImGuiChildFlags_Borders);
		const float badgeW = MenuWidgets::BadgeWidth("default");
		for (const Preset* p : list) {
			const std::string pl = Lower(p->name);
			if (!filter.empty() && pl.find(filter) == std::string::npos) continue;
			int order = -1;
			for (size_t i = 0; i < r.stack.size(); i++) if (Lower(r.stack[i]) == pl) order = (int)i + 1;
			std::string text = p->name;
			if (order > 0) text = "[" + std::to_string(order) + "] " + text;
			auto it = gOpen.find(Lower(p->path));
			if (it != gOpen.end() && it->second.Dirty()) text += " *";
			const bool isDefault = !r.defaultPreset.empty() && Lower(r.defaultPreset) == pl;
			const bool sel = Lower(gSel.path) == Lower(p->path);

			ImGui::PushID(p->name.c_str());
			const float x0 = ImGui::GetCursorPosX();
			const float rowW = ImGui::GetContentRegionAvail().x;
			ImGui::SetNextItemAllowOverlap();
			if (ImGui::Selectable("##p", sel, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetFrameHeight()))) Select(p->path, p->name, gFeature, p->name);
			const ImVec2 after = ImGui::GetCursorPos();
			ImGui::SameLine(x0 + pad);
			ImGui::AlignTextToFramePadding();
			if (order > 0) ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Accent());
			if (MenuWidgets::Ellipsized(text, rowW - pad * 2.0f - (isDefault ? badgeW + style.ItemSpacing.x : 0.0f))) Tip(text);
			if (order > 0) ImGui::PopStyleColor();
			if (isDefault) {
				ImGui::SameLine(x0 + rowW - badgeW - pad);
				MenuWidgets::Badge("default");
			}
			ImGui::SetCursorPos(after);
			ImGui::PopID();
		}
		ImGui::EndChild();

		const bool havePreset = !gSel.preset.empty() && gSel.feature == gFeature;
		const char* labels[3] = { "New", "Duplicate", "Delete" };
		const bool enabled[3] = { true, havePreset, havePreset };
		switch (MenuWidgets::ToolbarButtons(labels, 3, enabled)) {
		case 0: gNewName[0] = 0; gNewFrom = 0; gOpenNew = true; break;
		case 1: {
			std::string name = gSel.preset + " copy";
			for (int n = 2; cfg.PresetExists((Feature)gFeature, name); n++) name = gSel.preset + " copy " + std::to_string(n);
			auto it = fc.Presets.find(Lower(gSel.preset));
			if (it != fc.Presets.end()) CreatePreset(cfg, gFeature, name, &it->second);
			break;
		}
		case 2: gOpenDelete = true; break;
		}
		DrawModals(cfg, list);
	}

	/* ------------------------------------------------------------ right pane: values */

	void DrawLiveCell(const IniDocument::Item& it) {
		const int type = Data().SettingType(it.section.c_str(), it.key.c_str());
		if (type < 0) {
			ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
			ImGui::TextUnformatted("-");
			ImGui::PopStyleColor();
			Tip("Not an NVR setting.");
			return;
		}
		const float live = Data().SettingF(it.section.c_str(), it.key.c_str());
		if (type == MenuNodeType::Boolean) ImGui::TextUnformatted(live != 0.0f ? "true" : "false");
		else if (type == MenuNodeType::Integer) ImGui::Text("%d", (int)live);
		else if (type == MenuNodeType::Float) ImGui::Text("%g", live);
		else Muted("text");
	}

	void DrawValues(OpenFile& f) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const float s = MenuBackend::Scale();
		const bool nvr = gSel.feature == FeatureNVR && !gSel.preset.empty();

		// Keys grouped by section, in file order.
		std::vector<IniDocument::Item> items = f.doc.Items();
		std::vector<std::string> order;
		std::map<std::string, std::vector<size_t>> bySection;
		for (size_t i = 0; i < items.size(); i++) {
			const std::string k = Lower(items[i].section);
			if (!bySection.count(k)) order.push_back(k);
			bySection[k].push_back(i);
		}
		for (const std::string& sec : f.doc.Sections()) {	// sections without keys still get a group
			if (!bySection.count(Lower(sec))) { bySection[Lower(sec)]; order.push_back(Lower(sec)); }
		}

		ImGui::BeginChild("##vals", ImVec2(0, 0), ImGuiChildFlags_None);
		if (order.empty()) Muted("The file has no keys yet. Add a section below.");

		std::set<std::string> seen;
		std::string removeSec, removeKey;
		bool doRemove = false;
		const float total = ImGui::GetContentRegionAvail().x;
		for (const std::string& lowSec : order) {
			const std::vector<size_t>& idx = bySection[lowSec];
			std::string secName = lowSec;
			for (const IniDocument::Item& it : items) if (Lower(it.section) == lowSec) { secName = it.section; break; }
			if (idx.empty()) for (const std::string& n : f.doc.Sections()) if (Lower(n) == lowSec) { secName = n; break; }

			ImGui::PushID(lowSec.c_str());
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			ImGui::PushFont(MenuBackend::FontSemiBold());
			const bool open = ImGui::CollapsingHeader((secName + "  (" + std::to_string(idx.size()) + ")###sec").c_str());
			ImGui::PopFont();
			if (open) {
				float keyW = 0.0f;
				for (size_t i : idx) keyW = std::max(keyW, ImGui::CalcTextSize(items[i].key.c_str()).x);
				keyW = std::min(std::max(keyW + style.CellPadding.x * 2.0f + 8.0f * s, total * 0.22f), total * 0.40f);
				const float liveW = 96.0f * s;
				if (!idx.empty() && ImGui::BeginTable("##ini", nvr ? 4 : 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
					ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, keyW);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
					if (nvr) ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthFixed, liveW);
					ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 4.0f);
					if (nvr) ImGui::TableHeadersRow();
					for (size_t i : idx) {
						const IniDocument::Item& it = items[i];
						const bool dup = !seen.insert(lowSec + "\n" + Lower(it.key)).second;
						const std::string* o = f.orig.Get(it.section, it.key);
						const bool changed = !dup && (!o || *o != it.value);
						ImGui::PushID((int)i);
						ImGui::TableNextRow();
						if (changed) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(MenuTheme::AccentDim()));
						ImGui::TableNextColumn();
						ImGui::AlignTextToFramePadding();
						CellText(it.key);
						ImGui::TableNextColumn();
						if (dup) {
							Muted(("ignored duplicate: " + it.value).c_str());
						} else {
							char buf[256];
							strncpy_s(buf, it.value.c_str(), _TRUNCATE);
							ImGui::SetNextItemWidth(-FLT_MIN);
							if (ImGui::InputText("##v", buf, sizeof(buf))) f.doc.Set(it.section, it.key, buf);
						}
						if (nvr) { ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); DrawLiveCell(it); }
						ImGui::TableNextColumn();
						if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) { doRemove = true; removeSec = it.section; removeKey = it.key; }
						Tip("Remove this key from the file.");
						ImGui::PopID();
					}
					ImGui::EndTable();
				}

				// Add key row for this section
				if (gAddKeyIn == lowSec) {
					const float avail = ImGui::GetContentRegionAvail().x;
					const float bw = ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2.0f;
					const float keyIn = std::max(80.0f * s, avail * 0.30f);
					ImGui::SetNextItemWidth(keyIn);
					ImGui::InputTextWithHint("##ak", "key", gAddKey, sizeof(gAddKey));
					ImGui::SameLine();
					ImGui::SetNextItemWidth(std::max(60.0f * s, avail - keyIn - bw * 2.0f - style.ItemSpacing.x * 3.0f));
					const bool enter = ImGui::InputTextWithHint("##av", "value", gAddValue, sizeof(gAddValue), ImGuiInputTextFlags_EnterReturnsTrue);
					ImGui::SameLine();
					const bool canAdd = gAddKey[0] != 0;
					ImGui::BeginDisabled(!canAdd);
					if (ImGui::Button("Add", ImVec2(bw, 0)) || (enter && canAdd)) { f.doc.Set(secName, gAddKey, gAddValue); gAddKey[0] = gAddValue[0] = 0; }
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ImGui::Button("Cancel", ImVec2(bw, 0))) gAddKeyIn.clear();
				}
				else if (ImGui::SmallButton("Add key...")) { gAddKeyIn = lowSec; gAddKey[0] = gAddValue[0] = 0; gAddSectionOpen = false; }
				ImGui::Spacing();
			}
			ImGui::PopID();
		}
		if (doRemove) f.doc.Remove(removeSec, removeKey);

		// Add section row
		ImGui::Separator();
		if (!gAddSectionOpen) {
			if (ImGui::Button("Add section...")) { gAddSectionOpen = true; gAddKeyIn.clear(); gAddSection[0] = gAddKey[0] = gAddValue[0] = 0; }
		}
		else {
			const float avail = ImGui::GetContentRegionAvail().x;
			const float bw = ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2.0f;
			const float w3 = std::max(60.0f * s, (avail - bw * 2.0f - style.ItemSpacing.x * 4.0f) / 3.0f);
			ImGui::SetNextItemWidth(w3);
			ImGui::InputTextWithHint("##as", "section", gAddSection, sizeof(gAddSection));
			ImGui::SameLine(); ImGui::SetNextItemWidth(w3);
			ImGui::InputTextWithHint("##ak2", "key", gAddKey, sizeof(gAddKey));
			ImGui::SameLine(); ImGui::SetNextItemWidth(w3);
			ImGui::InputTextWithHint("##av2", "value", gAddValue, sizeof(gAddValue));
			ImGui::SameLine();
			const bool canAdd = gAddSection[0] && gAddKey[0];
			ImGui::BeginDisabled(!canAdd);
			if (ImGui::Button("Add", ImVec2(bw, 0))) { f.doc.Set(gAddSection, gAddKey, gAddValue); gAddKey[0] = gAddValue[0] = 0; gAddSectionOpen = false; }
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(bw, 0))) gAddSectionOpen = false;
		}
		ImGui::EndChild();
	}

	/* ------------------------------------------------------------ right pane: used by */

	void DrawUsedBy(const LocationConfig& cfg) {
		const LocationStatus& st = Data().LocStatus();
		const float s = MenuBackend::Scale();
		const int f = gSel.feature;
		const FeatureConfig& fc = cfg.Features[f];

		ImGui::BeginChild("##used", ImVec2(0, 0), ImGuiChildFlags_None);

		// defaults
		const char* defKeys[2] = { nullptr, nullptr };
		if (f == FeatureNVR || f == FeatureGameSettings) { defKeys[0] = "Exterior"; defKeys[1] = "Interior"; } else defKeys[0] = "DefaultPreset";
		bool anyDefault = false;
		for (const char* dk : defKeys) {
			if (!dk) continue;
			const std::string* v = fc.Ini.Get("Main", dk);
			if (v && Lower(*v) == Lower(gSel.preset)) {
				std::string what = !strcmp(dk, "Exterior") ? "all exteriors" : !strcmp(dk, "Interior") ? "all interiors" : "everything";
				ImGui::TextColored(MenuTheme::Accent(), "Default for %s", what.c_str());
				anyDefault = true;
			}
		}
		if (!anyDefault) Muted("Not a default preset.");

		if (!st.Target.empty()) {
			const std::string label = "Add to current location (" + st.Target + ")";
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) AddToCurrentLocation(cfg, f, gSel.preset);
			Tip("Adds " + gSel.preset + " to [Overrides] " + st.Target + " in " + fc.IniName + " (then Save that file).");
		}

		// One row per editor ID (a cell can match through an override and a keyword): the "why"s are joined.
		struct Row { std::string id; std::string why; const MenuInstalled* info; };
		std::vector<Row> rows;
		{
			std::map<std::string, size_t> byId;
			for (const PresetUse& u : cfg.FindPresetUses((Feature)f, gSel.preset)) {
				std::string why = u.kind == PresetUse::Override ? "override in " + u.via : u.kind == PresetUse::Keyword ? "keyword in " + u.via : "alias " + u.via;
				auto it = byId.find(Lower(u.editorId));
				if (it != byId.end()) { rows[it->second].why += "; " + why; continue; }
				byId[Lower(u.editorId)] = rows.size();
				rows.push_back({ u.editorId, why, Data().FindInstalled(u.editorId) });
			}
		}
		size_t installed = 0;
		for (const Row& r : rows) if (r.info) installed++;

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##usefilter", "Filter by editor ID", gUseFilter, sizeof(gUseFilter));
		ImGui::Checkbox("Show not installed", &gShowMissing);
		ImGui::SameLine();
		MenuWidgets::MutedF("%zu locations, %zu installed", rows.size(), installed);
		if (!Data().CanGoTo()) Muted("Go is off: the NVSE console interface is missing.");
		const std::string filter = Lower(gUseFilter);

		const std::string go = Data().GoStatus();
		const float footer = go.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing();
		if (rows.empty()) Muted("No location uses this preset through [Overrides], KEYWORDS or aliases.");
		else if (ImGui::BeginTable("##uses", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX, ImVec2(0, -footer))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Editor ID", ImGuiTableColumnFlags_WidthFixed, 190.0f * s);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f * s);
			ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthFixed, 150.0f * s);
			ImGui::TableSetupColumn("Why", ImGuiTableColumnFlags_WidthFixed, 260.0f * s);
			ImGui::TableSetupColumn("##go", ImGuiTableColumnFlags_WidthFixed, 56.0f * s);
			ImGui::TableHeadersRow();
			int n = 0;
			for (const Row& r : rows) {
				if (!r.info && !gShowMissing) continue;
				if (!filter.empty() && Lower(r.id).find(filter) == std::string::npos) continue;
				ImGui::PushID(n++);
				ImGui::TableNextRow();
				if (!r.info) ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
				ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); CellText(r.id);
				ImGui::TableNextColumn(); CellText(!r.info ? "Not installed" : r.info->worldspace ? "Worldspace" : "Interior");
				ImGui::TableNextColumn(); CellText(r.info ? r.info->plugin : "");
				ImGui::TableNextColumn(); CellText(r.why);
				if (!r.info) ImGui::PopStyleColor();
				ImGui::TableNextColumn();
				const bool canGo = r.info && Data().CanGoTo();
				ImGui::BeginDisabled(!canGo);
				if (ImGui::SmallButton("Go")) {
					if (Data().RequestGoTo(r.id)) Data().CloseMenu();
				}
				ImGui::EndDisabled();
				Tip(r.info ? "Close the menu and travel there (coc, or cow for a worldspace)." : "No cell or worldspace with this editor ID in your load order");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (!go.empty()) Muted(go.c_str());
		ImGui::EndChild();
	}

	/* ------------------------------------------------------------ right pane */

	void DrawRight(const LocationConfig& cfg) {
		if (gSel.path.empty()) { Muted("Pick a preset or a file on the left."); return; }
		OpenFile& f = Open(gSel.path);
		const ImGuiStyle& style = ImGui::GetStyle();
		const float s = MenuBackend::Scale();
		const bool dirty = f.Dirty();

		// Title line: name on the left, unsaved marker + Save / Revert on the right.
		const float btnW = std::max(ImGui::CalcTextSize("Revert").x, ImGui::CalcTextSize("Save").x) + style.FramePadding.x * 2.0f + 20.0f * s;
		const char* unsavedText = "Unsaved changes";
		const float reserve = btnW * 2.0f + style.ItemSpacing.x + (dirty ? ImGui::CalcTextSize(unsavedText).x + style.ItemSpacing.x : 0.0f) + style.ItemSpacing.x * 2.0f;
		const float total = ImGui::GetContentRegionMax().x;
		ImGui::AlignTextToFramePadding();
		ImGui::PushFont(MenuBackend::FontSemiBold());
		MenuWidgets::Ellipsized(gSel.label, std::max(60.0f * s, ImGui::GetContentRegionAvail().x - reserve));
		ImGui::PopFont();
		ImGui::SameLine(total - (reserve - style.ItemSpacing.x * 2.0f));
		if (dirty) { ImGui::TextColored(MenuTheme::Warning(), "%s", unsavedText); ImGui::SameLine(); }
		ImGui::BeginDisabled(!dirty);
		ImGui::PushStyleColor(ImGuiCol_Button, MenuTheme::AccentDim());
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, MenuTheme::Accent());
		if (ImGui::Button("Save", ImVec2(btnW, 0))) SaveFile(f);
		ImGui::PopStyleColor(2);
		Tip("Write this file, then reload the presets so the change shows in the current location.");
		ImGui::SameLine();
		if (ImGui::Button("Revert", ImVec2(btnW, 0))) RevertFile(f);
		Tip("Throw away unsaved edits to this file.");
		ImGui::EndDisabled();

		ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
		MenuWidgets::Ellipsized(f.path);
		ImGui::PopStyleColor();
		Tip(f.path + "\n\nEdits go to the mod that provides the file; new files go to MO2's overwrite folder.");
		if (f.missing) ImGui::TextColored(MenuTheme::Warning(), "The file does not exist (Save creates it).");
		MenuWidgets::StatusLine(gStatus, gStatusError);

		const bool havePreset = !gSel.preset.empty() && gSel.feature >= 0;
		if (!havePreset) gSubTab = 0;
		ImGui::Spacing();
		if (MenuWidgets::TabButton("Values", gSubTab == 0)) gSubTab = 0;
		if (havePreset) {
			ImGui::SameLine();
			if (MenuWidgets::TabButton("Used by", gSubTab == 1)) gSubTab = 1;
		}
		ImGui::Spacing();

		if (gSubTab == 1 && havePreset) DrawUsedBy(cfg);
		else DrawValues(f);
	}
}

namespace PresetsPage {

bool SelectPreset(int feature, const char* name) {
	const LocationConfig& cfg = Data().LocConfig();
	if (feature < 0 || feature >= FeatureCount) return false;
	auto it = cfg.Features[feature].Presets.find(Lower(name));
	if (it == cfg.Features[feature].Presets.end()) return false;
	gFeature = feature;
	Select(it->second.path, it->second.name, feature, it->second.name);
	return true;
}

void ShowSubTab(int tab) {
	gSubTab = tab;
}

void OpenFile(const std::string& path, const std::string& label, int feature, const std::string& preset) {
	if (feature >= 0) gFeature = feature;
	Select(path, label, feature, preset);
	gSubTab = 0;
}

int UnsavedCount() {
	int n = 0;
	for (auto& kv : gOpen) if (kv.second.Dirty()) n++;
	return n;
}

void Draw() {
	const LocationStatus& st = Data().LocStatus();
	if (!st.Enabled) {
		ImGui::TextWrapped("%s", st.Warning.empty() ? "Locations is off (Main.Main.Locations Enabled), so there are no presets to manage." : st.Warning.c_str());
		return;
	}
	const LocationConfig& cfg = Data().LocConfig();
	const float s = MenuBackend::Scale();
	const ImGuiStyle& style = ImGui::GetStyle();

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float maxLeft = std::max(160.0f, std::min(520.0f * s, avail.x * 0.55f));
	const float minLeft = std::min(220.0f * s, maxLeft);
	float leftW = std::max(minLeft, std::min(maxLeft, gLeftUnits * s));

	// Both panes use the same roomy padding.
	auto pane = [&](const char* id, float width, void (*draw)(const LocationConfig&)) {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 12.0f * s));
		ImGui::BeginChild(id, ImVec2(width, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 8.0f * s));
		draw(cfg);
		ImGui::PopStyleVar();
		ImGui::EndChild();
	};
	pane("##pl", leftW, DrawLeft);
	if (MenuWidgets::SplitterV("##plsplit", &leftW, minLeft, maxLeft, avail.y)) gLeftUnits = leftW / s;
	pane("##pr", 0.0f, DrawRight);
	(void)style;
}

}
