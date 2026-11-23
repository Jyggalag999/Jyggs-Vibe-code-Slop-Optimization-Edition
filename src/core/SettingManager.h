#pragma once

#define TOML11_PRESERVE_COMMENTS_BY_DEFAULT
#include "../lib/toml11/toml.hpp"


struct SettingsMainStruct {

	struct MainStruct {
		bool	RemoveUnderwater;
		bool	ForceMSAA;
		bool	ForceReflections;
		bool	RemovePrecipitations;
		bool	MemoryHeapManagement;
		bool	MemoryTextureManagement;
		bool	ReplaceIntro;
        bool    SkipFog;
        bool    RenderEffects;
        bool    RenderPreTonemapping;
		bool	FrameChain;
		bool	ChainUsesGameTexture;
		bool	DedicatedGodRays;
		bool	SlimDepthBuffer;
		bool	DedicatedAO;
		bool	CompositeFog;
		bool	MergedPointShadows;
		bool	CubeMapFaceCulling;
		bool	SkipUnusedCubeMap;
		bool	CubeMapCameraCulling;
		bool	PartialShadowAtlasUpdate;
		bool	SharedShadowWalk;
		bool	BatchScriptSettings;	// script NVRSetSetting calls rebuild the settings once per frame (false = on every call, as in 4.4.1)
		UInt8	AnisotropicFilter;
		UInt16	ScreenshotKey;
		bool	HDRScreenshot;
		float	FarPlaneDistance;
	};
	
	struct FrameRateStruct {
		bool	SmartControl;
		bool	SmartBackgroundProcess;
		UInt32	SmartControlFPS;
		UInt32  BackgroundThreadPriority;
		float	FlowControl;
	};

	struct CullingProcessStruct {
		bool EnableCulling;
		bool EnableReflectionCulling;
		float CullMinSize;
		float CullMinSizeReflection;
	};

	struct OcclusionCullingStruct {
		bool	Enabled;
		bool	OccludingStatic;
		bool	OccludedStatic;
		bool	OccludedDistantStatic;
		bool	OccludedDistantStaticIC;
		UInt32	OcclusionMapRatio;
		float	OccludingStaticMin;
		float	OccludedStaticMin;
		float	OccludedStaticMax;
		float	OccludedDistantStaticMax;
	};

	struct CameraModeStruct {
		bool		Enabled;
		bool		ChasingFirst;
		bool		ChasingThird;
		UInt8		Crosshair;
		NiPoint3	Offset;
		NiPoint3	AimingOffset;
		NiPoint3	DialogOffset;
		float		FoV;
	};
	
	struct IKFootStruct {
		bool		Enabled;
		float		Coeff;
		float		CoeffThigh;
		float		CoeffCalf;
		float		CoeffFoot;
		float		CoeffToe;
	};

	struct EquipmentModeStruct {
		bool		Enabled;
		bool		SleepingEquipment;
		bool		SwimmingEquipment;
		UInt16		TorchKey;
		UInt16		CombatEquipmentKey;
		float		DualBlockDelay;
		NiPoint3	ShieldOnBackPos;
		NiPoint3	ShieldOnBackRot;
		NiPoint3	WeaponOnBackPos;
		NiPoint3	WeaponOnBackRot;
		NiPoint3	TwoHandWeaponOnBackPos;
		NiPoint3	TwoHandWeaponOnBackRot;
		NiPoint3	BowOnBackPos;
		NiPoint3	BowOnBackRot;
		NiPoint3	StaffOnBackPos;
		NiPoint3	StaffOnBackRot;
		NiPoint3	TorchOnBeltPos;
		NiPoint3	TorchOnBeltRot;
	};

	struct GrassModeStruct {
		bool	Enabled;
	};

	struct MountedCombatStruct {
		bool		Enabled;
		NiPoint3	WeaponOnBackPos;
		NiPoint3	WeaponOnBackRot;
		NiPoint3	TwoHandWeaponOnBackPos;
		NiPoint3	TwoHandWeaponOnBackRot;
		NiPoint3	BowOnBackPos;
		NiPoint3	BowOnBackRot;
		NiPoint3	StaffOnBackPos;
		NiPoint3	StaffOnBackRot;
	};
	
	struct SleepingModeStruct {
		bool	Enabled;
		UInt8	Mode;
	};
	
	struct WeatherModeStruct {
		bool		Enabled;
		NiPoint3	CoeffNight;
		NiPoint3	CoeffFog;
		NiPoint3	CoeffSun;
	};
	
	struct ShadowModeStruct {
		float	NearQuality;
	};

	struct ShadersStruct {
		bool Blood;
		bool Grass;
		bool HDR;
		bool NightEye;
		bool POM;
		bool Skin;
		bool Terrain;
		bool Water;
		bool Extra;
	};


	struct MenuStruct {
		UInt8	KeyEnable;
		UInt8	KeySave;
		UInt8	AccentColor[3];
		UInt8	BackgroundColor[3];
		UInt8	PanelColor[3];
		UInt8	FrameColor[3];
		UInt8	TextColor[3];
		UInt8	CursorColor[3];
		bool	CursorMatchesAccent;
	};

