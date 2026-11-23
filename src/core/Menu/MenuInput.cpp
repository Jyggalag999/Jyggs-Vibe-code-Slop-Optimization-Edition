#include "MenuInput.h"
#include "MenuUI.h"
#include "../../../lib/imgui/imgui.h"
#include "../../../lib/imgui/backends/imgui_impl_win32.h"

// Declared (but disabled) in imgui_impl_win32.h.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
	// Controls::UpdateValues call inside TESMain::OnIdle_PollControls.
	constexpr SIZE_T	kUpdateValuesCall = 0x0086F39E;

	// Controls singleton layout (see InputControl in Game.h): joystick states start at 0x34, 8 devices x 2
	// (current, previous) DIJOYSTATE of 80 bytes each.
	constexpr SIZE_T	kJoyStateOffset = 0x34;
	constexpr UInt32	kJoyStateCount = 16;

	CallDetour			gUpdateValuesDetour;

	WNDPROC				gPrevProc = nullptr;
	HWND				gHwnd = nullptr;
	bool				gProcIsUnicode = false;

	// The menu's own cursor: DirectInput mouse deltas gathered in UpdateValuesHook, applied in FeedMouse.
	bool				gCursorValid = false;	// false = put it at the screen center on the next FeedMouse
	float				gCursorX = 0.0f;
	float				gCursorY = 0.0f;
	LONG				gPendingDX = 0;
	LONG				gPendingDY = 0;
	LONG				gPendingWheel = 0;
	bool				gMouseButtons[3] = {};

	bool MenuTextActive() {
		return ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
	}

	// Clears everything the game would turn into actions: keyboard, mouse and gamepad buttons.
	void ClearGameInput(InputControl* input, void* controls) {
		memset(input->CurrentKeyState, 0, sizeof(input->CurrentKeyState));
		memset(input->PreviousKeyState, 0, sizeof(input->PreviousKeyState));
		memset(&input->CurrentMouseState, 0, sizeof(input->CurrentMouseState));
		memset(&input->PreviousMouseState, 0, sizeof(input->PreviousMouseState));

		// Gamepad: release buttons and the d-pad. Sticks and triggers are left as the device reports them.
		DIJOYSTATE* joy = (DIJOYSTATE*)((UInt8*)controls + kJoyStateOffset);
		for (UInt32 i = 0; i < kJoyStateCount; i++) {
			memset(joy[i].rgbButtons, 0, sizeof(joy[i].rgbButtons));
			for (int p = 0; p < 4; p++) joy[i].rgdwPOV[p] = 0xFFFFFFFF;
		}
	}

	void __fastcall UpdateValuesHook(void* apControls, void*) {
		ThisCall(gUpdateValuesDetour.GetOverwrittenAddr(), apControls);

		GameMenuManager* menu = TheGameMenuManager;
		InputControl* input = (InputControl*)apControls;
		if (!menu || !input) return;

		// The menu key is read from the raw state, before it is cleared below. Edges are tracked here, not with
		// PreviousKeyState: ClearGameInput zeroes that every frame the menu is open, so a held key would look
		// newly pressed on the next frame and close the menu again.
		static bool sMenuKeyDown = false;
		static bool sSaveKeyDown = false;
		const UInt8 key = TheSettingManager->SettingsMain.Menu.KeyEnable;
		const bool menuKeyDown = key && input->CurrentKeyState[key];
		const bool menuKeyPressed = menuKeyDown && !sMenuKeyDown;
		sMenuKeyDown = menuKeyDown;
		if (menuKeyPressed && !MenuTextActive()) {
			if (menu->Enabled) {
				menu->Enabled = false;
				MenuInput::OnToggled(false);
			}
			else if (InterfaceManager && InterfaceManager->IsActive(Menu::MenuType::kMenuType_None)) {
				menu->Enabled = true;
				MenuInput::OnToggled(true);
			}
		}

		// The save key works while the menu is open.
		const UInt8 saveKey = TheSettingManager->SettingsMain.Menu.KeySave;
		const bool saveKeyDown = saveKey && input->CurrentKeyState[saveKey];
		const bool saveKeyPressed = saveKeyDown && !sSaveKeyDown;
		sSaveKeyDown = saveKeyDown;
		if (menu->Enabled && saveKeyPressed && !MenuTextActive() && TheSettingManager->hasUnsavedChanges)
			MenuUI::Save();

		if (menu->Enabled) {
			// Take the mouse for the menu's cursor before the game's copy is cleared.
			const DIMOUSESTATE2& mouse = input->CurrentMouseState;
			gPendingDX += mouse.lX;
			gPendingDY += mouse.lY;
			gPendingWheel += mouse.lZ;
			for (int b = 0; b < 3; b++) gMouseButtons[b] = (mouse.rgbButtons[b] & 0x80) != 0;
			ClearGameInput(input, apControls);
		}
	}

	bool IsMouseMessage(UINT msg) {
		switch (msg) {
		case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_NCMOUSEMOVE: case WM_NCMOUSELEAVE:
		case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
		case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
		case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
			return true;
		}
		return false;
	}

	LRESULT CALLBACK MenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		GameMenuManager* menu = TheGameMenuManager;
		if (menu && menu->Enabled && ImGui::GetCurrentContext()) {
			// Mouse messages carry the pinned Windows cursor; the menu's mouse comes from DirectInput (FeedMouse).
			if (!IsMouseMessage(msg)) ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
			const ImGuiIO& io = ImGui::GetIO();

			switch (msg) {
			case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
			case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
			case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
				if (io.WantCaptureMouse) return 0;
				break;
			case WM_KEYDOWN: case WM_KEYUP:
			case WM_CHAR: case WM_UNICHAR:
				// System keys (Alt+Tab, Alt+F4) are not swallowed.
				if (io.WantTextInput || io.WantCaptureKeyboard) return 0;
				break;
			}
		}
		return gProcIsUnicode ? CallWindowProcW(gPrevProc, hWnd, msg, wParam, lParam)
							  : CallWindowProcA(gPrevProc, hWnd, msg, wParam, lParam);
	}
}

