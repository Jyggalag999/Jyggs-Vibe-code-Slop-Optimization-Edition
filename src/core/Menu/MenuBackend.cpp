#include "MenuBackend.h"
#include "MenuInput.h"
#include "MenuTheme.h"
#include "Fonts.h"
#include "../../../lib/imgui/imgui.h"
#include "../../../lib/imgui/imgui_internal.h" // ImGui::RenderMouseCursor
#include "../../../lib/imgui/backends/imgui_impl_dx9.h"
#include "../../../lib/imgui/backends/imgui_impl_win32.h"

namespace {
	constexpr float BaseFontSize = 17.0f;
	constexpr float CursorScale = 1.75f;	// menu cursor size at 1080p (ImGui's default is 1.0, unscaled)

	bool		gMenuOpen = false;

	bool		gInitialised = false;
	bool		gFailed = false;
	bool		gStyleDirty = true;
	float		gScale = 0.0f;
	ImFont*		gRegular = nullptr;
	ImFont*		gSemiBold = nullptr;

	// NiDX9Renderer reset notification: bBeforeReset = true right before the D3D device is reset, false right after.
	bool __cdecl OnDeviceReset(bool abBeforeReset, void*) {
		if (!gInitialised) return true;
		if (abBeforeReset)
			ImGui_ImplDX9_InvalidateDeviceObjects();
		else
			ImGui_ImplDX9_CreateDeviceObjects();
		return true;
	}

	float ScreenScale() {
		float scale = (float)TheRenderManager->height / 1080.0f;
		if (scale < 0.5f) scale = 0.5f;
		if (scale > 3.0f) scale = 3.0f;
		return scale;
	}

	void BuildFonts(float scale) {
		ImGuiIO& io = ImGui::GetIO();
		const float size = floorf(BaseFontSize * scale + 0.5f);

		ImGui_ImplDX9_InvalidateDeviceObjects(); // drops the font texture; the backend rebuilds it on the next frame
		io.Fonts->Clear();
		gRegular = io.Fonts->AddFontFromMemoryCompressedTTF(InterRegular_compressed_data, InterRegular_compressed_size, size);
		gSemiBold = io.Fonts->AddFontFromMemoryCompressedTTF(InterSemiBold_compressed_data, InterSemiBold_compressed_size, size);
		io.FontDefault = gRegular;
	}
}

bool MenuBackend::IsInitialised() { return gInitialised; }
ImFont* MenuBackend::FontRegular() { return gRegular; }
ImFont* MenuBackend::FontSemiBold() { return gSemiBold; }
float MenuBackend::Scale() { return gScale; }
void MenuBackend::RequestStyleRefresh() { gStyleDirty = true; }

bool MenuBackend::EnsureInit() {
	if (gInitialised) return true;
	if (gFailed) return false;

	NiDX9Renderer* renderer = NiDX9Renderer::GetSingleton();
	IDirect3DDevice9* device = TheRenderManager ? TheRenderManager->device : nullptr;
	if (!renderer || !device) return false;

	HWND hwnd = renderer->windowDevice ? renderer->windowDevice : renderer->windowFocus;
	if (!hwnd || !IsWindow(hwnd)) return false;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // do not write imgui.ini into the game folder
	io.LogFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // the game owns the Windows cursor; ImGui draws its own

	if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX9_Init(device)) {
		Logger::Log("Menu: ImGui backend initialisation failed, the menu is disabled.");
		ImGui::DestroyContext();
		gFailed = true;
		return false;
	}

	// Release and rebuild ImGui's D3D objects around every device reset (alt-tab, resolution change).
	ThisCall(0x0086BAE0, renderer, (void*)OnDeviceReset, (void*)nullptr);

	gInitialised = true;
	gStyleDirty = true;
	MenuInput::InstallWndProc(hwnd);
	Logger::Log("Menu: ImGui %s initialised.", IMGUI_VERSION);
	return true;
}

bool MenuBackend::BeginFrame(bool menuOpen) {
	if (!EnsureInit()) return false;

	const float scale = ScreenScale();
	if (gStyleDirty || scale != gScale) {
		if (scale != gScale || !gRegular) BuildFonts(scale);
		gScale = scale;
		const auto& m = TheSettingManager->SettingsMain.Menu;
		MenuTheme::Colors colors;
		memcpy(colors.accent, m.AccentColor, 3);
		memcpy(colors.background, m.BackgroundColor, 3);
		memcpy(colors.panel, m.PanelColor, 3);
		memcpy(colors.frame, m.FrameColor, 3);
		memcpy(colors.text, m.TextColor, 3);
		MenuTheme::Apply(scale, colors);
		gStyleDirty = false;
	}

	ImGuiIO& io = ImGui::GetIO();
	io.MouseDrawCursor = false; // drawn in EndFrame instead, bigger, in the accent color
	gMenuOpen = menuOpen;

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	if (menuOpen) MenuInput::FeedInput(); // after the backend's GetCursorPos fallback, so the menu's cursor wins
	ImGui::NewFrame();
	return true;
}

void MenuBackend::EndFrame() {
	if (gMenuOpen && ImGui::GetMouseCursor() != ImGuiMouseCursor_None)
		ImGui::RenderMouseCursor(ImGui::GetIO().MousePos, CursorScale * gScale, ImGui::GetMouseCursor(),
			MenuTheme::CursorFill(TheSettingManager->SettingsMain.Menu.CursorMatchesAccent, TheSettingManager->SettingsMain.Menu.CursorColor), IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 96));
	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}