	struct PurgerStruct {
		bool	Enabled;
		bool	PurgeTextures;
		bool	PurgeCells;
		UInt16	Key;
		int		Time;
	};

	struct GravityStruct {
		bool	Enabled;
		float	Value;
	};

	struct DodgeStruct {
		bool	Enabled;
		bool	DoubleTap;
		UInt8	AcrobaticsLevel;
		float	DoubleTapTime;
	};
	
	struct FlyCamStruct {
		bool	Enabled;
		UInt8	KeyAdd;
		UInt8	KeySubtract;
		float	ScrollMultiplier;
		float	StepValue;
	};

	struct LocationsStruct {
		bool	Enabled;
		float	BlendSeconds;
		bool	DebugLog;
		bool	PresetEditor;
	};

	struct ProfilerStruct {
		bool	Enabled;			// log GPU time per effect / shadow map pass
		float	IntervalSeconds;
		bool	AutoBenchmark;		// alternate the Performance preset off/on every AutoBenchmarkSeconds and write a CSV row per window
		float	AutoBenchmarkSeconds;
		bool	CellChangeTiming;	// log a [CellChange] line with frame times and settings work for 300 frames after each location change
	};

	struct DevelopStruct {
		bool    DebugMode;       // enables hotkeys to print textures
		UInt8	TraceShaders;
	};

	MainStruct					Main;
	CullingProcessStruct		CullingProcess;
	OcclusionCullingStruct		OcclusionCulling;
	CameraModeStruct			CameraMode;
	IKFootStruct				IKFoot;
	EquipmentModeStruct			EquipmentMode;
	FrameRateStruct				FrameRate;
	GrassModeStruct				GrassMode;
	MountedCombatStruct			MountedCombat;
	SleepingModeStruct			SleepingMode;
	WeatherModeStruct			WeatherMode;
	ShadowModeStruct			ShadowMode;
	ShadersStruct				Shaders;
	MenuStruct					Menu;
	PurgerStruct				Purger;
	GravityStruct				Gravity;
	DodgeStruct					Dodge;
	FlyCamStruct				FlyCam;
	DevelopStruct				Develop;
	LocationsStruct				Locations;
	ProfilerStruct				Profiler;
};

typedef std::vector<UInt32> FormsList;

struct SettingsWaterStruct {
	float choppiness;
	float waveWidth;
	float waveSpeed;
	float reflectivity;
	float causticsStrength;
	float causticsStrengthS;
	float shoreFactor;
	float shoreMovement;
	float turbidity;
	float inScattCoeff;
	float inExtCoeff_R;
	float inExtCoeff_G;
	float inExtCoeff_B;
	float depthDarkness;
};


struct SettingsColoringStruct {
	float Strength;
	float BaseGamma;
	float Fade;
	float Contrast;
	float Saturation;
	float Bleach;
	float BleachLuma;
	float ColorCurve;
	float ColorCurveR;
	float ColorCurveG;
	float ColorCurveB;
	float EffectGamma;
	float EffectGammaR;
	float EffectGammaG;
	float EffectGammaB;
	float Linearization;
};


struct SettingsWeatherStruct {
	char					LowerLayer[80];
	char					UpperLayer[80];
	UInt8					WindSpeed;
	UInt8					CloudSpeedLower;
	UInt8					CloudSpeedUpper;
	UInt8					TransDelta;
	UInt8					SunGlare;
	UInt8					SunDamage;
	float					FogNearDay;
	float					FogNearNight;
	float					FogFarDay;
	float					FogFarNight;
	TESWeather::ColorData	Colors[TESWeather::kNumColorTypes];
	float					HDR[14];
};

typedef std::map<std::string, SettingsWaterStruct> SettingsWaterMap;
typedef std::map<std::string, SettingsColoringStruct> SettingsColoringMap;
typedef std::map<std::string, SettingsWeatherStruct> SettingsWeatherMap;
typedef std::vector<std::string> StringList;
typedef toml::basic_value<toml::preserve_comments, std::map, std::vector> tomlValue;

struct SettingChange {
	std::string	Section;
	std::string	Key;
	float		Value;
};

class SettingManager : public SettingManagerBase {
public:
	class Configuration {
	public:
		enum NodeType {
			Boolean	 = 0,
			Integer  = 1,
			Float    = 2,
			String   = 3,
		};
		
		struct SectionPosition {
			char* Start;
			char* End;
			bool Found;
		};

		class ConfigNode {
		public:
			char		Section[40];
			char		Key[40];
			char		MidSection[80];
			char		Value[80];
			std::string Description;
			bool		Reboot;
			UInt32		Type;
			
			bool operator < (const ConfigNode& Node) const { return (strcoll(Key, Node.Key) < 0 ? true : false); }
		};

		typedef	std::vector<ConfigNode> SettingList;

