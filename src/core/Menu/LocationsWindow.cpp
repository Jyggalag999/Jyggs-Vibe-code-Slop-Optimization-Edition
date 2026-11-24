#include "LocationsWindow.h"
#include "MenuBackend.h"
#include "MenuTheme.h"
#include "MenuData.h"
#include "MenuWidgets.h"
#include "PresetsPage.h"
#include "../../../lib/imgui/imgui.h"
#include <algorithm>

// The game headers define min/max macros.
#undef min
#undef max

namespace {
	using MenuWidgets::Tip;
	using MenuWidgets::Muted;

	int		gTab = 0;			// 0 = Presets, 1 = Current location
	bool	gShowAll = false;

	std::string FileName(const std::string& p) {
		size_t s = p.find_last_of("\\/");
		return s == std::string::npos ? p : p.substr(s + 1);
	}

	void CellText(const std::string& text) {
		if (MenuWidgets::Ellipsized(text)) Tip(text);
	}

	// Row in the "Files in use here" list: name on the left, Open on the right (jumps to the Presets tab).
	void FileRow(const std::string& label, const std::string& path, int feature, const std::string& preset, const char* tag, int& n) {
		ImGui::PushID(n++);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		CellText(tag && *tag ? label + "  (" + tag + ")" : label);
		ImGui::TableNextColumn();
		const bool canOpen = Data().PresetEditor();
		ImGui::BeginDisabled(!canOpen);
		if (ImGui::SmallButton("Open")) {
			PresetsPage::OpenFile(path, label, feature, preset);
			gTab = 0;
		}
		ImGui::EndDisabled();
		Tip(canOpen ? "Open this file in the Presets tab." : "The preset editor is off (Locations PresetEditor).");
		ImGui::PopID();
	}

