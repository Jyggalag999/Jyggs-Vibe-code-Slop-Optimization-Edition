#include "MenuInput.h"
#include "MenuUI.h"
#include "../Locations/LocationApply.h"
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

	// The menu's own cursor: DirectInput mouse deltas gathered in UpdateValuesHook, applied in FeedInput.
	bool				gCursorValid = false;	// false = put it at the screen center on the next FeedInput
	float				gCursorX = 0.0f;
	float				gCursorY = 0.0f;
	LONG				gPendingDX = 0;
	LONG				gPendingDY = 0;
	LONG				gPendingWheel = 0;
	bool				gMouseButtons[3] = {};

	// Keyboard: DirectInput snapshot taken in UpdateValuesHook before the game's copy is cleared. Typing does not
	// depend on WM_KEYDOWN/WM_CHAR reaching the window (some setups never deliver them).
	UInt8				gKeys[256] = {};
	UInt8				gPrevKeys[256] = {};
	bool				gKeysValid = false;
	DWORD				gRepeatDik = 0;			// DIK being auto-repeated as characters (0 = none)
	DWORD				gRepeatNextTick = 0;
		UInt32				gWmCharCount = 0;		// WM_CHAR seen while the menu was open (0 = messages never arrive)

	// Key capture (Keybinds page)
	bool				gCapture = false;		// waiting for a key
	bool				gCaptureFresh = false;	// first frame: only take the held-key snapshot
	bool				gCapturePending = false;	// a key was captured, the page has not collected it yet
	int					gCapturedDik = 0;
	UInt8				gCapPrev[256] = {};
	bool				gHotkeyHold = false;	// after a capture: hotkeys stay off until every key is released

	ImGuiKey DikToImGuiKey(int dik) {
		switch (dik) {
		case 0x01: return ImGuiKey_Escape;	case 0x0E: return ImGuiKey_Backspace;	case 0x0F: return ImGuiKey_Tab;
		case 0x1C: return ImGuiKey_Enter;	case 0x39: return ImGuiKey_Space;		case 0x9C: return ImGuiKey_KeypadEnter;
		case 0x1D: return ImGuiKey_LeftCtrl;	case 0x9D: return ImGuiKey_RightCtrl;
		case 0x2A: return ImGuiKey_LeftShift;	case 0x36: return ImGuiKey_RightShift;
		case 0x38: return ImGuiKey_LeftAlt;		case 0xB8: return ImGuiKey_RightAlt;
		case 0xC7: return ImGuiKey_Home;	case 0xC8: return ImGuiKey_UpArrow;		case 0xC9: return ImGuiKey_PageUp;
		case 0xCB: return ImGuiKey_LeftArrow;	case 0xCD: return ImGuiKey_RightArrow;
		case 0xCF: return ImGuiKey_End;		case 0xD0: return ImGuiKey_DownArrow;	case 0xD1: return ImGuiKey_PageDown;
		case 0xD2: return ImGuiKey_Insert;	case 0xD3: return ImGuiKey_Delete;
		case 0x0B: return ImGuiKey_0;
		case 0x4F: return ImGuiKey_Keypad1;	case 0x50: return ImGuiKey_Keypad2;	case 0x51: return ImGuiKey_Keypad3;
		case 0x4B: return ImGuiKey_Keypad4;	case 0x4C: return ImGuiKey_Keypad5;	case 0x4D: return ImGuiKey_Keypad6;
		case 0x47: return ImGuiKey_Keypad7;	case 0x48: return ImGuiKey_Keypad8;	case 0x49: return ImGuiKey_Keypad9;
		case 0x52: return ImGuiKey_Keypad0;	case 0x53: return ImGuiKey_KeypadDecimal;
		case 0x4E: return ImGuiKey_KeypadAdd;	case 0x4A: return ImGuiKey_KeypadSubtract;
		case 0x37: return ImGuiKey_KeypadMultiply;	case 0xB5: return ImGuiKey_KeypadDivide;
		case 0x0C: return ImGuiKey_Minus;	case 0x0D: return ImGuiKey_Equal;
		case 0x1A: return ImGuiKey_LeftBracket;	case 0x1B: return ImGuiKey_RightBracket;
		case 0x2B: return ImGuiKey_Backslash;	case 0x27: return ImGuiKey_Semicolon;	case 0x28: return ImGuiKey_Apostrophe;
		case 0x29: return ImGuiKey_GraveAccent;	case 0x33: return ImGuiKey_Comma;	case 0x34: return ImGuiKey_Period;
		case 0x35: return ImGuiKey_Slash;
		}
		if (dik >= 0x02 && dik <= 0x0A) return (ImGuiKey)(ImGuiKey_1 + (dik - 0x02));
		if (dik >= 0x3B && dik <= 0x44) return (ImGuiKey)(ImGuiKey_F1 + (dik - 0x3B));
		if (dik == 0x57) return ImGuiKey_F11;
		if (dik == 0x58) return ImGuiKey_F12;
		static const char* kRows[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
		static const int kRowStart[3] = { 0x10, 0x1E, 0x2C };
		for (int r = 0; r < 3; r++) {
			const int n = (int)strlen(kRows[r]);
			if (dik >= kRowStart[r] && dik < kRowStart[r] + n) return (ImGuiKey)(ImGuiKey_A + (kRows[r][dik - kRowStart[r]] - 'A'));
		}
		return ImGuiKey_None;
	}

	// DIK -> text with the active keyboard layout. Returns the UTF-16 unit count (0 if the key makes no text).
	int DikToText(int dik, wchar_t* out) {
		const bool shift = gKeys[0x2A] || gKeys[0x36];
		const bool ctrl = gKeys[0x1D] || gKeys[0x9D];
		const bool altL = gKeys[0x38] != 0, altR = gKeys[0xB8] != 0;
		const bool altGr = ctrl && altR;
		if ((ctrl && !altGr) || altL) return 0;		// shortcuts (Ctrl+A/C/V) go to ImGui as keys
		if (altR && !ctrl) return 0;

		HKL layout = GetKeyboardLayout(0);
		const bool extended = (dik & 0x80) != 0;
		UINT vk = MapVirtualKeyExA(extended ? (0xE000 | (dik & 0x7F)) : dik, MAPVK_VSC_TO_VK_EX, layout);
		if (!vk) return 0;
		BYTE state[256] = {};
		if (shift) state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
		if (altGr) { state[VK_CONTROL] = state[VK_LCONTROL] = 0x80; state[VK_MENU] = state[VK_RMENU] = 0x80; }
		if (GetKeyState(VK_CAPITAL) & 1) state[VK_CAPITAL] = 1;
		if (GetKeyState(VK_NUMLOCK) & 1) state[VK_NUMLOCK] = 1;
		int n = ToUnicodeEx(vk, dik & 0x7F, state, out, 4, 0x4 /* do not change dead-key state */, layout);
		if (n < 0) return 0;
		for (int i = 0; i < n; i++) if (out[i] < 0x20 || out[i] == 0x7F) return 0;
		return n;
	}

	void ResetKeyboard() {
		memset(gKeys, 0, sizeof(gKeys));
		memset(gPrevKeys, 0, sizeof(gPrevKeys));
		gKeysValid = false;
		gRepeatDik = 0;
	}

	void FeedKeyboard(ImGuiIO& io) {
		if (!gKeysValid) return;
		const DWORD now = GetTickCount();
		for (int dik = 1; dik < 256; dik++) {
			const bool down = (gKeys[dik] & 0x80) != 0;
			const bool was = (gPrevKeys[dik] & 0x80) != 0;
			if (down == was) continue;
			const ImGuiKey k = DikToImGuiKey(dik);
			if (k != ImGuiKey_None) io.AddKeyEvent(k, down);
			if (down) {
				wchar_t text[4];
				const int n = DikToText(dik, text);
				if (n > 0) {
					for (int i = 0; i < n; i++) io.AddInputCharacterUTF16(text[i]);
					gRepeatDik = dik;
					gRepeatNextTick = now + 450;
				}
			}
			else if (gRepeatDik == (DWORD)dik) gRepeatDik = 0;
		}
		if (gRepeatDik && (gKeys[gRepeatDik] & 0x80)) {
			if ((LONG)(now - gRepeatNextTick) >= 0) {
				wchar_t text[4];
				const int n = DikToText(gRepeatDik, text);
				for (int i = 0; i < n; i++) io.AddInputCharacterUTF16(text[i]);
				gRepeatNextTick = now + 33;
			}
		}
		else gRepeatDik = 0;
		memcpy(gPrevKeys, gKeys, sizeof(gKeys));
	}

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

		// Go (coc/cow) from the Locations window runs here, in the main loop. Run from the render hook it did the
		// whole cell load in the middle of drawing a frame and the game froze.
		if (TheLocationManager) TheLocationManager->RunPendingGo();

		GameMenuManager* menu = TheGameMenuManager;
		InputControl* input = (InputControl*)apControls;
		if (!menu || !input) return;

		// Key capture for the Keybinds page: the first new key press is taken before the hotkeys below see it.
		if (gCapture && menu->Enabled) {
			if (gCaptureFresh) {
				memcpy(gCapPrev, input->CurrentKeyState, sizeof(gCapPrev));
				gCaptureFresh = false;
			}
			else {
				for (int d = 1; d < 256; d++) {
					if ((input->CurrentKeyState[d] & 0x80) && !(gCapPrev[d] & 0x80)) {
						gCapturedDik = d;
						gCapture = false;
						gCapturePending = true;
						gHotkeyHold = true;
						break;
					}
				}
				memcpy(gCapPrev, input->CurrentKeyState, sizeof(gCapPrev));
			}
		}
		if (gHotkeyHold && !gCapture && !gCapturePending) {
			bool any = false;
			for (int d = 1; d < 256; d++) if (input->CurrentKeyState[d] & 0x80) any = true;
			if (!any) gHotkeyHold = false;
		}
		const bool suppressHotkeys = gCapture || gCapturePending || gHotkeyHold;

		// The menu key is read from the raw state, before it is cleared below. Edges are tracked here, not with
		// PreviousKeyState: ClearGameInput zeroes that every frame the menu is open, so a held key would look
		// newly pressed on the next frame and close the menu again.
		static bool sMenuKeyDown = false;
		static bool sSaveKeyDown = false;
		static bool sLocationsKeyDown = false;
		const UInt8 key = TheSettingManager->SettingsMain.Menu.KeyEnable;
		const bool menuKeyDown = key && input->CurrentKeyState[key];
		const bool menuKeyPressed = menuKeyDown && !sMenuKeyDown && !suppressHotkeys;
		sMenuKeyDown = menuKeyDown;
		const UInt8 locKey = TheSettingManager->SettingsMain.Menu.KeyLocations;
		const bool locKeyDown = locKey && input->CurrentKeyState[locKey];
		const bool locKeyPressed = locKeyDown && !sLocationsKeyDown && !suppressHotkeys;
		sLocationsKeyDown = locKeyDown;
		// Closed: a key opens its window. Open: the same key closes it, the other key switches window.
		if ((menuKeyPressed || locKeyPressed) && !MenuTextActive()) {
			const GameMenuManager::Window target = menuKeyPressed ? GameMenuManager::Window::Settings : GameMenuManager::Window::Locations;
			if (menu->Enabled) {
				if (menu->ActiveWindow == target) {
					menu->Enabled = false;
					MenuInput::OnToggled(false);
				}
				else menu->ActiveWindow = target;
			}
			else if (InterfaceManager && InterfaceManager->IsActive(Menu::MenuType::kMenuType_None)) {
				menu->ActiveWindow = target;
				menu->Enabled = true;
				MenuInput::OnToggled(true);
			}
		}

		// The save key works while the menu is open.
		const UInt8 saveKey = TheSettingManager->SettingsMain.Menu.KeySave;
		const bool saveKeyDown = saveKey && input->CurrentKeyState[saveKey];
		const bool saveKeyPressed = saveKeyDown && !sSaveKeyDown && !suppressHotkeys;
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
			memcpy(gKeys, input->CurrentKeyState, sizeof(gKeys));
				gKeysValid = true;
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

	bool IsKeyboardMessage(UINT msg) {
		return msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR || msg == WM_UNICHAR;
	}

	LRESULT CALLBACK MenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		GameMenuManager* menu = TheGameMenuManager;
		if (menu && menu->Enabled && ImGui::GetCurrentContext()) {
			// Mouse and keyboard come from DirectInput (FeedInput); window messages only feed focus and the like.
			if (msg == WM_CHAR) gWmCharCount++;
			if (!IsMouseMessage(msg) && !IsKeyboardMessage(msg)) ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
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

void MenuInput::BeginKeyCapture() {
	gCapture = true;
	gCaptureFresh = true;
	gCapturePending = false;
}

void MenuInput::CancelKeyCapture() {
	gCapture = false;
	gCapturePending = false;
}

bool MenuInput::KeyCaptureActive() { return gCapture || gCapturePending; }

int MenuInput::PollCapturedKey() {
	if (!gCapturePending) return -1;
	gCapturePending = false;
	return gCapturedDik;
}

void MenuInput::OnToggled(bool open) {
	CancelKeyCapture();
	gHotkeyHold = false;
	if (!open) Logger::Log("Menu: keyboard via DirectInput; window messages seen this session: %u WM_CHAR", gWmCharCount);
	ResetKeyboard();
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

void MenuInput::FeedInput() {
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

	FeedKeyboard(io);
}