		void			Init();
		bool			FillNode(ConfigNode* Node, const char* Section, const char* Key);
		void			FillSections(StringList* Sections, const char* ParentSection);
		void			FillSettings(SettingList* Nodes, const char* Section);
		void			SetValue(ConfigNode* Node);
		bool			GetValue(const char* Section, const char* Key, char* Value); // cached FillNode value; Value must hold ConfigNode::Value (80)
		bool			PresetActive();	// Main.Main.Performance PerformancePreset (user value, else default); cached until the config changes
		bool			PresetLookup(const char* Section, const char* Key, tomlValue& Out); // value the Performance preset sets for this key, if active and listed
		int				PresetState = -1;	// cache of PresetActive: -1 unknown, 0 off, 1 on
		void			CreateWeatherSection(const char* WeatherName, TESWeather* Weather);
		tomlValue*		FindSection(tomlValue* table, StringList* keys);

		tomlValue		TomlConfig;
		tomlValue		DefaultConfig;
		// Read cache for GetValue. Cleared whenever TomlConfig/DefaultConfig change (Init, SetValue).
		// All callers are on the game/render main thread, so no locking.
		std::unordered_map<std::string, std::pair<bool, std::string>> ValueCache; // "Section\x1fKey" -> (found, Node.Value)
		// "SectionKey" -> NodeType, or -1 when the key is not a setting. Types come from the defaults, which never change
		// at runtime, so this is only cleared by Init.
		std::unordered_map<std::string, int> TypeCache;
		bool			configLoaded;
		char*			Config;
		char*			ConfigB;
		UInt32			FileSize;
	};

	static void				Initialize();
	void					LoadSettings();
	void					FlushPendingReload(); // runs the LoadSettings that script/Locations setters deferred (once, however many values changed)
	void					SaveSettings();
	int						GetSettingI(const char* Section, const char* Key);
	float					GetSettingF(const char* Section, const char* Key);
	char*					GetSettingS(const char* Section, const char* Key, char* Value);
	float					GetSettingTransition(const char* Section, const char* Key, bool isExterior, float isDaytime);
	void					SetSetting(const char* Section, const char* Key, float Value);
	void					SetSetting(const char* Section, const char* Key, int Value);
	void					SetSetting(const char* Section, const char* Key, bool Value);
	void					SetSettingS(const char* Section, const char* Key, char* Value);
	void					SetSettingF(const char* Section, const char* Key, float Value);
	int						GetSettingType(const char* Section, const char* Key); // Configuration::NodeType, or -1 if the setting doesn't exist
	UInt32					SetSettingBatch(const std::vector<SettingChange>& Changes); // sets many values, one deferred reload (FlushPendingReload); returns how many were set
	void					SetSetting(Configuration::ConfigNode* Node);
	bool					IsPresetOverridden(const char* Section, const char* Key); // true while the Performance preset supplies this key's value (the menu shows it disabled)
	void					Increment(const char* Section, const char* Key);
	void					Decrement(const char* Section, const char* Key);
	void					SetSettingWeather(const char* Section, const char* Key, float Value);
	void					FillMenuSections(StringList* Sections, const char* ParentSection);
	void					FillMenuSettings(Configuration::SettingList* Settings, const char* Section);
	void					GetFormList(const char* Name, FormsList* SettingsList);
	void					CreateNode(Configuration::ConfigNode* Node, const char* Section, const char* Key, float Value, bool Reboot);
	void					CreateNode(Configuration::ConfigNode* Node, const char* Section, const char* Key, int Value, bool Reboot);
	void					CreateNode(Configuration::ConfigNode* Node, const char* Section, const char* Key, bool Value, bool Reboot);
	void					CreateNodeS(Configuration::ConfigNode* Node, const char* Section, const char* Key, const char* Value, bool Reboot);
	bool					GetMenuShaderEnabled(const char* Name);
	void					SetMenuShaderEnabled(const char* Name, bool enabled);
	bool					GetMenuMiscEnabled(const char* Name);
	void					SetMenuMiscEnabled(const char* Name, bool enabled);
	SettingsWaterStruct*	GetSettingsWater(const char* PlayerLocation);
	SettingsColoringStruct* GetSettingsColoring(const char* PlayerLocation);
	SettingsWeatherStruct*	GetSettingsWeather(const char* WeatherName);
	void					SetSettingsWeather(TESWeather* Weather);
	bool					IsShaderForced(const char* Name);

	template <typename T> static std::string	ToString(const T Value);
	template <typename T> static T				FromString(const char* Value);
	static void									SplitString(const char* String, const char* Delimiter, StringList* Values);
	template <typename T> static void			FillFromString(const char* String, const char* Delimiter, T* Values);

	Configuration					Config;
	bool							GameLoading;
	bool							SettingsChanged;
	bool							hasUnsavedChanges;
	bool							ReloadPending; // SetSettingF changed values; the settings structs are rebuilt once by FlushPendingReload
	SettingsMainStruct				SettingsMain;
	SettingsWaterMap				SettingsWater;
	SettingsColoringMap				SettingsColoring;
	SettingsWeatherMap				SettingsWeather;

private:
	void					FilterMenuSections(StringList* Sections, const char* ParentSection);
};
