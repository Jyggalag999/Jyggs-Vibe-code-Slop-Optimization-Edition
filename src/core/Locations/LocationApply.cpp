#include "../CellProfiler.h"
#include "LocationApply.h"
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <chrono>

using namespace Locations;

LocationManager* TheLocationManager = nullptr;

namespace {

// Engine addresses (checked with _AgentKit; no bytes are patched for any of them, they are only called).
const UInt32 kGetCellImageSpace = 0x00547650;		// TESObjectCELL::GetImageSpace
const UInt32 kSetCellImageSpace = 0x00547750;		// TESObjectCELL::SetImageSpace (null removes the cell's image space)
const UInt32 kSetImageSpaceParameters = 0x00B4F430;	// BSShaderManager::SetImageSpaceParameters(float* traits), cdecl
const UInt32 kGetUsableImageSpace = 0x00547680;	// TESObjectCELL::GetUsableImageSpace (the cell's own, else the default interior image space 0x160)
const UInt32 kImageSpaceMakeDefault = 0x0052B780;	// TESImageSpace::MakeDefault (the game's default trait values), thiscall
const UInt32 kGameSettingCollection = 0x011C8048;	// GameSettingCollection** (singleton)
const UInt32 kGetGameSetting = 0x004F8A90;			// GameSettingCollection::GetSetting(const char* name, Setting** out)
const UInt32 kFormHeapAllocate = 0x00401000;
const UInt8  kFormTypeImageSpace = 0x53;			// FormTypeID values of TESDataHandler::CreateFormFromID
const UInt8  kFormTypeLightingTemplate = 0x65;
const UInt32 kInheritAll = 0x1FF;					// cell inherit flags, bits 0-8: use the lighting template

// TESImageSpace: float fTraitValues[33] right after TESForm (0x18).
inline float* ImageTraits(TESImageSpace* apForm) { return (float*)((UInt8*)apForm + 0x18); }

// BGSLightingTemplate::Data (INTERIOR_DATA) right after TESForm (0x18).
struct LightingData {
	UInt8	Ambient[4];
	UInt8	Directional[4];
	UInt8	Fog[4];
	float	FogNear;
	float	FogFar;
	UInt32	DirectionalXY;
	UInt32	DirectionalZ;
	float	DirectionalFade;
	float	FogClip;
	float	FogPower;
};
inline LightingData* TemplateData(BGSLightingTemplate* apForm) { return (LightingData*)((UInt8*)apForm + 0x18); }

inline UInt8 ToByte(float afValue) { return (UInt8)(afValue < 0.0f ? 0.0f : (afValue > 255.0f ? 255.0f : afValue)); }

// Trait ids are the ones JIP / JohnnyGuitar use (from CRTLightingSwapper's trait map).
float GetTemplateTrait(BGSLightingTemplate* apForm, int aiId) {
	LightingData* d = TemplateData(apForm);
	switch (aiId) {
	case 1: case 2: case 3: return d->Ambient[aiId - 1];
	case 4: case 5: case 6: return d->Directional[aiId - 4];
	case 7: case 8: case 9: return d->Fog[aiId - 7];
	case 10: return d->FogNear;
	case 11: return d->FogFar;
	case 12: return (float)d->DirectionalXY;
	case 13: return (float)d->DirectionalZ;
	case 14: return d->DirectionalFade;
	case 15: return d->FogClip;
	case 16: return d->FogPower;
	}
	return 0.0f;
}

void SetTemplateTrait(BGSLightingTemplate* apForm, int aiId, float afValue) {
	LightingData* d = TemplateData(apForm);
	switch (aiId) {
	case 1: case 2: case 3: d->Ambient[aiId - 1] = ToByte(afValue); break;
	case 4: case 5: case 6: d->Directional[aiId - 4] = ToByte(afValue); break;
	case 7: case 8: case 9: d->Fog[aiId - 7] = ToByte(afValue); break;
	case 10: d->FogNear = afValue; break;
	case 11: d->FogFar = afValue; break;
	case 12: d->DirectionalXY = (UInt32)afValue; break;
	case 13: d->DirectionalZ = (UInt32)afValue; break;
	case 14: d->DirectionalFade = afValue; break;
	case 15: d->FogClip = afValue; break;
	case 16: d->FogPower = afValue; break;
	}
}

enum TraitKind { KindImage, KindTemplate, KindDay, KindSunset, KindNight, KindSunrise, KindEnabled };
struct TraitRef { TraitKind Kind; int Id; };

// "section:key" (lowercase) -> trait. Straight from CRTLightingSwapper's aTraitMap.
const std::unordered_map<std::string, TraitRef>& TraitMap() {
	static std::unordered_map<std::string, TraitRef> map;
	if (!map.empty()) return map;
	struct Item { const char* Key; TraitKind Kind; int Id; };
	static const Item items[] = {
		{"ImageSpace.HDR:EyeAdapt", KindImage, 0}, {"ImageSpace.HDR:BlurRadius", KindImage, 1}, {"ImageSpace.HDR:BlurPasses", KindImage, 2},
		{"ImageSpace.HDR:EmissiveMult", KindImage, 3}, {"ImageSpace.HDR:TargetLUM", KindImage, 4}, {"ImageSpace.HDR:UpperLUMClamp", KindImage, 5},
		{"ImageSpace.HDR:BrightScale", KindImage, 6}, {"ImageSpace.HDR:BrightClamp", KindImage, 7}, {"ImageSpace.HDR:LUMRampNoTex", KindImage, 8},
		{"ImageSpace.HDR:LUMRampMin", KindImage, 9}, {"ImageSpace.HDR:LUMRampMax", KindImage, 10}, {"ImageSpace.HDR:SunlightDimmer", KindImage, 11},
		{"ImageSpace.HDR:GrassDimmer", KindImage, 12}, {"ImageSpace.HDR:TreeDimmer", KindImage, 13}, {"ImageSpace.HDR:SkinDimmer", KindImage, 14},
		{"ImageSpace.Bloom:BlurRadius", KindImage, 15}, {"ImageSpace.Bloom:AlphaMultInterior", KindImage, 16}, {"ImageSpace.Bloom:AlphaMultExterior", KindImage, 17},
		{"ImageSpace.GetHit:BlurRadius", KindImage, 18}, {"ImageSpace.GetHit:BlurDampingContrast", KindImage, 19}, {"ImageSpace.GetHit:DampingContrast", KindImage, 20},
		{"ImageSpace.NightEye:TintR", KindImage, 21}, {"ImageSpace.NightEye:TintG", KindImage, 22}, {"ImageSpace.NightEye:TintB", KindImage, 23},
		{"ImageSpace.NightEye:Brightness", KindImage, 24},
		{"ImageSpace.Cinematic:Saturation", KindImage, 25}, {"ImageSpace.Cinematic:ContrastAvgLum", KindImage, 26}, {"ImageSpace.Cinematic:Contrast", KindImage, 27},
		{"ImageSpace.Cinematic:Brightness", KindImage, 28}, {"ImageSpace.Cinematic:TintR", KindImage, 29}, {"ImageSpace.Cinematic:TintG", KindImage, 30},
		{"ImageSpace.Cinematic:TintB", KindImage, 31}, {"ImageSpace.Cinematic:TintPower", KindImage, 32},
		{"LightingTemplate.Ambient:R", KindTemplate, 1}, {"LightingTemplate.Ambient:G", KindTemplate, 2}, {"LightingTemplate.Ambient:B", KindTemplate, 3},
		{"LightingTemplate.Directional:R", KindTemplate, 4}, {"LightingTemplate.Directional:G", KindTemplate, 5}, {"LightingTemplate.Directional:B", KindTemplate, 6},
		{"LightingTemplate.Directional:RotationXY", KindTemplate, 12}, {"LightingTemplate.Directional:RotationZ", KindTemplate, 13},
		{"LightingTemplate.Directional:Fade", KindTemplate, 14},
		{"LightingTemplate.Fog:R", KindTemplate, 7}, {"LightingTemplate.Fog:G", KindTemplate, 8}, {"LightingTemplate.Fog:B", KindTemplate, 9},
		{"LightingTemplate.Fog:Near", KindTemplate, 10}, {"LightingTemplate.Fog:Far", KindTemplate, 11}, {"LightingTemplate.Fog:Power", KindTemplate, 16},
		{"LightingTemplate.Fog:ClipDistance", KindTemplate, 15},
		{"Ambient.Day:R", KindDay, 0}, {"Ambient.Day:G", KindDay, 1}, {"Ambient.Day:B", KindDay, 2},
		{"Ambient.Sunset:R", KindSunset, 0}, {"Ambient.Sunset:G", KindSunset, 1}, {"Ambient.Sunset:B", KindSunset, 2},
		{"Ambient.Sunset:FadeIn", KindSunset, 3}, {"Ambient.Sunset:FadeOut", KindSunset, 4},
		{"Ambient.Night:R", KindNight, 0}, {"Ambient.Night:G", KindNight, 1}, {"Ambient.Night:B", KindNight, 2},
		{"Ambient.Sunrise:R", KindSunrise, 0}, {"Ambient.Sunrise:G", KindSunrise, 1}, {"Ambient.Sunrise:B", KindSunrise, 2},
		{"Ambient.Sunrise:FadeIn", KindSunrise, 3}, {"Ambient.Sunrise:FadeOut", KindSunrise, 4},
		{"Ambient:Enabled", KindEnabled, 0},
	};
	for (const Item& i : items) map[Lower(i.Key)] = { i.Kind, i.Id };
	return map;
}

// "true" / "false" / a number (like the NVR swapper). False if the text is none of those.
bool ParseValue(const std::string& asText, float& arOut) {
	std::string l = Lower(asText);
	if (l == "true") { arOut = 1.0f; return true; }
	if (l == "false") { arOut = 0.0f; return true; }
	if (l.empty()) return false;
	char* end = nullptr;
	double v = strtod(l.c_str(), &end);
	if (end == l.c_str() || *end != 0) return false;
	arOut = (float)v;
	return true;
}

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

bool SameStack(const std::vector<std::string>& a, const std::vector<std::string>& b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) if (Lower(a[i]) != Lower(b[i])) return false;
	return true;
}

}

