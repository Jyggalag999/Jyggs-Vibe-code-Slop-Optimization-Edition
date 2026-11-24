#pragma once

// The Locations window (own hotkey KeyLocations): the preset manager and the current location status.
namespace LocationsWindow {
	// Draws the window for one frame (the menu is open and an ImGui frame has been started).
	void		Draw();
	// Called when the window opens.
	void		OnOpened();
	// Opens a tab on the next frame: 0 = Presets, 1 = Current location (used by the preview program).
	void		ShowTab(int tab);
}
