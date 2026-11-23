#include "PresetsPage.h"
#include "MenuBackend.h"
#include "MenuTheme.h"
#include "../Locations/LocationApply.h"
#include "../Locations/IniDocument.h"
#include "../GameMenuManager.h"
#include "../../../lib/imgui/imgui.h"
#include <map>
#include <set>
#include <algorithm>

// The game headers define min/max macros.
#undef min
#undef max

using namespace Locations;

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

	void Tip(const char* text) {
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
	}

	void Muted(const char* text) {
		ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}

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
		TheLocationManager->Reload();
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
		const LocationStatus& st = TheLocationManager->GetStatus();
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

	/* ------------------------------------------------------------ panes */

	void DrawFileLink(const std::string& label, const std::string& path, int feature, const std::string& preset, const char* tag) {
		const bool sel = Lower(gSel.path) == Lower(path);
		std::string text = label;
		if (tag && *tag) text += std::string("  (") + tag + ")";
		auto it = gOpen.find(Lower(path));
		if (it != gOpen.end() && it->second.Dirty()) text += "  *";
		if (ImGui::Selectable((text + "##f" + path).c_str(), sel)) Select(path, label, feature, preset);
	}

	void DrawLeft(const LocationConfig& cfg) {
		const LocationStatus& st = TheLocationManager->GetStatus();

		if (!st.Target.empty() && ImGui::CollapsingHeader("Files in use here")) {
			Muted(("Everything that decides the presets of " + st.Target).c_str());
			DrawFileLink("Settings.ini", cfg.SettingsPath, -1, "", "");
			for (int f = 0; f < FeatureCount; f++) {
				const FeatureConfig& fc = cfg.Features[f];
				const Resolution& r = st.Features[f];
				if (!fc.Active() || !r.valid) continue;
				DrawFileLink(fc.IniName, fc.IniPath, f, "", fc.Name);
				for (size_t i = 0; i < r.stack.size(); i++) {
					auto p = fc.Presets.find(Lower(r.stack[i]));
					if (p == fc.Presets.end()) continue;
					DrawFileLink(std::string(fc.PresetFolder) + "\\" + FileName(p->second.path), p->second.path, f, p->second.name, i == 0 ? "default" : "");
				}
			}
			std::vector<std::string> kf = cfg.KeywordFilesFor(st.Target);
			for (const std::string& k : kf) {
				ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
				ImGui::Text("KEYWORDS\\%s  (read only)", k.c_str());
				ImGui::PopStyleColor();
			}
			ImGui::Spacing();
		}

		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo("##feature", cfg.Features[gFeature].Name)) {
			for (int f = 0; f < FeatureCount; f++) {
				const bool act = cfg.Features[f].Active();
				if (!act) ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
				if (ImGui::Selectable(cfg.Features[f].Name, gFeature == f)) gFeature = f;
				if (!act) ImGui::PopStyleColor();
			}
			ImGui::EndCombo();
		}
		Tip("Which kind of preset to list.");

		const FeatureConfig& fc = cfg.Features[gFeature];
		if (!fc.Active()) {
			Muted(!fc.SwitchOn ? (std::string(fc.Name) + " is switched off in Settings.ini (" + fc.SwitchKey + ").").c_str()
				: (std::string(fc.IniName) + " is missing, so " + fc.Name + " does nothing.").c_str());
			DrawFileLink("Settings.ini", cfg.SettingsPath, -1, "", "");
			return;
		}

		DrawFileLink(fc.IniName, fc.IniPath, gFeature, "", "feature file");

		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##filter", "Filter presets", gFilter, sizeof(gFilter));
		Tip("Type part of a preset name. [n] = place in this location's stack, later wins.");
		std::string filter = Lower(gFilter);

		const Resolution& r = st.Features[gFeature];
		std::vector<const Preset*> list;
		for (const auto& kv : fc.Presets) list.push_back(&kv.second);
		std::sort(list.begin(), list.end(), [](const Preset* a, const Preset* b) { return Lower(a->name) < Lower(b->name); });

		const float footer = ImGui::GetFrameHeightWithSpacing();
		ImGui::BeginChild("##plist", ImVec2(0, -footer), ImGuiChildFlags_Borders);
		for (const Preset* p : list) {
			std::string pl = Lower(p->name);
			if (!filter.empty() && pl.find(filter) == std::string::npos) continue;
			std::string text = p->name;
			int order = -1;
			for (size_t i = 0; i < r.stack.size(); i++) if (Lower(r.stack[i]) == pl) order = (int)i + 1;
			if (order > 0) text = "[" + std::to_string(order) + "] " + text;
			auto it = gOpen.find(Lower(p->path));
			if (it != gOpen.end() && it->second.Dirty()) text += " *";
			const bool sel = Lower(gSel.path) == Lower(p->path);
			if (order > 0) ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Accent());
			if (ImGui::Selectable((text + "##p" + p->name).c_str(), sel)) Select(p->path, p->name, gFeature, p->name);
			if (order > 0) ImGui::PopStyleColor();
			if (!r.defaultPreset.empty() && Lower(r.defaultPreset) == pl) { ImGui::SameLine(); Muted("default"); }
		}
		ImGui::EndChild();

		if (ImGui::Button("New", ImVec2(Btn(), 0))) { gNewName[0] = 0; gNewFrom = 0; gOpenNew = true; }
		Tip("Create a new preset, empty or as a copy of an existing one.");
		ImGui::SameLine();
		const bool havePreset = !gSel.preset.empty() && gSel.feature == gFeature;
		if (!havePreset) ImGui::BeginDisabled();
		if (ImGui::Button("Duplicate", ImVec2(Btn(), 0))) {
			std::string name = gSel.preset + " copy";
			for (int n = 2; cfg.PresetExists((Feature)gFeature, name); n++) name = gSel.preset + " copy " + std::to_string(n);
			auto it = fc.Presets.find(Lower(gSel.preset));
			if (it != fc.Presets.end()) CreatePreset(cfg, gFeature, name, &it->second);
		}
		Tip("Copy the selected preset under a new name.");
		ImGui::SameLine();
		if (ImGui::Button("Delete", ImVec2(Btn(), 0))) gOpenDelete = true;
		Tip("Rename the selected preset's file to .nvrdeleted (asks first).");
		if (!havePreset) ImGui::EndDisabled();

		if (gOpenNew) { ImGui::OpenPopup("New preset"); gOpenNew = false; }
		if (ImGui::BeginPopupModal("New preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("New %s preset", fc.Name);
			ImGui::InputText("Name", gNewName, sizeof(gNewName));
			std::vector<const Preset*> froms = list;
			std::string cur = gNewFrom <= 0 || gNewFrom > (int)froms.size() ? "(empty)" : froms[gNewFrom - 1]->name;
			if (ImGui::BeginCombo("Copy from", cur.c_str())) {
				if (ImGui::Selectable("(empty)", gNewFrom == 0)) gNewFrom = 0;
				for (size_t i = 0; i < froms.size(); i++) if (ImGui::Selectable(froms[i]->name.c_str(), gNewFrom == (int)i + 1)) gNewFrom = (int)i + 1;
				ImGui::EndCombo();
			}
			const bool valid = ValidName(gNewName) && !cfg.PresetExists((Feature)gFeature, gNewName);
			if (*gNewName && !valid) Muted("Use letters, digits, space, _ - . and a name that is not taken.");
			if (!valid) ImGui::BeginDisabled();
			if (ImGui::Button("Create", ImVec2(Btn(), 0))) {
				CreatePreset(cfg, gFeature, gNewName, gNewFrom > 0 ? froms[gNewFrom - 1] : nullptr);
				ImGui::CloseCurrentPopup();
			}
			if (!valid) ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(Btn(), 0))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (gOpenDelete) { ImGui::OpenPopup("Delete preset"); gOpenDelete = false; }
		if (ImGui::BeginPopupModal("Delete preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Delete %s?", gSel.preset.c_str());
			Muted("The file is renamed to .nvrdeleted, not erased. Locations that name it fall back to their other presets.");
			if (ImGui::Button("Delete", ImVec2(Btn(), 0))) {
				auto it = fc.Presets.find(Lower(gSel.preset));
				if (it != fc.Presets.end()) { Preset copy = it->second; DeletePreset(cfg, gFeature, copy); }
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(Btn(), 0))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	void DrawMiddle(const LocationConfig& cfg) {
		if (gSel.path.empty()) { Muted("Pick a preset or a file on the left."); return; }
		OpenFile& f = Open(gSel.path);

		ImGui::PushFont(MenuBackend::FontSemiBold());
		ImGui::TextUnformatted(gSel.label.c_str());
		ImGui::PopFont();
		Muted(f.path.c_str());
		Tip("Edits go to the mod that provides the file; new files go to MO2's overwrite folder.");
		if (f.missing) ImGui::TextColored(MenuTheme::Warning(), "The file does not exist (Save creates it).");

		const bool dirty = f.Dirty();
		if (!dirty) ImGui::BeginDisabled();
		ImGui::PushStyleColor(ImGuiCol_Button, MenuTheme::AccentDim());
		if (ImGui::Button("Save", ImVec2(Btn(), 0))) SaveFile(f);
		ImGui::PopStyleColor();
		Tip("Write this file, then reload the presets so the change shows in the current location.");
		ImGui::SameLine();
		if (ImGui::Button("Revert", ImVec2(Btn(), 0))) RevertFile(f);
		Tip("Throw away unsaved edits to this file.");
		if (!dirty) ImGui::EndDisabled();
		if (dirty) { ImGui::SameLine(); ImGui::TextColored(MenuTheme::Warning(), "Unsaved changes"); }
		if (!gStatus.empty()) {
			if (gStatusError) ImGui::TextColored(MenuTheme::Warning(), "%s", gStatus.c_str()); else Muted(gStatus.c_str());
		}

		const bool nvr = gSel.feature == FeatureNVR && !gSel.preset.empty();
		const int cols = nvr ? 4 : 3;
		ImGui::BeginChild("##rows", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * (gAddOpen ? 2.0f : 1.0f)), ImGuiChildFlags_Borders);
		if (ImGui::BeginTable("##ini", cols + 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			if (nvr) ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
			ImGui::TableHeadersRow();
			std::vector<IniDocument::Item> items = f.doc.Items();
			std::set<std::string> seen;
			int row = 0;
			std::string removeSec, removeKey, lastSec; bool doRemove = false;
			for (const IniDocument::Item& it : items) {
				const bool dup = !seen.insert(Lower(it.section) + "\n" + Lower(it.key)).second;
				const std::string* o = f.orig.Get(it.section, it.key);
				const bool changed = !dup && (!o || *o != it.value);
				ImGui::PushID(row++);
				ImGui::TableNextRow();
				if (changed) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(MenuTheme::AccentDim()));
				ImGui::TableNextColumn();
				if (Lower(it.section) != lastSec) { ImGui::TextColored(MenuTheme::Accent(), "%s", it.section.c_str()); lastSec = Lower(it.section); }
				ImGui::TableNextColumn(); ImGui::TextUnformatted(it.key.c_str());
				ImGui::TableNextColumn();
				if (dup) {
					Muted(("ignored duplicate: " + it.value).c_str());
				} else {
					char buf[256];
					strncpy_s(buf, it.value.c_str(), _TRUNCATE);
					ImGui::SetNextItemWidth(-1);
					if (ImGui::InputText("##v", buf, sizeof(buf))) f.doc.Set(it.section, it.key, buf);
				}
				if (nvr) {
					ImGui::TableNextColumn();
					int type = TheSettingManager->GetSettingType(it.section.c_str(), it.key.c_str());
					if (type < 0) { Muted("not an NVR setting"); }
					else {
						float live = TheSettingManager->GetSettingF(it.section.c_str(), it.key.c_str());
						if (type == SettingManager::Configuration::NodeType::Boolean) ImGui::TextUnformatted(live != 0.0f ? "true" : "false");
						else if (type == SettingManager::Configuration::NodeType::Integer) ImGui::Text("%d", (int)live);
						else if (type == SettingManager::Configuration::NodeType::Float) ImGui::Text("%g", live);
						else Muted("text");
					}
				}
				ImGui::TableNextColumn();
				if (ImGui::SmallButton("x")) { doRemove = true; removeSec = it.section; removeKey = it.key; }
				Tip("Remove this key from the file.");
				ImGui::PopID();
			}
			ImGui::EndTable();
			if (doRemove) f.doc.Remove(removeSec, removeKey);
		}
		ImGui::EndChild();

		if (ImGui::Button(gAddOpen ? "Hide add key" : "Add key...", ImVec2(Btn() * 1.3f, 0))) gAddOpen = !gAddOpen;
		if (gAddOpen) {
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
			ImGui::InputTextWithHint("##as", "section", gAddSection, sizeof(gAddSection));
			ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
			ImGui::InputTextWithHint("##ak", "key", gAddKey, sizeof(gAddKey));
			ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
			ImGui::InputTextWithHint("##av", "value", gAddValue, sizeof(gAddValue));
			ImGui::SameLine();
			const bool canAdd = gAddSection[0] && gAddKey[0];
			if (!canAdd) ImGui::BeginDisabled();
			if (ImGui::Button("Add", ImVec2(Btn(), 0))) { f.doc.Set(gAddSection, gAddKey, gAddValue); gAddKey[0] = gAddValue[0] = 0; }
			if (!canAdd) ImGui::EndDisabled();
		}
		(void)cfg;
	}

	void DrawRight(const LocationConfig& cfg) {
		const LocationStatus& st = TheLocationManager->GetStatus();
		ImGui::PushFont(MenuBackend::FontSemiBold());
		ImGui::TextUnformatted("Where it is used");
		ImGui::PopFont();

		if (gSel.preset.empty() || gSel.feature < 0) {
			Muted("Select a preset to see the locations that use it.");
			return;
		}
		const int f = gSel.feature;
		const FeatureConfig& fc = cfg.Features[f];

		// defaults
		const char* defKeys[2] = { nullptr, nullptr };
		if (f == FeatureNVR || f == FeatureGameSettings) { defKeys[0] = "Exterior"; defKeys[1] = "Interior"; } else defKeys[0] = "DefaultPreset";
		for (const char* dk : defKeys) {
			if (!dk) continue;
			const std::string* v = fc.Ini.Get("Main", dk);
			if (v && Lower(*v) == Lower(gSel.preset)) {
				std::string what = !strcmp(dk, "Exterior") ? "all exteriors" : !strcmp(dk, "Interior") ? "all interiors" : "everything";
				ImGui::TextColored(MenuTheme::Accent(), "Default for %s", what.c_str());
			}
		}

		if (!st.Target.empty()) {
			if (ImGui::Button("Add to current location", ImVec2(-1, 0))) AddToCurrentLocation(cfg, f, gSel.preset);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds %s to [Overrides] %s in %s (then Save that file).", gSel.preset.c_str(), st.Target.c_str(), fc.IniName);
		}

		std::vector<PresetUse> uses = cfg.FindPresetUses((Feature)f, gSel.preset);
		ImGui::Text("%zu location(s)", uses.size());
		if (!LocationManager::CanGoTo()) Muted("Go is off: the NVSE console interface is missing.");
		if (!uses.empty() && ImGui::BeginTable("##uses", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Editor ID", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Why", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("##go", ImGuiTableColumnFlags_WidthFixed, 52.0f * MenuBackend::Scale());
			ImGui::TableHeadersRow();
			int row = 0;
			for (const PresetUse& u : uses) {
				ImGui::PushID(row++);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(u.editorId.c_str());
				ImGui::TableNextColumn();
				std::string why = u.kind == PresetUse::Override ? "Override in " + u.via : u.kind == PresetUse::Keyword ? "keyword in " + u.via : "alias " + u.via;
				ImGui::TextUnformatted(why.c_str());
				ImGui::TableNextColumn();
				if (!LocationManager::CanGoTo()) ImGui::BeginDisabled();
				if (ImGui::SmallButton("Go")) {
					if (TheLocationManager->RequestGoTo(u.editorId)) TheGameMenuManager->Enabled = false;
				}
				Tip("Close the menu and travel there (coc, or cow for a worldspace).");
				if (!LocationManager::CanGoTo()) ImGui::EndDisabled();
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		const std::string& go = TheLocationManager->GoStatus();
		if (!go.empty()) Muted(go.c_str());
	}
}

namespace PresetsPage {

int UnsavedCount() {
	int n = 0;
	for (auto& kv : gOpen) if (kv.second.Dirty()) n++;
	return n;
}

void Draw() {
	ImGui::PushFont(MenuBackend::FontSemiBold());
	ImGui::TextUnformatted("Presets");
	ImGui::PopFont();

	const LocationStatus& st = TheLocationManager->GetStatus();
	if (!st.Enabled) {
		ImGui::Spacing();
		ImGui::TextWrapped("%s", st.Warning.empty() ? "Locations is off (Main.Main.Locations Enabled), so there are no presets to manage." : st.Warning.c_str());
		return;
	}
	const LocationConfig& cfg = TheLocationManager->GetConfig();
	const int unsaved = UnsavedCount();
	if (unsaved) {
		ImGui::SameLine();
		ImGui::TextColored(MenuTheme::Warning(), "  %d file(s) with unsaved edits (kept while the game runs)", unsaved);
	}
	ImGui::Spacing();

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float w = avail.x - ImGui::GetStyle().ItemSpacing.x * 2.0f;
	// Roomier panes: inner padding on the three columns and more air between rows.
	const float sc = MenuBackend::Scale();
	auto pane = [&](const char* id, float width, void (*draw)(const LocationConfig&)) {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * sc, 14.0f * sc));
		ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * sc, 9.0f * sc));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * sc, 5.0f * sc));
		draw(cfg);
		ImGui::PopStyleVar(2);
		ImGui::EndChild();
	};
	pane("##pl", w * 0.24f, DrawLeft);
	ImGui::SameLine();
	pane("##pm", w * 0.48f, DrawMiddle);
	ImGui::SameLine();
	pane("##pr", 0.0f, DrawRight);
}

}
