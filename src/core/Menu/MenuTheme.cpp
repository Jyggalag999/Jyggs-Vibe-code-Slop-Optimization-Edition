#include "MenuTheme.h"
#include "../../../lib/imgui/imgui.h"

static ImVec4 gAccent = ImVec4(40 / 255.0f, 140 / 255.0f, 255 / 255.0f, 1.0f);
static ImVec4 gMuted = ImVec4(0.55f, 0.58f, 0.64f, 1.0f);
static ImVec4 gWarning = ImVec4(0.88f, 0.64f, 0.35f, 1.0f);

static ImVec4 Rgb(const UInt8 c[3], float alpha = 1.0f) { return ImVec4(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, alpha); }
static ImVec4 Hex(unsigned rgb, float alpha = 1.0f) {
	return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}
static ImVec4 WithAlpha(ImVec4 c, float a) { c.w = a; return c; }
// Mix toward another color: t = 0 gives a, t = 1 gives b.
static ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
	return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
}
static ImVec4 Lighten(const ImVec4& c, float t) { return Mix(c, ImVec4(1, 1, 1, 1), t); }

ImVec4 MenuTheme::Accent() { return gAccent; }
ImVec4 MenuTheme::AccentDim() { return WithAlpha(gAccent, 0.35f); }
ImVec4 MenuTheme::Muted() { return gMuted; }
ImVec4 MenuTheme::Warning() { return gWarning; }

unsigned MenuTheme::CursorFill(bool matchAccent, const UInt8 custom[3]) {
	const ImVec4 c = matchAccent ? gAccent : Rgb(custom);
	return IM_COL32((int)(c.x * 255.0f + 0.5f), (int)(c.y * 255.0f + 0.5f), (int)(c.z * 255.0f + 0.5f), 255);
}

void MenuTheme::Apply(float scale, const Colors& colors) {
	gAccent = Rgb(colors.accent);

	ImGuiStyle style; // fresh defaults so repeated scaling does not compound

	style.WindowPadding = ImVec2(16, 14);
	style.FramePadding = ImVec2(10, 6);
	style.ItemSpacing = ImVec2(10, 9);
	style.ItemInnerSpacing = ImVec2(8, 6);
	style.CellPadding = ImVec2(10, 7);
	style.IndentSpacing = 16;
	style.ScrollbarSize = 12;
	style.GrabMinSize = 14;

	style.WindowRounding = 8;
	style.ChildRounding = 6;
	style.FrameRounding = 6;
	style.PopupRounding = 6;
	style.ScrollbarRounding = 6;
	style.GrabRounding = 6;
	style.TabRounding = 6;

	style.WindowBorderSize = 0;
	style.ChildBorderSize = 0;
	style.PopupBorderSize = 0;
	style.FrameBorderSize = 0;
	style.TabBorderSize = 0;
	style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	style.SeparatorTextBorderSize = 1;

	ImVec4* c = style.Colors;
	const ImVec4 text = Rgb(colors.text);
	const ImVec4 bg = Rgb(colors.background);
	const ImVec4 panel = Rgb(colors.panel);
	const ImVec4 frame = Rgb(colors.frame);
	const ImVec4 frameHover = Lighten(frame, 0.08f);
	const ImVec4 frameActive = Lighten(frame, 0.16f);
	const ImVec4 button = Lighten(frame, 0.05f);
	const ImVec4 line = Lighten(frame, 0.04f);
	gMuted = Mix(text, bg, 0.45f);
	gWarning = Mix(Hex(0xE0A458), text, 0.1f);

	c[ImGuiCol_Text] = text;
	c[ImGuiCol_TextDisabled] = Mix(text, bg, 0.62f);
	c[ImGuiCol_WindowBg] = WithAlpha(bg, 0.96f);
	c[ImGuiCol_ChildBg] = WithAlpha(panel, 0.96f);
	c[ImGuiCol_PopupBg] = WithAlpha(panel, 0.98f);
	c[ImGuiCol_Border] = WithAlpha(line, 0.0f);
	c[ImGuiCol_BorderShadow] = Hex(0x000000, 0.0f);
	c[ImGuiCol_FrameBg] = frame;
	c[ImGuiCol_FrameBgHovered] = frameHover;
	c[ImGuiCol_FrameBgActive] = frameActive;
	c[ImGuiCol_TitleBg] = bg;
	c[ImGuiCol_TitleBgActive] = bg;
	c[ImGuiCol_TitleBgCollapsed] = bg;
	c[ImGuiCol_MenuBarBg] = panel;
	c[ImGuiCol_ScrollbarBg] = Hex(0x000000, 0.0f);
	c[ImGuiCol_ScrollbarGrab] = Lighten(frame, 0.14f);
	c[ImGuiCol_ScrollbarGrabHovered] = Lighten(frame, 0.24f);
	c[ImGuiCol_ScrollbarGrabActive] = Lighten(frame, 0.34f);
	c[ImGuiCol_CheckMark] = gAccent;
	c[ImGuiCol_SliderGrab] = WithAlpha(gAccent, 0.85f);
	c[ImGuiCol_SliderGrabActive] = gAccent;
	c[ImGuiCol_Button] = button;
	c[ImGuiCol_ButtonHovered] = Lighten(frame, 0.13f);
	c[ImGuiCol_ButtonActive] = Lighten(frame, 0.21f);
	c[ImGuiCol_Header] = WithAlpha(gAccent, 0.20f);
	c[ImGuiCol_HeaderHovered] = WithAlpha(gAccent, 0.30f);
	c[ImGuiCol_HeaderActive] = WithAlpha(gAccent, 0.38f);
	c[ImGuiCol_Separator] = line;
	c[ImGuiCol_SeparatorHovered] = WithAlpha(gAccent, 0.6f);
	c[ImGuiCol_SeparatorActive] = gAccent;
	c[ImGuiCol_ResizeGrip] = Hex(0x000000, 0.0f);
	c[ImGuiCol_ResizeGripHovered] = WithAlpha(gAccent, 0.5f);
	c[ImGuiCol_ResizeGripActive] = WithAlpha(gAccent, 0.8f);
	c[ImGuiCol_TextSelectedBg] = WithAlpha(gAccent, 0.35f);
	c[ImGuiCol_TableHeaderBg] = WithAlpha(panel, 0.0f);
	c[ImGuiCol_TableBorderStrong] = line;
	c[ImGuiCol_TableBorderLight] = Mix(line, panel, 0.5f);
	c[ImGuiCol_TableRowBg] = Hex(0x000000, 0.0f);
	c[ImGuiCol_TableRowBgAlt] = WithAlpha(gAccent, 0.04f);
	c[ImGuiCol_NavCursor] = WithAlpha(gAccent, 0.0f);
	c[ImGuiCol_ModalWindowDimBg] = Hex(0x000000, 0.55f);

	style.ScaleAllSizes(scale);
	ImGui::GetStyle() = style;
}