static NVSEConsoleInterface* gConsole = nullptr;

void LocationManager::SetConsoleInterface(NVSEConsoleInterface* apConsole) {
	gConsole = apConsole;
}

bool LocationManager::CanGoTo() {
	return gConsole && gConsole->RunScriptLine;
}

/*
* Interior cells and worldspaces of the load order by editor ID, built once (the load order does not change at runtime).
* The plugin is the file whose mod index is the top byte of the form ID ("?" for 0xFF / unknown indexes).
*/
void LocationManager::BuildInstalledIndex() {
	InstalledBuilt = true;
	if (!DataHandler) return;
	auto timer = TimeLogger();

	std::unordered_map<UInt8, std::string> plugins;
	for (TList<ModInfo>::Iterator Itr = DataHandler->modList.modInfoList.Begin(); !Itr.End() && Itr.Get(); ++Itr) {
		if (Itr.Get()->modIndex != 0xFF) plugins[Itr.Get()->modIndex] = Itr.Get()->name;
	}
	auto pluginOf = [&](TESForm* apForm) -> std::string {
		auto it = plugins.find((UInt8)(apForm->refID >> 24));
		return it != plugins.end() ? it->second : "?";
	};

	unsigned interiors = 0, worldspaces = 0;
	NiTArray<TESObjectCELL*>& cells = DataHandler->cellArray;
	for (UInt16 i = 0; i < cells.end && cells.data; i++) {
		TESObjectCELL* cell = cells.data[i];
		if (!cell || !cell->IsInterior()) continue;
		const char* name = ((TESForm*)cell)->GetEditorName();
		if (!name || !*name) continue;
		InstalledInfo info;
		info.Form = cell;
		info.Plugin = pluginOf((TESForm*)cell);
		if (Installed.emplace(Lower(name), info).second) interiors++;
	}
	for (TList<TESWorldSpace>::Iterator Itr = DataHandler->worldSpaceList.Begin(); !Itr.End() && Itr.Get(); ++Itr) {
		TESForm* ws = (TESForm*)Itr.Get();
		const char* name = ws->GetEditorName();
		if (!name || !*name) continue;
		InstalledInfo info;
		info.Form = ws;
		info.Worldspace = true;
		info.Plugin = pluginOf(ws);
		if (Installed.emplace(Lower(name), info).second) worldspaces++;
	}
	Logger::Log("[Locations] installed index: %u interiors, %u worldspaces, %u ms", interiors, worldspaces, (unsigned)timer.LogTime("LocationManager::InstalledIndex"));
}

