#include "Menu/MenuBackend.h"
#include "Menu/MenuInput.h"
#include "Menu/MenuUI.h"
#include "Menu/LocationsWindow.h"
#include "../../lib/imgui/imgui.h"

#define MenuSettings TheSettingManager->SettingsMain.Menu

void GameMenuManager::Initialize() {

	Logger::Log("Starting the menu manager...");
	TheGameMenuManager = new GameMenuManager();

	TheGameMenuManager->Enabled = false;
	TheGameMenuManager->MainMenuOn = false;
	TheGameMenuManager->IsLiveMenu = nullptr;

	MenuInput::InstallHooks();
}

void GameMenuManager::UpdateSettings() {
	MenuBackend::RequestStyleRefresh(); // screen size or accent color may have changed
}

std::string GameMenuManager::GetKeyName(int keyCode) {
	static const std::map<int, const char*> names = {
		{1, "Esc"}, {2, "1"}, {3, "2"}, {4, "3"}, {5, "4"}, {6, "5"}, {7, "6"}, {8, "7"}, {9, "8"}, {10, "9"}, {11, "0"},
		{12, "-"}, {13, "="}, {14, "Backspace"}, {15, "Tab"},
		{16, "Q"}, {17, "W"}, {18, "E"}, {19, "R"}, {20, "T"}, {21, "Y"}, {22, "U"}, {23, "I"}, {24, "O"}, {25, "P"},
		{26, "["}, {27, "]"}, {28, "Enter"}, {29, "Left Ctrl"},
		{30, "A"}, {31, "S"}, {32, "D"}, {33, "F"}, {34, "G"}, {35, "H"}, {36, "J"}, {37, "K"}, {38, "L"},
		{39, ";"}, {40, "'"}, {41, "`"}, {42, "Left Shift"}, {43, "\\"},
		{44, "Z"}, {45, "X"}, {46, "C"}, {47, "V"}, {48, "B"}, {49, "N"}, {50, "M"},
		{51, ","}, {52, "."}, {53, "/"}, {54, "Right Shift"}, {55, "Numpad *"}, {56, "Left Alt"}, {57, "Spacebar"}, {58, "Caps Lock"},
		{59, "F1"}, {60, "F2"}, {61, "F3"}, {62, "F4"}, {63, "F5"}, {64, "F6"}, {65, "F7"}, {66, "F8"}, {67, "F9"}, {68, "F10"},
		{69, "Num Lock"}, {70, "Scroll Lock"},
		{71, "Numpad 7"}, {72, "Numpad 8"}, {73, "Numpad 9"}, {74, "Numpad -"}, {75, "Numpad 4"}, {76, "Numpad 5"}, {77, "Numpad 6"},
		{78, "Numpad +"}, {79, "Numpad 1"}, {80, "Numpad 2"}, {81, "Numpad 3"}, {82, "Numpad 0"}, {83, "Numpad ."},
		{87, "F11"}, {88, "F12"}, {156, "Numpad Enter"}, {157, "Right Control"}, {181, "Numpad /"}, {184, "Right Alt"},
		{199, "Home"}, {200, "Up Arrow"}, {201, "PgUp"}, {203, "Left Arrow"}, {205, "Right Arrow"}, {207, "End"},
		{208, "Down Arrow"}, {209, "PgDown"}, {210, "Insert"}, {211, "Delete"},
	};
	auto result = names.find(keyCode);
	return result == names.end() ? std::to_string(keyCode) : result->second;
}

void GameMenuManager::MainMenuMessage() {
	std::chrono::time_point now = std::chrono::system_clock::now();

	// on the main menu only show a line at the bottom as a signal that the mod has loaded
	if (!MainMenuOn) {
		MainMenuOn = true;
		MainMenuStartTime = now;
	}

	std::chrono::duration<double> elapsed_seconds = now - MainMenuStartTime;
	if (elapsed_seconds.count() > 5.0) return; // only shown for 5 seconds

	if (!MenuBackend::BeginFrame(false)) return;

	const std::string message = std::string(PluginVersion::VersionString) + " - Open the Config Menu by pressing the key " + GetKeyName(MenuSettings.KeyEnable);
	const ImVec2 size = ImGui::CalcTextSize(message.c_str());
	const ImGuiIO& io = ImGui::GetIO();
	const ImVec2 pos((io.DisplaySize.x - size.x) * 0.5f, io.DisplaySize.y - size.y - 10.0f * MenuBackend::Scale());
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 200), message.c_str());
	dl->AddText(pos, IM_COL32(215, 218, 224, 255), message.c_str());

	MenuBackend::EndFrame();
}

// The open key and the input blocking are handled in MenuInput (they run when the game polls its controls).
void GameMenuManager::HandleInput() {
	if (!InterfaceManager->IsActive(Menu::MenuType::kMenuType_None)) Enabled = false; // close when a game menu opens
}

void GameMenuManager::Render() {

	if (InterfaceManager->IsActive(Menu::MenuType::kMenuType_Main)) {
		Enabled = false;
		MainMenuMessage();
		return;
	}

	static bool wasDrawn = false; // the menu was drawn last frame
	HandleInput();
	if (!Enabled) {
		if (wasDrawn) { wasDrawn = false; MenuInput::OnToggled(false); } // closed by a game menu or the key
		return;
	}

	if (!MenuBackend::BeginFrame(true)) return;
	static Window drawnWindow = Window::Settings; // the window drawn last frame
	if (!wasDrawn || drawnWindow != ActiveWindow) {
		if (ActiveWindow == Window::Locations) LocationsWindow::OnOpened();
		else MenuUI::OnOpened();
		wasDrawn = true;
		drawnWindow = ActiveWindow;
	}
	if (ActiveWindow == Window::Locations) LocationsWindow::Draw();
	else MenuUI::Draw();
	MenuBackend::EndFrame();
	if (!Enabled) { wasDrawn = false; MenuInput::OnToggled(false); } // closed with Escape
}
