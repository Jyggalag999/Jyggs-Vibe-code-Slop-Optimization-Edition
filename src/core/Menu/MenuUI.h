#pragma once

namespace MenuUI {
	// Draws the settings window for one frame (the menu is open and an ImGui frame has been started).
	void		Draw();
	// Called when the menu opens: rebuilds the sidebar and the cached values.
	void		OnOpened();
	// Saves the settings file (also used by the KeySave hotkey).
	void		Save();
}