const LocationManager::InstalledInfo* LocationManager::FindInstalled(const std::string& asEditorId) {
	if (!EnsureInit()) return nullptr;
	if (!InstalledBuilt) BuildInstalledIndex();
	auto it = Installed.find(Lower(asEditorId));
	return it != Installed.end() ? &it->second : nullptr;
}

bool LocationManager::IsWorldSpace(const char* asEditorId) {
	const InstalledInfo* info = FindInstalled(asEditorId);
	return info && info->Worldspace;
}

bool LocationManager::RequestGoTo(const std::string& asEditorId) {
	if (!CanGoTo()) { GoResult = "NVSE console interface not available"; return false; }
	bool bad = asEditorId.empty();
	for (char c : asEditorId) if ((unsigned char)c <= ' ' || c == ';' || c == '"') bad = true;
	if (bad) { GoResult = "invalid editor ID"; return false; }
	PendingGo = asEditorId;
	GoFrames = 0;
	GoResult = "queued: " + asEditorId;
	return true;
}

// A cell inside the worldspace to land in: the middle of its world map's usable area (cow 0 0 is often void or
// empty in mod and DLC worldspaces), else the middle of its cell bounds, else 0,0.
static const char* WorldspaceEntryCell(TESWorldSpace* ws, int& x, int& y) {
	x = 0; y = 0;
	if (!ws) return "no data, 0,0";
	const TESWorldSpace::MapData& m = ws->mapData;
	if (m.cellNWCoordinates.X != 0 || m.cellNWCoordinates.Y != 0 || m.cellSECoordinates.X != 0 || m.cellSECoordinates.Y != 0) {
		x = ((int)m.cellNWCoordinates.X + (int)m.cellSECoordinates.X) / 2;
		y = ((int)m.cellNWCoordinates.Y + (int)m.cellSECoordinates.Y) / 2;
		return "middle of the map's usable area";
	}
	if (ws->min.X != 0.0f || ws->min.Y != 0.0f || ws->max.X != 0.0f || ws->max.Y != 0.0f) {
		// NAM0/NAM9 are world units; a cell is 4096 units
		x = (int)floorf((ws->min.X + ws->max.X) * 0.5f / 4096.0f);
		y = (int)floorf((ws->min.Y + ws->max.Y) * 0.5f / 4096.0f);
		return "middle of the cell bounds";
	}
	return "no data, 0,0";
}

