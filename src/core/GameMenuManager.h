#pragma once

// The in-game settings menu (Dear ImGui). Drawing lives in core/Menu/*, this class keeps the state the
// rest of the plugin uses: open/closed, the main menu message, and the RealTimeMenu pause query.
class GameMenuManager { // Never disposed
public:
	static void Initialize();

	void					Render();
	void					HandleInput();
	void					MainMenuMessage();
	void					UpdateSettings();
	std::string				GetKeyName(int keyCode);

	enum class Window { Settings, Locations };

	bool										Enabled; // a menu window is open (whichever ActiveWindow says)
	Window										ActiveWindow = Window::Settings;
	std::chrono::system_clock::time_point		MainMenuStartTime;
	bool										MainMenuOn;

	enum MenuPauseState : uint32_t {
		MENU_PAUSED	= 0, // Pauses the game, runs MenuMode only
		MENU_LIVE	= 1, // Does not pause the game, runs both GameMode and MenuMode
		MENU_MIXED	= 2, // Does not pause the game, runs only MenuMode
	};

	MenuPauseState(__cdecl* IsLiveMenu)(uint32_t aeMenu, bool abCheckInstances, bool abGameModeCheck);
};
