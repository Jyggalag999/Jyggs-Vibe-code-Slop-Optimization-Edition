#pragma once

// The "Presets" page of the settings menu: create, edit, save and delete the Cartographer-format preset INIs
// that Locations uses, see where a preset is used and jump to those locations.
namespace PresetsPage {
	// Draws the page body (inside the content child window).
	void		Draw();
	// Number of files with edits that are not saved yet.
	int			UnsavedCount();
}
