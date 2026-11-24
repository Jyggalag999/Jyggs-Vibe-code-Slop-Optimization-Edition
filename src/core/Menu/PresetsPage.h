#pragma once

#include <string>

// The "Presets" page of the settings menu: create, edit, save and delete the Cartographer-format preset INIs
// that Locations uses, see where a preset is used and jump to those locations.
namespace PresetsPage {
	// Draws the page body (inside the content child window).
	void		Draw();
	// Number of files with edits that are not saved yet.
	int			UnsavedCount();
	// Selects a preset of a feature (used by the preview program); false if it does not exist.
	bool		SelectPreset(int feature, const char* name);
	// Selects a file (feature ini, Settings.ini or a preset) for editing; feature -1 = Settings.ini.
	// Right pane tab: 0 = Values, 1 = Used by (preview program).
	void		ShowSubTab(int tab);
	void		OpenFile(const std::string& path, const std::string& label, int feature, const std::string& preset);
}
