#pragma once

// Input for the ImGui menu:
//  - a chained call hook on Controls::UpdateValues (0x0086F39E, inside TESMain::OnIdle_PollControls) that
//    reads the menu key and, while the menu is open, clears the game's keyboard/mouse/gamepad button state;
//  - a chained WndProc subclass that keeps mouse/keyboard window messages away from the game while the menu
//    wants them. Keys and typed characters come from the DirectInput keyboard state (window messages are not
//    reliable in every setup); WM_CHAR is only counted, to prove that in the log.
// The mouse comes from the game's DirectInput state, not Windows: in game the Windows cursor stays pinned at the
// window center, so ImGui gets its own cursor moved by the DirectInput deltas (FeedInput).
namespace MenuInput {
	// Feeds the menu's own cursor, buttons, wheel, keys and typed characters to ImGui. Call between ImGui_ImplWin32_NewFrame and
	// ImGui::NewFrame (after the backend's GetCursorPos fallback, so ours wins), only while the menu is open.
	void		FeedInput();
	void		InstallHooks();
	// Subclasses the game window. Idempotent; call once the window exists.
	void		InstallWndProc(HWND hwnd);
	// Restores the previous window procedure, only if ours is still the current one.
	void		RemoveWndProc();
	bool		WndProcInstalled();
	// Called when the menu opens or closes.
	void		OnToggled(bool open);

	// Key capture (Keybinds page). BeginKeyCapture takes the next key pressed from the DirectInput snapshot (a scancode
	// like the key settings hold). While it is active, and until the keys are released afterwards, the menu hotkeys
	// do not fire. KeyCaptureActive stays true until PollCapturedKey has delivered the result, so the Escape that
	// cancels a capture does not also close the menu.
	void		BeginKeyCapture();
	void		CancelKeyCapture();
	bool		KeyCaptureActive();
	int			PollCapturedKey();	// -1 = nothing yet, else the scancode (1 = Escape: the caller treats it as cancel)
}