void LocationManager::RunPendingGo() {
	if (PendingGo.empty()) return;
	if (!CanGoTo() || !Player || !Player->parentCell || !InterfaceManager || !InterfaceManager->IsActive(Menu::MenuType::kMenuType_None)) return;
	if (++GoFrames < 3) return;	// let the NVR menu finish closing
	std::string id = PendingGo;
	PendingGo.clear();
	char cmd[512];
	bool ok;
	const InstalledInfo* info = FindInstalled(id);
	if (info) {	// known: pick the command directly
		if (info->Worldspace) {
			int x = 0, y = 0;
			const char* from = WorldspaceEntryCell((TESWorldSpace*)info->Form, x, y);
			Logger::Log("[Presets] worldspace '%s': entry cell %d,%d (%s)", id.c_str(), x, y, from);
			snprintf(cmd, sizeof(cmd), "cow %s %d %d", id.c_str(), x, y);
		}
		else snprintf(cmd, sizeof(cmd), "coc %s", id.c_str());
		ok = gConsole->RunScriptLine(cmd, nullptr);
	} else {	// not in the index: coc, then cow
		snprintf(cmd, sizeof(cmd), "coc %s", id.c_str());
		ok = gConsole->RunScriptLine(cmd, nullptr);
		if (!ok) {	// not a cell: try it as a worldspace
			Logger::Log("[Presets] '%s' failed, trying cow", cmd);
			snprintf(cmd, sizeof(cmd), "cow %s 0 0", id.c_str());
			ok = gConsole->RunScriptLine(cmd, nullptr);
		}
	}
	Logger::Log("[Presets] ran '%s' -> %s", cmd, ok ? "ok" : "failed");
	GoResult = std::string(cmd) + (ok ? " (sent)" : " (failed)");
}

void LocationManager::Initialize() {
	Logger::Log("Starting the Locations manager...");
	TheLocationManager = new LocationManager();
}

void LocationManager::Log(const char* aMessage, ...) {
	if (!TheSettingManager->SettingsMain.Locations.DebugLog) return;
	char buffer[1024];
	va_list args;
	va_start(args, aMessage);
	vsnprintf(buffer, sizeof(buffer), aMessage, args);
	va_end(args);
	Logger::Log("[Locations] %s", buffer);
}

/*
* Runs once, when the game data is there (first frame or first PositionPlayer call).
*/
bool LocationManager::EnsureInit() {
	if (Initialized) return true;
	if (InitFailed || !DataHandler || !Player || !Tes) return false;

	auto timer = TimeLogger();
	Initialized = true;

	for (TList<ModInfo>::Iterator Itr = DataHandler->modList.modInfoList.Begin(); !Itr.End() && Itr.Get(); ++Itr) {
		if (!_stricmp(Itr.Get()->name, "Cartographer.esp")) { Status.CartographerEsp = true; break; }
	}
	if (Status.CartographerEsp) {
		Status.Warning = "Cartographer.esp is loaded. Locations stays off so the two don't fight. Disable Cartographer.esp to use the built-in version.";
		Logger::Log("[Locations] %s", Status.Warning.c_str());
		return true;
	}

	Config.Load("Data", [](const std::string& asMessage) { Logger::Log("[Locations] %s", asMessage.c_str()); });
	Status.ConfigLoadMs = (unsigned)timer.LogTime("LocationManager::Init");
	Logger::Log("[Locations] Ready (%u ms).", Status.ConfigLoadMs);

	ImageSpace = (TESImageSpace*)DataHandler->CreateForm(kFormTypeImageSpace);
	Template = (BGSLightingTemplate*)DataHandler->CreateForm(kFormTypeLightingTemplate);
	if (!ImageSpace || !Template) {
		Logger::Log("[Locations] Could not create the image space / lighting template, interior lighting is off.");
		ImageSpace = nullptr;
		Template = nullptr;
	}
	return true;
}

bool LocationManager::Active() {
	if (!EnsureInit()) return false;
	Status.Enabled = !Status.CartographerEsp && TheSettingManager->SettingsMain.Locations.Enabled;
	return Status.Enabled;
}

void* LocationManager::TargetForm(TESObjectCELL* apCell) {
	if (!apCell) return nullptr;
	return apCell->IsInterior() ? (void*)apCell : (void*)apCell->worldSpace;
}

LocationManager::Target LocationManager::GetTarget(TESObjectCELL* apCell) {
	Target t;
	t.Form = TargetForm(apCell);
	if (!t.Form) return t;
	t.Interior = apCell->IsInterior();
	t.BehaveAsExterior = t.Interior && (apCell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior);
	const char* name = ((TESForm*)t.Form)->GetEditorName();
	t.Name = name ? name : "";
	return t;
}

/* ---------------------------------------------------------------- entry points */

void LocationManager::OnDestination(TESObjectCELL* apCell) {
	if (!Active()) return;
	void* form = TargetForm(apCell);
	if (!form || form == LastTarget) return;
	HookFrom = LastTarget;
	Apply(apCell, true);
	HookTarget = form;
	HookTick = GetTickCount64();
}