	// The "Current location" tab: target, keywords, the files that decide it and what each feature resolved to.
	void DrawCurrentLocation() {
		const LocationStatus& st = Data().LocStatus();
		const Locations::LocationConfig& cfg = Data().LocConfig();
		const float s = MenuBackend::Scale();

		ImGui::BeginChild("##curscroll", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
		if (st.Target.empty()) {
			Muted(st.Enabled ? "No location picked yet." : "Locations is off.");
			ImGui::EndChild();
			return;
		}

		// Summary card
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 10.0f * s));
		ImGui::BeginChild("##card", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
		MenuWidgets::MutedF("%s", st.Interior ? "Interior cell" : "Worldspace");
		ImGui::PushFont(MenuBackend::FontSemiBold());
		ImGui::TextColored(MenuTheme::Accent(), "%s", st.Target.c_str());
		ImGui::PopFont();
		std::string kw;
		for (const std::string& k : st.Keywords) kw += (kw.empty() ? "" : ", ") + k;
		ImGui::PushTextWrapPos(0.0f);
		ImGui::Text("Keywords: %s", kw.empty() ? "none" : kw.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
		ImGui::Spacing();

		// Files in use here
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Files in use here")) {
			Muted(("Everything that decides the presets of " + st.Target).c_str());
			if (ImGui::BeginTable("##files", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
				ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("##open", ImGuiTableColumnFlags_WidthFixed, 64.0f * s);
				int n = 0;
				FileRow("Settings.ini", cfg.SettingsPath, -1, "", "", n);
				for (int f = 0; f < Locations::FeatureCount; f++) {
					const Locations::FeatureConfig& fc = cfg.Features[f];
					const Locations::Resolution& r = st.Features[f];
					if (!fc.Active() || !r.valid) continue;
					FileRow(fc.IniName, fc.IniPath, f, "", fc.Name, n);
					for (size_t i = 0; i < r.stack.size(); i++) {
						auto p = fc.Presets.find(Locations::Lower(r.stack[i]));
						if (p == fc.Presets.end()) continue;
						FileRow(std::string(fc.PresetFolder) + "\\" + FileName(p->second.path), p->second.path, f, p->second.name, i == 0 ? "default" : "", n);
					}
				}
				for (const std::string& k : cfg.KeywordFilesFor(st.Target)) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
					CellText("KEYWORDS\\" + k + "  (read only)");
					ImGui::PopStyleColor();
					ImGui::TableNextColumn();
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();
		}

		ImGui::Checkbox("Show values from the default preset too", &gShowAll);

		for (int f = 0; f < Locations::FeatureCount; f++) {
			const Locations::Resolution& r = st.Features[f];
			const char* name = Data().FeatureName(f);
			ImGui::PushID(f);
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			ImGui::PushFont(MenuBackend::FontSemiBold());
			const bool open = ImGui::CollapsingHeader(name);
			ImGui::PopFont();
			if (open) {
				if (!r.valid) {
					const std::string& why = (f == Locations::FeatureLighting && !st.LightingNote.empty()) ? st.LightingNote : r.reason;
					Muted(("Not used: " + (why.empty() ? std::string("nothing to do") : why)).c_str());
				}
				else {
					std::string stack;
					for (const std::string& p : r.stack) stack += (stack.empty() ? "" : " > ") + p;
					ImGui::PushTextWrapPos(0.0f);
					ImGui::Text("Presets (later wins): %s", stack.c_str());
					ImGui::PopTextWrapPos();
					if (!st.Applied[f]) Muted("Same presets as before, nothing was written.");
					if (ImGui::BeginTable("##loc", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
						ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 2.2f);
						ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.2f);
						ImGui::TableSetupColumn("From preset", ImGuiTableColumnFlags_WidthStretch, 1.6f);
						ImGui::TableHeadersRow();
						int shown = 0;
						for (const Locations::ResolvedValue& v : r.values) {
							if (!gShowAll && v.source == r.defaultPreset) continue;
							shown++;
							ImGui::TableNextRow();
							ImGui::TableNextColumn(); CellText(v.section + " : " + v.key);
							ImGui::TableNextColumn(); CellText(v.value);
							ImGui::TableNextColumn(); CellText(v.source);
						}
						if (!shown) {
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
							ImGui::TextUnformatted("Only the default preset's values apply here.");
							ImGui::PopStyleColor();
						}
						ImGui::EndTable();
					}
				}
				ImGui::Spacing();
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	// Fixed bar: title, tabs, unsaved marker and Reload.
	void DrawHeaderBar(bool presetsTab) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const float s = MenuBackend::Scale();
		const float barH = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
		ImGui::BeginChild("##lochdr", ImVec2(0, barH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGui::AlignTextToFramePadding();
		ImGui::PushFont(MenuBackend::FontSemiBold());
		ImGui::TextUnformatted("Locations");
		ImGui::PopFont();
		ImGui::SameLine(0.0f, 28.0f * s);
		if (presetsTab) {
			if (MenuWidgets::TabButton("Presets", gTab == 0)) gTab = 0;
			ImGui::SameLine();
		}
		if (MenuWidgets::TabButton("Current location", gTab == 1)) gTab = 1;

		const int unsaved = PresetsPage::UnsavedCount();
		const float bw = ImGui::CalcTextSize("Reload").x + style.FramePadding.x * 2.0f + 24.0f * s;
		char text[64] = {};
		if (unsaved) snprintf(text, sizeof(text), "%d file%s unsaved", unsaved, unsaved == 1 ? "" : "s");
		const float right = bw + (unsaved ? ImGui::CalcTextSize(text).x + style.ItemSpacing.x * 2.0f : 0.0f);
		ImGui::SameLine(ImGui::GetContentRegionMax().x - right);
		if (unsaved) {
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(MenuTheme::Warning(), "%s", text);
			Tip("Edited preset files that are not saved yet. They are kept while the game runs.");
			ImGui::SameLine(0, style.ItemSpacing.x * 2.0f);
		}
		if (ImGui::Button("Reload", ImVec2(bw, 0))) Data().LocReload();
		Tip("Re-reads Data\\Config\\Cartographer and Data\\KEYWORDS and re-applies them to where you are now (blends if BlendSeconds > 0).");
		ImGui::EndChild();
	}
}

void LocationsWindow::OnOpened() {
}

void LocationsWindow::ShowTab(int tab) {
	gTab = tab;
}

void LocationsWindow::Draw() {
	ImGuiIO& io = ImGui::GetIO();
	const float s = MenuBackend::Scale();

	// Escape closes the window unless a text field or a dialog is in use.
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
		Data().CloseMenu();
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.70f, io.DisplaySize.y * 0.80f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(900.0f * s, io.DisplaySize.x), std::min(560.0f * s, io.DisplaySize.y)), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

	ImGui::Begin("Locations##nvr", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	const LocationStatus& st = Data().LocStatus();
	if (!st.Enabled) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextColored(MenuTheme::Warning(), "%s", st.Warning.empty() ? "Locations is off. Turn it on in the settings menu (Locations page)." : st.Warning.c_str());
		ImGui::PopTextWrapPos();
	}
	else {
		const bool presetsTab = Data().PresetEditor();
		if (!presetsTab && gTab == 0) gTab = 1;
		DrawHeaderBar(presetsTab);
		if (gTab == 0) {
			ImGui::BeginChild("##presets", ImVec2(0, 0), ImGuiChildFlags_None);
			PresetsPage::Draw();
			ImGui::EndChild();
		}
		else DrawCurrentLocation();
	}

	ImGui::End();
}
