#pragma once
/*
* What the menu pages need from the rest of the plugin, as one interface. The game implements it in
* MenuDataGame.cpp (the only menu file that touches the engine singletons); the preview program in
* tools/MenuPreview implements it with fake data. This header is engine-free (std only).
*/

#include "../Locations/LocationStatus.h"
#include "../Locations/LocationConfig.h"
#include <string>
#include <vector>
#include <map>
#include <set>

struct MenuNode {
	char			Section[64] = {};	// full path, e.g. "Shaders.Bloom.Main"
	char			Key[40] = {};
	char			Value[80] = {};
	std::string		Description;
	unsigned		Type = 0;			// MenuNodeType
};

namespace MenuNodeType {
	enum { Boolean = 0, Integer = 1, Float = 2, String = 3 };	// same values as SettingManager::Configuration::NodeType
}

struct MenuTiming {
	std::string		name;
	float			update, render, total;
};

struct MenuInstalled {
	bool			worldspace = false;
	std::string		plugin;
};

class IMenuData {
public:
	virtual ~IMenuData() {}

	// ---- settings tree (sidebar, search, pages)
	virtual void	MenuSections(std::vector<std::string>& out, const char* parent) = 0;		// parent null = top level
	virtual void	MenuSettings(std::vector<MenuNode>& out, const std::string& section) = 0;
	virtual bool	FillNode(MenuNode& out, const std::string& section, const std::string& key) = 0;
	virtual void	SearchIndex(std::vector<std::pair<std::string, std::string>>& out) = 0;	// (path, lower-case blob of names below it)
	virtual bool	DefaultValue(const MenuNode& node, bool preset, std::string& out) = 0;
	virtual bool	IsPresetOverridden(const char* section, const char* key) = 0;

	// ---- writes (the caller queues ApplyChanges once per frame)
	virtual void	SetSetting(const MenuNode& node) = 0;
	virtual void	SetPresetSetting(const MenuNode& node) = 0;
	virtual void	RemovePresetSetting(const char* section, const char* key) = 0;
	virtual void	SetBool(const char* section, const char* key, bool value) = 0;
	virtual void	ApplyChanges() = 0;			// LoadSettings + refresh of the menu colors
	virtual bool	HasUnsaved() = 0;
	virtual void	Save() = 0;
	virtual void	Revert(const std::set<std::string>& toggledShaders) = 0;

	// ---- shaders / performance
	virtual bool	ShaderEnabled(const std::string& name) = 0;
	virtual void	ToggleShader(const std::string& name) = 0;
	virtual void	Timings(std::vector<MenuTiming>& rows, float& shadowMaps) = 0;
	virtual std::string OcclusionStatus() = 0;
	virtual void	ForceWeather(const std::string& name) = 0;

	// ---- style
	virtual void	StyleRgb(const char* key, unsigned char out[3]) = 0;	// AccentColor, BackgroundColor, PanelColor, FrameColor, TextColor, CursorColor
	virtual bool	CursorMatchesAccent() = 0;

	// ---- menu
	virtual void	CloseMenu() = 0;
	virtual std::string LocationsKeyName() = 0;
	virtual std::string KeyName(int scancode) = 0;			// DirectInput scancode as text
	virtual void	BeginKeyCapture() = 0;					// Keybinds page: capture the next key press
	virtual void	CancelKeyCapture() = 0;
	virtual bool	KeyCaptureActive() = 0;					// true until PollCapturedKey delivered
	virtual int		PollCapturedKey() = 0;					// -1 nothing yet, else a scancode (1 = Esc = cancel)
	virtual float	SettingF(const char* section, const char* key) = 0;
	virtual int		SettingType(const char* section, const char* key) = 0;	// MenuNodeType, -1 if unknown

	// ---- Locations
	virtual bool	PresetEditor() = 0;
	virtual const LocationStatus&		LocStatus() = 0;
	virtual const Locations::LocationConfig& LocConfig() = 0;
	virtual void	LocReload() = 0;
	virtual const MenuInstalled* FindInstalled(const std::string& editorId) = 0;
	virtual bool	CanGoTo() = 0;
	virtual bool	RequestGoTo(const std::string& editorId) = 0;
	virtual std::string GoStatus() = 0;
	virtual const char* FeatureName(int feature) = 0;
};

IMenuData& Data();	// the game's (MenuDataGame.cpp) or the preview's implementation