void LocationManager::Update() {
	if (!Active()) {
		if (LightCell) RestoreLighting();
		return;
	}
	TESObjectCELL* cell = Player ? Player->parentCell : nullptr;
	if (!cell) return;

	// Fallback: the location changed without going through the PositionPlayer hooks.
	void* form = TargetForm(cell);
	if (HookTarget) {
		// The hook already applied the destination. The player is still in the old location until the load
		// finishes; don't switch back to it (and then forward again). Give up after a minute (load cancelled).
		const bool waiting = form == HookFrom && GetTickCount64() - HookTick < 60000;
		if (form == HookTarget || !waiting) HookTarget = HookFrom = nullptr;
		if (waiting) form = nullptr;
	}
	if (form && form != LastTarget) {
		Log("location changed without a hook, applying now");
		Apply(cell, true);
	}

	unsigned long long now = GetTickCount64();
	float dt = LastTick ? (now - LastTick) / 1000.0f : 0.0f;
	LastTick = now;

	if (Blending) {
		float seconds = TheSettingManager->SettingsMain.Locations.BlendSeconds;
		BlendT += seconds > 0.001f ? dt / seconds : 1.0f;
		if (BlendT >= 1.0f) { BlendT = 1.0f; Blending = false; }
		StepBlend();
	}

	UpdateAmbientCycle();

	if (RefreshPending && LightCell == cell) {
		RefreshPending = false;
		RefreshImageSpace();
	}
}

void LocationManager::Reload() {
	if (!Active()) return;
	Log("reload");
	Config.Load("Data", [](const std::string& asMessage) { Logger::Log("[Locations] %s", asMessage.c_str()); });
	for (int i = 0; i < FeatureCount; i++) HaveLastStack[i] = false;
	LastTarget = nullptr;
	HookTarget = HookFrom = nullptr;
	TESObjectCELL* cell = Player ? Player->parentCell : nullptr;
	if (cell) Apply(cell, false);
}

/* ---------------------------------------------------------------- applying */

void LocationManager::Apply(TESObjectCELL* apCell, bool abInstant) {
	Target t = GetTarget(apCell);
	if (!t.Form) return;
	auto timer = TimeLogger();

	LastTarget = t.Form;
	Status.Target = t.Name;
	Status.Interior = t.Interior;
	Blending = !abInstant && TheSettingManager->SettingsMain.Locations.BlendSeconds > 0.0f;
	BlendT = Blending ? 0.0f : 1.0f;
	NvrBlends.clear();
	Log("destination %s (%s)", t.Name.c_str(), t.Interior ? "interior" : "exterior");

	// Per-phase times, so the next slow door can be pinned on one feature.
	using Clock = std::chrono::steady_clock;
	auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<float, std::milli>(b - a).count(); };
	auto t0 = Clock::now();
	ApplyNVR(t, abInstant);
	auto t1 = Clock::now();
	ApplyLighting(apCell, t, abInstant);
	auto t2 = Clock::now();
	ApplyFog(t, abInstant);
	auto t3 = Clock::now();
	ApplyGameSettings(t);
	auto t4 = Clock::now();
	Log("phases: NVR %.2f ms, Lighting %.2f ms, Fog %.2f ms, GameSettings %.2f ms", ms(t0, t1), ms(t1, t2), ms(t2, t3), ms(t3, t4));

	Status.Keywords.clear();
	for (int i = 0; i < FeatureCount; i++)
		for (auto& k : Status.Features[i].keywords)
			if (std::find(Status.Keywords.begin(), Status.Keywords.end(), k) == Status.Keywords.end()) Status.Keywords.push_back(k);

	RefreshPending = true;
	float applyMs = timer.LogTime("Locations::Apply");
	Log("applied in %.2f ms", applyMs);
	int applied = 0;
	for (int i = 0; i < FeatureCount; i++) if (Status.Applied[i]) applied += (int)Status.Features[i].values.size();
	CellProfiler::OnLocationsApply(applyMs, applied);
}

/*
* Resolves a feature for the target and stores it in the status. Returns false if there is nothing to apply
* (feature off, no default preset) or the preset stack is the same as the last one applied.
*/
static bool ResolveFeature(LocationConfig& aConfig, LocationStatus& aStatus, Feature aeFeature, const std::string& asTarget, bool abInterior,
	std::vector<std::string>& arLastStack, bool& arHaveLast, bool abSkipSame) {
	Resolution r = Resolve(aConfig, aeFeature, asTarget, abInterior);
	aStatus.Features[aeFeature] = r;
	aStatus.Applied[aeFeature] = false;
	if (!r.valid) { arHaveLast = false; return false; }
	if (abSkipSame && arHaveLast && SameStack(arLastStack, r.stack)) return false;
	arLastStack = r.stack;
	arHaveLast = true;
	return true;
}

