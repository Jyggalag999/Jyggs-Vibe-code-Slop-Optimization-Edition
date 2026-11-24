#pragma once
/*
* Locations (built-in Cartographer): applies the resolved presets to the game.
*
*   NVR settings   one batch through SettingManager, one deferred reload
*   Lighting       our own TESImageSpace + BGSLightingTemplate, pointed at the player's interior cell only
*                  (previous image space / template / inherit flags are remembered and restored on leave),
*                  plus the dynamic ambient cycle
*   Fog            fog distances of every loaded weather (exteriors)
*   Game settings  GameSettingCollection values
*
* Presets are applied from OnDestination, called by the PositionPlayer call-site hooks before the destination
* loads. Update (called every frame from ShaderManager::UpdateConstants) is the fallback for any location change
* that did not go through the hooks, runs the ambient cycle and the blending.
*/

#include "LocationResolver.h"

#include "LocationStatus.h"

struct NVSEConsoleInterface;
class TESObjectCELL;
class TESImageSpace;
class BGSLightingTemplate;
class TESWorldSpace;

class LocationManager {
public:
	static void Initialize();
	static void SetConsoleInterface(NVSEConsoleInterface* apConsole);	// from the plugin load, before Initialize; may be null
	static bool CanGoTo();												// NVSE's console interface is available

	// Go to a location: the menu calls this, the command runs from Update on a later frame once no game menu is
	// open ("cow <id> 0 0" for a worldspace, "coc <id>" otherwise). Only valid on the main thread.
	bool RequestGoTo(const std::string& asEditorId);
	const std::string& GoStatus() const { return GoResult; }	// last Go outcome, for the menu

	// A cell (interior) or worldspace in the current load order, found by editor ID.
	struct InstalledInfo {
		void*		Form = nullptr;
		bool		Worldspace = false;
		std::string	Plugin;				// file the form comes from, "?" if unknown
	};
	// Null if no interior cell or worldspace has this editor ID. Built once on first call; main thread only.
	const InstalledInfo* FindInstalled(const std::string& asEditorId);

	void Update();								// every frame
	void RunPendingGo();						// queued Go from the menu; call from the main loop, never from inside rendering
	void OnDestination(TESObjectCELL* apCell);	// before the player is moved to apCell (loads, doors, coc, fast travel)
	void Reload();								// re-read all files and re-apply for the current location (with blending)

	const LocationStatus& GetStatus() const { return Status; }
	const Locations::LocationConfig& GetConfig() const { return Config; }	// the loaded Cartographer files (preset manager)
	const char* FeatureName(int aiFeature) const { return Config.Features[aiFeature].Name; }

private:
	struct Target {
		void*		Form = nullptr;		// cell (interior) or worldspace (exterior), used to detect a change
		std::string	Name;
		bool		Interior = false;
		bool		BehaveAsExterior = false;
	};

	bool	EnsureInit();
	bool	Active();
	void*	TargetForm(TESObjectCELL* apCell);			// cell (interior) or worldspace (exterior); cheap
	Target	GetTarget(TESObjectCELL* apCell);			// also builds the editor ID
	void	Apply(TESObjectCELL* apCell, bool abInstant);
	void	ApplyNVR(const Target& aTarget, bool abInstant);
	void	ApplyLighting(TESObjectCELL* apCell, const Target& aTarget, bool abInstant);
	void	ApplyFog(const Target& aTarget, bool abInstant);
	void	ApplyGameSettings(const Target& aTarget);
	void	RestoreLighting();
	void	StepBlend();
	void	UpdateAmbientCycle();
	void	WriteLightingTargets(float afT);
	void	WriteFog(float afT);
	void	RefreshImageSpace();
	void	Log(const char* aMessage, ...);

	bool	IsWorldSpace(const char* asEditorId);
	void	BuildInstalledIndex();

	std::unordered_map<std::string, InstalledInfo> Installed;	// lowercase editor ID -> form
	bool		InstalledBuilt = false;

	std::string PendingGo;
	std::string GoResult;
	unsigned	GoFrames = 0;

	LocationStatus Status;
	Locations::LocationConfig Config;
	bool		Initialized = false;
	bool		InitFailed = false;
	void*		LastTarget = nullptr;
	// Set when a PositionPlayer hook applied a destination ahead of the load. Until the player's cell becomes that
	// destination, the old location (HookFrom) is expected and the fallback in Update leaves it alone.
	void*		HookTarget = nullptr;
	void*		HookFrom = nullptr;
	unsigned long long	HookTick = 0;
	std::vector<std::string> LastStack[Locations::FeatureCount];
	bool		HaveLastStack[Locations::FeatureCount] = {};
	bool		PresetOverrideLogged = false;	// logged once: a Locations key that the Performance preset also sets

	// Lighting
	TESImageSpace*			ImageSpace = nullptr;
	BGSLightingTemplate*	Template = nullptr;
	TESObjectCELL*			LightCell = nullptr;		// the cell currently pointed at our forms
	void*					SavedImageSpace = nullptr;
	void*					SavedTemplate = nullptr;
	unsigned				SavedInherit = 0;
	bool					RefreshPending = false;
	bool					CycleOn = false;
	float					Day[3] = {}, Sunset[5] = {}, Night[3] = {}, Sunrise[5] = {};
	float					ImgFrom[33] = {}, ImgTo[33] = {};
	float					TmplFrom[17] = {}, TmplTo[17] = {};	// index = JohnnyGuitar template trait id (1..16)

	// Fog
	float					FogFrom[4] = {}, FogTo[4] = {};
	bool					FogSet[4] = {};

	// NVR float blending
	struct NvrBlend { std::string Section, Key; float From, To; };
	std::vector<NvrBlend>	NvrBlends;

	// Blending state
	bool					Blending = false;
	float					BlendT = 1.0f;
	unsigned long long		LastTick = 0;
};

extern LocationManager* TheLocationManager;
