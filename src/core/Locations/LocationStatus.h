#pragma once
// What the Locations feature resolved for the current location. Engine-free: shared with the menu preview.

#include "LocationResolver.h"

// Read by the menu's Locations tab (later build). Only valid on the main thread.
struct LocationStatus {
	bool Enabled = false;				// Locations is running
	bool CartographerEsp = false;		// Cartographer.esp is loaded, so Locations stays off
	std::string Warning;				// text to show the user, empty if none
	std::string Target;					// editor ID of the cell (interior) or worldspace (exterior) the presets were chosen for
	bool Interior = false;
	std::vector<std::string> Keywords;	// keywords of the target that matched a preset or alias, all features together
	Locations::Resolution Features[Locations::FeatureCount];	// per feature: preset stack and, for each key, the preset the value came from
	bool Applied[Locations::FeatureCount] = {};					// the feature wrote something for the current target
	std::string LightingNote;			// why interior lighting is not applied (excluded cell, behave as exterior...), empty if applied
	unsigned ConfigLoadMs = 0;
};