void LocationManager::ApplyNVR(const Target& aTarget, bool abInstant) {
	if (!ResolveFeature(Config, Status, FeatureNVR, aTarget.Name, aTarget.Interior, LastStack[FeatureNVR], HaveLastStack[FeatureNVR], true)) {
		Log("NVR: %s", Status.Features[FeatureNVR].valid ? "same presets, nothing to do" : Status.Features[FeatureNVR].reason.c_str());
		return;
	}
	const Resolution& r = Status.Features[FeatureNVR];
	std::vector<SettingChange> changes;
	for (const ResolvedValue& v : r.values) {
		float value;
		if (!ParseValue(v.value, value)) continue;
		int type = TheSettingManager->GetSettingType(v.section.c_str(), v.key.c_str());
		if (type < 0) continue;	// not an NVR setting
		if (!PresetOverrideLogged && TheSettingManager->IsPresetOverridden(v.section.c_str(), v.key.c_str())) {
			PresetOverrideLogged = true;
			Log("NVR: %s.%s is also set by the Performance preset; the preset wins while it is on", v.section.c_str(), v.key.c_str());
		}
		float current = TheSettingManager->GetSettingF(v.section.c_str(), v.key.c_str());
		if (current == value) continue;
		if (Blending && type == SettingManager::Configuration::NodeType::Float) NvrBlends.push_back({ v.section, v.key, current, value });
		else changes.push_back({ v.section, v.key, value });
	}
	UInt32 count = TheSettingManager->SetSettingBatch(changes);
	Status.Applied[FeatureNVR] = true;
	Log("NVR: %zu preset(s), %u value(s) set, %zu blending", r.stack.size(), count, NvrBlends.size());
}

void LocationManager::ApplyLighting(TESObjectCELL* apCell, const Target& aTarget, bool abInstant) {
	Status.LightingNote.clear();
	Status.Applied[FeatureLighting] = false;
	const FeatureConfig& fc = Config.Features[FeatureLighting];

	const char* skip = nullptr;
	if (!fc.Active()) skip = "off";
	else if (!ImageSpace || !Template) skip = "forms could not be created";
	else if (!aTarget.Interior) skip = "exterior";
	else if (aTarget.BehaveAsExterior) skip = "cell behaves as exterior";
	else if (std::find(fc.Excludes.begin(), fc.Excludes.end(), Lower(aTarget.Name)) != fc.Excludes.end()) skip = "cell is excluded";
	if (skip) {
		RestoreLighting();
		Status.LightingNote = skip;
		Status.Features[FeatureLighting] = Resolution();
		Log("Lighting: %s", skip);
		return;
	}

	Resolution r = Resolve(Config, FeatureLighting, aTarget.Name, aTarget.Interior);
	Status.Features[FeatureLighting] = r;
	if (!r.valid) {
		RestoreLighting();
		Status.LightingNote = r.reason;
		Log("Lighting: %s", r.reason.c_str());
		return;
	}

	// Point only this cell at our forms; remember what it had.
	if (LightCell != apCell) {
		RestoreLighting();
		SavedImageSpace = (void*)ThisCall(kGetCellImageSpace, apCell);
		SavedTemplate = apCell->lightingTemplate;
		SavedInherit = apCell->inheritFlags;
		// Seed our forms with what the cell shows now, so keys the preset does not set keep the cell's values.
		// Forms from CreateForm start with every value at 0 (Cinematic Saturation 0 = black and white interior).
		// Called before the cell is pointed at our image space, so this is the cell's own or the default interior one.
		TESImageSpace* usable = (TESImageSpace*)ThisCall(kGetUsableImageSpace, apCell);
		if (usable && usable != ImageSpace) memcpy(ImageTraits(ImageSpace), ImageTraits(usable), 33 * sizeof(float));
		else ThisCall(kImageSpaceMakeDefault, ImageSpace);
		// Lighting: per field, like TESObjectCELL::GetDataAmbient / GetFogNear etc.: the template's value when its inherit bit is set
		// and the cell has a template, else the cell's own INTERIOR_DATA. Bits 0x1..0x100 = ambient, directional, fog color, fog near,
		// fog far, directional rotation (XY and Z), directional fade, clip distance, fog power.
		LightingData* own = (LightingData*)apCell->lighting;
		LightingData* tmpl = SavedTemplate ? TemplateData((BGSLightingTemplate*)SavedTemplate) : nullptr;
		auto from = [&](unsigned aiBit) { return (tmpl && (SavedInherit & aiBit)) ? tmpl : own; };
		LightingData* dst = TemplateData(Template);
		if (LightingData* s = from(0x1))   memcpy(dst->Ambient, s->Ambient, 4);
		if (LightingData* s = from(0x2))   memcpy(dst->Directional, s->Directional, 4);
		if (LightingData* s = from(0x4))   memcpy(dst->Fog, s->Fog, 4);
		if (LightingData* s = from(0x8))   dst->FogNear = s->FogNear;
		if (LightingData* s = from(0x10))  dst->FogFar = s->FogFar;
		if (LightingData* s = from(0x20))  { dst->DirectionalXY = s->DirectionalXY; dst->DirectionalZ = s->DirectionalZ; }
		if (LightingData* s = from(0x40))  dst->DirectionalFade = s->DirectionalFade;
		if (LightingData* s = from(0x80))  dst->FogClip = s->FogClip;
		if (LightingData* s = from(0x100)) dst->FogPower = s->FogPower;
		ThisCall(kSetCellImageSpace, apCell, ImageSpace);
		apCell->lightingTemplate = Template;
		apCell->inheritFlags |= kInheritAll;
		LightCell = apCell;
	}

	// Current values are the blend start; the targets start as the current values so keys the preset doesn't set stay as they are.
	float* traits = ImageTraits(ImageSpace);
	for (int i = 0; i < 33; i++) ImgFrom[i] = ImgTo[i] = traits[i];
	for (int i = 1; i <= 16; i++) TmplFrom[i] = TmplTo[i] = GetTemplateTrait(Template, i);

	bool cycle = false;
	const auto& map = TraitMap();
	for (const ResolvedValue& v : r.values) {
		auto it = map.find(Lower(v.section + ":" + v.key));
		if (it == map.end()) continue;
		float value;
		if (!ParseValue(v.value, value)) continue;
		const TraitRef& ref = it->second;
		switch (ref.Kind) {
		case KindImage:
			// JIP divides the tint color traits by 255.
			ImgTo[ref.Id] = ((ref.Id >= 21 && ref.Id <= 23) || (ref.Id >= 29 && ref.Id <= 31)) ? value / 255.0f : value;
			break;
		case KindTemplate:	TmplTo[ref.Id] = value; break;
		case KindDay:		Day[ref.Id] = value; break;
		case KindSunset:	Sunset[ref.Id] = value; break;
		case KindNight:		Night[ref.Id] = value; break;
		case KindSunrise:	Sunrise[ref.Id] = value; break;
		case KindEnabled:	cycle = value != 0.0f; break;
		}
	}
	CycleOn = cycle;

	if (!Blending) {
		WriteLightingTargets(1.0f);
	}
	Status.Applied[FeatureLighting] = true;
	Log("Lighting: %zu preset(s), ambient cycle %s", r.stack.size(), cycle ? "on" : "off");
}

