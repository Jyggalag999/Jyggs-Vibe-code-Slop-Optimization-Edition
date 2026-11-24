#include "MenuWidgets.h"
#include "MenuBackend.h"
#include "MenuTheme.h"
#include "../../../lib/imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

// The game headers define min/max macros.
#undef min
#undef max

namespace MenuWidgets {

void Tip(const std::string& text) {
	if (text.empty() || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) return;
	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
	ImGui::TextUnformatted(text.c_str());
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

void Muted(const char* text) {
	ImGui::PushStyleColor(ImGuiCol_Text, MenuTheme::Muted());
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

void MutedF(const char* fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	Muted(buf);
}

void PageHeader(const char* title, const char* subtitle) {
	ImGui::PushFont(MenuBackend::FontSemiBold());
	ImGui::TextUnformatted(title);
	ImGui::PopFont();
	if (subtitle && *subtitle) Muted(subtitle);
}

float ToggleSwitchWidth() { return ImGui::GetFrameHeight() * 0.80f * 1.8f; }

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

float BadgeWidth(const char* text) {
	return ImGui::CalcTextSize(text).x * 0.8f + ImGui::GetStyle().FramePadding.x * 2.0f;
}

void Badge(const char* text, bool warning) {
	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec2 ts(ImGui::CalcTextSize(text).x * 0.8f, ImGui::CalcTextSize(text).y * 0.8f);
	const ImVec2 size(ts.x + style.FramePadding.x * 2.0f, ImGui::GetFrameHeight() * 0.8f);
	const ImVec2 p = ImGui::GetCursorScreenPos();
	ImGui::Dummy(size);
	const ImVec4 col = warning ? MenuTheme::Warning() : MenuTheme::Accent();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(col), 3.0f);
	dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(p.x + style.FramePadding.x, p.y + (size.y - ts.y) * 0.5f), ImGui::GetColorU32(col), text);
}

int ToolbarButtons(const char* const* labels, int n, const bool* enabled, int primary) {
	if (n <= 0) return -1;
	const ImGuiStyle& style = ImGui::GetStyle();
	const float avail = ImGui::GetContentRegionAvail().x;
	const float each = std::floor((avail - style.ItemSpacing.x * (n - 1)) / n);
	int clicked = -1;
	for (int i = 0; i < n; i++) {
		if (i) ImGui::SameLine();
		const float minW = ImGui::CalcTextSize(labels[i]).x + style.FramePadding.x * 2.0f;
		const bool off = enabled && !enabled[i];
		if (off) ImGui::BeginDisabled();
		if (i == primary) {
			ImGui::PushStyleColor(ImGuiCol_Button, MenuTheme::AccentDim());
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, MenuTheme::Accent());
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, MenuTheme::Accent());
		}
		if (ImGui::Button(labels[i], ImVec2(std::max(each, minW), 0))) clicked = i;
		if (i == primary) ImGui::PopStyleColor(3);
		if (off) ImGui::EndDisabled();
	}
	return clicked;
}

void StatusLine(const std::string& text, bool error) {
	if (text.empty()) return;
	ImGui::PushStyleColor(ImGuiCol_Text, error ? MenuTheme::Warning() : MenuTheme::Muted());
	ImGui::TextWrapped("%s", text.c_str());
	ImGui::PopStyleColor();
}

bool SplitterV(const char* id, float* width, float minW, float maxW, float height) {
	const float w = 6.0f * MenuBackend::Scale();
	ImGui::SameLine(0, 0);
	ImGui::InvisibleButton(id, ImVec2(w, height));
	const bool active = ImGui::IsItemActive();
	const bool hov = ImGui::IsItemHovered();
	if (hov || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	if (active) *width = std::max(minW, std::min(maxW, *width + ImGui::GetIO().MouseDelta.x));
	const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
	const float x = (a.x + b.x) * 0.5f;
	ImGui::GetWindowDrawList()->AddLine(ImVec2(x, a.y + 2.0f), ImVec2(x, b.y - 2.0f),
		ImGui::GetColorU32(active ? MenuTheme::Accent() : hov ? MenuTheme::Muted() : ImGui::GetStyleColorVec4(ImGuiCol_Border)), 1.0f);
	ImGui::SameLine(0, 0);
	return active;
}

bool TabButton(const char* label, bool active) {
	const ImGuiStyle& style = ImGui::GetStyle();
	const float w = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f + 14.0f * MenuBackend::Scale();
	ImGui::PushStyleColor(ImGuiCol_Button, active ? MenuTheme::AccentDim() : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	const bool clicked = ImGui::Button(label, ImVec2(w, 0));
	ImGui::PopStyleColor();
	if (active) {
		const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y), ImGui::GetColorU32(MenuTheme::Accent()), 2.0f);
	}
	return clicked;
}

bool Ellipsized(const std::string& text, float width) {
	const float avail = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
	const float full = ImGui::CalcTextSize(text.c_str()).x;
	if (full <= avail) { ImGui::TextUnformatted(text.c_str()); return false; }
	const char* dots = "...";
	const float room = std::max(0.0f, avail - ImGui::CalcTextSize(dots).x);
	// Longest prefix that fits (cut on a character boundary).
	size_t len = text.size();
	while (len > 0 && ImGui::CalcTextSize(text.c_str(), text.c_str() + len).x > room) {
		len--;
		while (len > 0 && (text[len] & 0xC0) == 0x80) len--;
	}
	const std::string cut = text.substr(0, len) + dots;
	ImGui::TextUnformatted(cut.c_str());
	return true;
}

}