void MenuInput::InstallHooks() {
	gUpdateValuesDetour.ReplaceCall(kUpdateValuesCall, UpdateValuesHook);
}

void MenuInput::InstallWndProc(HWND hwnd) {
	if (gPrevProc || !hwnd) return;

	gHwnd = hwnd;
	gProcIsUnicode = IsWindowUnicode(hwnd) != FALSE;
	gPrevProc = gProcIsUnicode ? (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)MenuWndProc)
							   : (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)MenuWndProc);
	if (!gPrevProc) {
		Logger::Log("Menu: could not subclass the game window (error %u), mouse and typing will not work.", GetLastError());
		return;
	}
	Logger::Log("Menu: window procedure chained.");
}

void MenuInput::RemoveWndProc() {
	if (!gPrevProc || !gHwnd) return;

	// Another plugin (dxvk, for one) may have subclassed after us; only restore if ours is still on top.
	const LONG_PTR current = gProcIsUnicode ? GetWindowLongPtrW(gHwnd, GWLP_WNDPROC) : GetWindowLongPtrA(gHwnd, GWLP_WNDPROC);
	if (current == (LONG_PTR)MenuWndProc) {
		if (gProcIsUnicode) SetWindowLongPtrW(gHwnd, GWLP_WNDPROC, (LONG_PTR)gPrevProc);
		else SetWindowLongPtrA(gHwnd, GWLP_WNDPROC, (LONG_PTR)gPrevProc);
		gPrevProc = nullptr;
	}
}

bool MenuInput::WndProcInstalled() { return gPrevProc != nullptr; }

void MenuInput::OnToggled(bool) {
	// Drop held keys/buttons so nothing is stuck across the open/close boundary; the cursor starts centered.
	gCursorValid = false;
	gPendingDX = gPendingDY = gPendingWheel = 0;
	for (bool& b : gMouseButtons) b = false;
	if (ImGui::GetCurrentContext()) {
		ImGuiIO& io = ImGui::GetIO();
		io.ClearInputKeys();
		for (int b = 0; b < 3; b++) io.AddMouseButtonEvent(b, false);
	}
}

void MenuInput::FeedMouse() {
	ImGuiIO& io = ImGui::GetIO();
	const float w = io.DisplaySize.x, h = io.DisplaySize.y;
	if (w <= 0.0f || h <= 0.0f) return;

	if (!gCursorValid) {
		gCursorX = w * 0.5f;
		gCursorY = h * 0.5f;
		gCursorValid = true;
		gPendingDX = gPendingDY = 0;
	}

	// DirectInput counts are raw mouse units; scale with the screen height so the speed feels the same at
	// 1080p and 1440p/4K.
	const float speed = h / 1080.0f;
	gCursorX = std::clamp(gCursorX + gPendingDX * speed, 0.0f, w - 1.0f);
	gCursorY = std::clamp(gCursorY + gPendingDY * speed, 0.0f, h - 1.0f);
	io.AddMousePosEvent(gCursorX, gCursorY);

	for (int b = 0; b < 3; b++) io.AddMouseButtonEvent(b, gMouseButtons[b]);
	if (gPendingWheel) io.AddMouseWheelEvent(0.0f, gPendingWheel / (float)WHEEL_DELTA);
	gPendingDX = gPendingDY = gPendingWheel = 0;
}