void LocationManager::ApplyFog(const Target& aTarget, bool abInstant) {
	Status.Applied[FeatureFog] = false;
	if (aTarget.Interior) {	// the swapper ignores interiors
		Status.Features[FeatureFog] = Resolution();
		return;
	}
	if (!ResolveFeature(Config, Status, FeatureFog, aTarget.Name, false, LastStack[FeatureFog], HaveLastStack[FeatureFog], true)) {
		Log("Fog: %s", Status.Features[FeatureFog].valid ? "same presets, nothing to do" : Status.Features[FeatureFog].reason.c_str());
		return;
	}
	static const char* keys[4] = { "weather.fog:distancedaynear", "weather.fog:distancedayfar", "weather.fog:distancenightnear", "weather.fog:distancenightfar" };
	const Resolution& r = Status.Features[FeatureFog];

	// Weather fog distances are fogDistance[0..3] at 0x0F0 of TESWeather (JIP trait ids 15-18).
	TESWeather* first = nullptr;
	for (TList<TESWeather>::Iterator Itr = DataHandler->weathers.Begin(); !Itr.End() && Itr.Get(); ++Itr) { first = Itr.Get(); break; }
	for (int i = 0; i < 4; i++) {
		FogSet[i] = false;
		FogFrom[i] = first ? ((float*)((UInt8*)first + 0xF0))[i] : 0.0f;
	}
	for (const ResolvedValue& v : r.values) {
		std::string key = Lower(v.section + ":" + v.key);
		for (int i = 0; i < 4; i++) {
			float value;
			if (key == keys[i] && ParseValue(v.value, value)) { FogTo[i] = value; FogSet[i] = true; }
		}
	}
	if (!Blending) WriteFog(1.0f);
	Status.Applied[FeatureFog] = true;
	Log("Fog: %zu preset(s)", r.stack.size());
}

void LocationManager::ApplyGameSettings(const Target& aTarget) {
	Status.Applied[FeatureGameSettings] = false;
	if (!ResolveFeature(Config, Status, FeatureGameSettings, aTarget.Name, aTarget.Interior, LastStack[FeatureGameSettings], HaveLastStack[FeatureGameSettings], true)) {
		Log("GameSettings: %s", Status.Features[FeatureGameSettings].valid ? "same presets, nothing to do" : Status.Features[FeatureGameSettings].reason.c_str());
		return;
	}
	const Resolution& r = Status.Features[FeatureGameSettings];
	void* collection = *(void**)kGameSettingCollection;
	if (!collection) return;
	typedef bool(__thiscall* GetSettingFn)(void*, const char*, void**);
	typedef void* (__cdecl* AllocFn)(size_t);

	UInt32 count = 0;
	for (const ResolvedValue& v : r.values) {
		if (v.key.empty() || v.value.empty()) continue;
		GameSetting* setting = nullptr;
		if (!((GetSettingFn)kGetGameSetting)(collection, v.key.c_str(), (void**)&setting) || !setting) continue;
		char type = setting->Name ? setting->Name[0] : v.key[0];
		float value;
		switch (type) {
		case 'b': case 'i': case 'u': case 'c': case 'f':
			if (!ParseValue(v.value, value)) break;
			if (type == 'f') setting->fValue = value;
			else if (type == 'b') setting->iValue = value != 0.0f ? 1 : 0;
			else setting->iValue = (int)value;
			count++;
			break;
		case 's': case 'S': {
			size_t len = v.value.size();
			char* text = (char*)((AllocFn)kFormHeapAllocate)(len + 1);
			if (!text) break;
			memcpy(text, v.value.c_str(), len + 1);
			setting->pValue = text;	// the old string is left alone, like xNVSE's SetStringSetting
			count++;
			break;
		}
		}
	}
	Status.Applied[FeatureGameSettings] = true;
	Log("GameSettings: %zu preset(s), %u value(s) set", r.stack.size(), count);
}

