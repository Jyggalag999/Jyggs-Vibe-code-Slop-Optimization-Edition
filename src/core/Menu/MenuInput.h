#pragma once

// Input for the ImGui menu:
//  - a chained call hook on Controls::UpdateValues (0x0086F39E, inside TESMain::OnIdle_PollControls) that
//    reads the menu key and, while the menu is open, clears the game's keyboard/mouse/gamepad button state;
//  - a chained WndProc subclass that feeds keys and characters to ImGui (xNVSE makes the game's DirectInput
//    devices non-exclusive, so Windows messages do arrive).
// The mouse comes from the game's DirectInput state, not Windows: in game the Windows cursor stays pinned at the
// window center, so ImGui gets its own cursor moved by the DirectInput deltas (FeedMouse).
namespace MenuInput {
	// Feeds the menu's own cursor, buttons and wheel to ImGui. Call between ImGui_ImplWin32_NewFrame and
	// ImGui::NewFrame (after the backend's GetCursorPos fallback, so ours wins), only while the menu is open.
	void		FeedMouse();
	void		InstallHooks();
	// Subclasses the game window. Idempotent; call once the window exists.
	void		InstallWndProc(HWND hwnd);
	// Restores the previous window procedure, only if ours is still the current one.
	void		RemoveWndProc();
	bool		WndProcInstalled();
	// Called when the menu opens or closes.
	void		OnToggled(bool open);
}
