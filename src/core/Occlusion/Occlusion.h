#pragma once
// Built-in occlusion culling (port of Conor's Occlusion Culling 1.2.3, mrfury97/FNV-Occlusion-Culling, GPL-3.0).
// This header holds only plain data and three entry points, so Occlusion.cpp does not need any of our other headers.
#include <cstdint>

namespace Occ {
	// One field per key of the original OcclusionCulling.ini; same defaults except compareKey (0: his 207 is our menu key).
	struct Config {
		bool enabled = true;
		float minRadius = 150.0f;        // smaller objects are never tested (game units)
		float nearDistance = 400.0f;     // boxes closer to the camera than this are never tested
		int maxQueries = 500;            // queries issued per frame at most
		int maxAnswerAge = 4;            // oldest answer (frames) that counts
		int visibleRetest = 4;           // a visible object is tested again after this many frames
		float guardDegrees = 2.5f;       // camera turn per frame of answer age beyond which an answer is not used
		float guardUnits = 32.0f;        // camera move per frame of answer age
		float revealDepth = 150.0f;      // boxes grow by how far a camera move slides an edge this far away (0 = off)
		float screenPad = 1.5f;          // every box reaches at least this many pixels past its object on screen (TAA jitter)
		int logInterval = 600;           // frames per log block
		bool debugBoxes = false;         // draw the tested boxes (green: visible, red: hidden; red ones through walls)
		bool culling = true;             // start with hiding on
		int toggleKey = 0xD2;            // DirectInput scancode of the on/off key (Insert)
		bool distantLOD = false;         // also test distant LOD objects
		bool imposters = true;           // also test imposters (refs flagged to show at any distance: never faded out)
		bool lampCulling = false;        // switch hidden lamps off (otherwise they are only measured)
		bool orientedBoxes = true;       // tighter boxes from the shapes' vertices, in the object's own frame
		int compareKey = 0;              // switches land, grass, throttles off one at a time for comparisons (needs devTools)
		bool actorCulling = true;        // NPCs and creatures are tested and hidden too
		float actorMargin = 24.0f;       // added around an actor's bound (plus 10% of its radius)
		bool animThrottle = false;       // actors the main view does not draw update their bones every animInterval frames
		int animInterval = 2;
		float animNearDistance = 512.0f; // actors closer than this to the camera always update every frame
		bool particleThrottle = false;   // particle systems on objects the main view does not draw update every particleInterval frames
		int particleInterval = 3;
		float particleNearDistance = 512.0f;
		bool activatorCulling = true;    // doors, activators, talking activators and terminals are tested and hidden too
		float activatorMargin = 16.0f;   // added around their bound (plus 5% of its radius)
		bool treeCulling = true;         // SpeedTree trees and shrubs are tested and hidden too
		bool landCulling = true;         // land quadrants (2048 x 2048 units, four per cell) are tested and hidden too
		float landMargin = 32.0f;        // added above and below a quadrant's height range (tessellated land moves)
		bool lodTerrain = true;          // distant terrain LOD chunks (under the scene's LandLOD node) are tested and hidden too
		bool grassCulling = true;        // grass groups (TallGrassTriStrips under the scene's Grass node) are tested and hidden too
		float grassMargin = 32.0f;       // added around a grass group's box on every side (blades sway in the wind)
		float treeMargin = 32.0f;        // added around a tree's bound (plus 20% of its radius) for its leaf cards
		bool devTools = false;           // log blocks of numbers, timers, debug boxes and the compare key (decided at install)
	};
}

// Implemented in OcclusionBridge.cpp (our Logger, "[Occlusion] " prefix).
namespace OcclusionBridge {
	void LogLine(const char* text);
	void InstallFromSettings();	// deferred init: reads [_Main.Main.Occlusion] and installs
	void OnSettingsLoaded();	// end of SettingManager::LoadSettings: live settings reach the running module
}

namespace Occlusion {
	enum State { kNotInstalled = 0, kDisabled, kInstalled, kStandaloneLoaded, kHookFailed };
	// At deferred init, after every plugin's load-time patches. Chains to whatever the patched sites hold.
	void Install(const Occ::Config& config);
	// Settings changed (menu, toml reload): every field but `enabled` and `devTools` takes effect at once.
	void Reload(const Occ::Config& config);
	State GetState();
	const char* StatusText();
}