/* ---------------------------------------------------------------- lighting helpers */

void LocationManager::RestoreLighting() {
	CycleOn = false;
	if (!LightCell) return;
	ThisCall(kSetCellImageSpace, LightCell, SavedImageSpace);
	LightCell->lightingTemplate = (BGSLightingTemplate*)SavedTemplate;
	LightCell->inheritFlags = SavedInherit;
	LightCell = nullptr;
	SavedImageSpace = nullptr;
	SavedTemplate = nullptr;
}

void LocationManager::WriteLightingTargets(float afT) {
	if (!ImageSpace || !Template) return;
	float* traits = ImageTraits(ImageSpace);
	for (int i = 0; i < 33; i++) traits[i] = Lerp(ImgFrom[i], ImgTo[i], afT);
	for (int i = 1; i <= 16; i++) SetTemplateTrait(Template, i, Lerp(TmplFrom[i], TmplTo[i], afT));
}

void LocationManager::WriteFog(float afT) {
	for (TList<TESWeather>::Iterator Itr = DataHandler->weathers.Begin(); !Itr.End() && Itr.Get(); ++Itr) {
		float* fog = (float*)((UInt8*)Itr.Get() + 0xF0);
		for (int i = 0; i < 4; i++) if (FogSet[i]) fog[i] = Lerp(FogFrom[i], FogTo[i], afT);
	}
}

void LocationManager::StepBlend() {
	for (const NvrBlend& b : NvrBlends) TheSettingManager->SetSettingF(b.Section.c_str(), b.Key.c_str(), Lerp(b.From, b.To, BlendT));
	if (LightCell) WriteLightingTargets(BlendT);
	if (Status.Applied[FeatureFog]) WriteFog(BlendT);
	if (!Blending) NvrBlends.clear();
	if (LightCell && Player && Player->parentCell == LightCell) RefreshImageSpace();
}

void LocationManager::RefreshImageSpace() {
	if (!ImageSpace || !LightCell || !Player || Player->parentCell != LightCell) return;
	((void(__cdecl*)(float*))kSetImageSpaceParameters)(ImageTraits(ImageSpace));	// as JohnnyGuitar does after SetCellImageSpace
}

/*
* CRTLightingCycle: ambient color from the time of day, written on our template.
*/
void LocationManager::UpdateAmbientCycle() {
	if (!CycleOn || !LightCell || !Template || !Player || Player->parentCell != LightCell || !Tes || !Tes->sky) return;
	TESClimate* climate = Tes->sky->firstClimate;
	if (!climate) return;

	float time = fmod(TimeGlobals::Get()->GameHour->data, 24.0f);
	// Climate times are in 10 minute units; Cartographer works in hours.
	float sunriseBegin = climate->sunriseBegin * 10.0f / 60.0f;
	float sunriseEnd = climate->sunriseEnd * 10.0f / 60.0f;
	float sunsetBegin = climate->sunsetBegin * 10.0f / 60.0f;
	float sunsetEnd = climate->sunsetEnd * 10.0f / 60.0f;
	float sunriseFadeIn = Sunrise[3], sunriseFadeOut = Sunrise[4];
	float sunsetFadeIn = Sunset[3], sunsetFadeOut = Sunset[4];

	auto frac = [](float a, float b) { return b > 0.0f ? a / b : 1.0f; };
	const float* from = Day;
	const float* to = Day;
	float t = 0.0f;

	if (time > sunriseEnd + sunriseFadeOut && time <= sunsetBegin - sunsetFadeIn) { from = to = Day; }
	else if (time > sunsetBegin - sunsetFadeIn && time <= sunsetBegin) { from = Day; to = Sunset; t = 1.0f - frac(sunsetBegin - time, sunsetFadeIn); }
	else if (time > sunsetBegin && time <= sunsetEnd) { from = to = Sunset; }
	else if (time > sunsetEnd && time <= sunsetEnd + sunsetFadeOut) { from = Sunset; to = Night; t = frac(time - sunsetEnd, sunsetFadeOut); }
	else if (time > sunsetEnd + sunsetFadeOut && time <= 24.0f) { from = to = Night; }
	else if (time >= 0.0f && time <= sunriseBegin - sunriseFadeIn) { from = to = Night; }
	else if (time > sunriseBegin - sunriseFadeIn && time <= sunriseBegin) { from = Night; to = Sunrise; t = 1.0f - frac(sunriseBegin - time, sunriseFadeIn); }
	else if (time > sunriseBegin && time <= sunriseEnd) { from = to = Sunrise; }
	else { from = Sunrise; to = Day; t = frac(time - sunriseEnd, sunriseFadeOut); }

	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	for (int i = 0; i < 3; i++) SetTemplateTrait(Template, 1 + i, Lerp(from[i], to[i], t));
}
