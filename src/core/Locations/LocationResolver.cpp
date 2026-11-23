#include "LocationResolver.h"

namespace Locations {

static std::vector<std::string> SplitComma(const std::string& s) {
	std::vector<std::string> out;
	if (s.empty()) return out;
	size_t pos = 0;
	while (true) {
		size_t f = s.find(',', pos);
		if (f == std::string::npos) { out.push_back(s.substr(pos)); break; }
		out.push_back(s.substr(pos, f - pos));
		pos = f + 1;
	}
	return out;
}

// "A name added again moves to the end."
static void AppendReplace(std::vector<std::string>& stack, const std::string& name) {
	std::string l = Lower(name);
	for (size_t i = 0; i < stack.size(); i++) {
		if (Lower(stack[i]) == l) { stack.erase(stack.begin() + i); break; }
	}
	stack.push_back(name);
}

Resolution Resolve(const LocationConfig& cfg, Feature feature, const std::string& target, bool interior) {
	Resolution r;
	r.feature = feature;
	r.target = target;
	r.interior = interior;
	const FeatureConfig& fc = cfg.Features[feature];

	if (!fc.Active()) { r.reason = "feature off"; return r; }
	if (target.empty()) { r.reason = "target has no editor ID"; return r; }

	// Default preset: NVR and GameSettings pick Interior/Exterior, Lighting and Fog use DefaultPreset.
	const std::string* def = nullptr;
	if (feature == FeatureNVR || feature == FeatureGameSettings) def = fc.Ini.Get("Main", interior ? "Interior" : "Exterior");
	else def = fc.Ini.Get("Main", "DefaultPreset");
	if (!def || def->empty()) { r.reason = "no default preset set"; return r; }
	auto defIt = fc.Presets.find(Lower(*def));
	if (defIt == fc.Presets.end()) { r.reason = "default preset '" + *def + "' does not exist"; return r; }
	r.defaultPreset = defIt->second.name;

	// CRTBuildPresets: default, alias presets, keyword presets, overrides.
	std::vector<std::string> stack;
	stack.push_back(defIt->second.name);

	for (auto& a : cfg.Aliases) {
		if (!cfg.HasKeyword(target, Lower(a.name))) continue;
		r.keywords.push_back(a.name);
		for (auto& t : a.targets) {
			auto it = fc.Presets.find(Lower(t));
			if (it != fc.Presets.end()) AppendReplace(stack, it->second.name);
		}
	}
	for (auto& kw : fc.KeywordOrder) {
		if (!cfg.HasKeyword(target, kw)) continue;
		const Preset& p = fc.Presets.at(kw);
		r.keywords.push_back(p.name);
		AppendReplace(stack, p.name);
	}
	if (const std::string* ov = fc.Ini.Get("Overrides", target)) {
		for (auto& t : SplitComma(*ov)) {
			auto it = fc.Presets.find(Lower(t));
			if (it != fc.Presets.end()) AppendReplace(stack, it->second.name);
		}
	}
	r.stack = stack;

	// Values: only keys of the default preset count; the first non-empty value searching from the end wins.
	std::vector<const Preset*> presets;
	for (auto& n : stack) presets.push_back(&fc.Presets.at(Lower(n)));
	std::unordered_set<std::string> seenSections;
	for (auto& sec : defIt->second.ini.sections) {
		std::string sl = Lower(sec.name);
		if (!seenSections.insert(sl).second) continue;
		std::unordered_set<std::string> seenKeys;
		for (auto& e : sec.entries) {
			std::string kl = Lower(e.key);
			if (!seenKeys.insert(kl).second) continue;

			ResolvedValue v;
			v.section = sec.name;
			v.key = e.key;
			v.value = e.value;
			v.source = defIt->second.name;
			for (size_t i = presets.size(); i-- > 0;) {
				const std::string* o = presets[i]->ini.Get(sec.name, e.key);
				if (o && !o->empty()) { v.value = *o; v.source = presets[i]->name; break; }
			}
			r.values.push_back(v);
		}
	}
	r.valid = true;
	return r;
}

}
