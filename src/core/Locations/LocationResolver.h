#pragma once
/*
* Locations: preset resolver. Reproduces Cartographer's CRTBuildPresets and the swappers' value lookup.
* No engine dependencies.
*/

#include "LocationConfig.h"

namespace Locations {

struct ResolvedValue {
	std::string section;	// as written in the default preset
	std::string key;
	std::string value;		// final value, may be empty
	std::string source;		// preset the value came from
};

struct Resolution {
	Feature feature = FeatureNVR;
	bool valid = false;						// false: the feature does nothing for this target
	std::string reason;						// why not valid
	std::string target;						// editor ID the presets were chosen for
	bool interior = false;
	std::string defaultPreset;
	std::vector<std::string> keywords;		// aliases and preset names the target has
	std::vector<std::string> stack;			// preset names, in order; later wins
	std::vector<ResolvedValue> values;
};

// target: editor ID of the cell (interior) or worldspace (exterior).
Resolution Resolve(const LocationConfig& cfg, Feature feature, const std::string& target, bool interior);

}
