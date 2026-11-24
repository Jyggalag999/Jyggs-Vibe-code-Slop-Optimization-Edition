// Glue between our settings/logger and the built-in occlusion culling (Occlusion.cpp, port of Conor's Occlusion Culling).
// Settings: [_Main.Main.Occlusion] (the keys shown in the menu) and [_Main.Main.OcclusionTuning] (the rest of his INI keys).
#include "Occlusion.h"
#include <algorithm>

namespace {
	const char* const kMain = "Main.Main.Occlusion";
	const char* const kTune = "Main.Main.OcclusionTuning";

	// Same ranges as the original INI reader; a value outside falls back to his default.
	int I(const char* sec, const char* key, int def, int lo, int hi) {
		const int v = TheSettingManager->GetSettingI(sec, key);
		return (v >= lo && v <= hi) ? v : def;
	}
	float F(const char* sec, const char* key, float def, float lo, float hi) {
		const float v = TheSettingManager->GetSettingF(sec, key);
		return (v >= lo && v <= hi) ? v : def;
	}

	Occ::Config Read() {
		const Occ::Config d;
		Occ::Config c;
		c.enabled = I(kMain, "Enabled", 0, 0, 1) != 0;
		c.culling = I(kMain, "Culling", 1, 0, 1) != 0;
		c.toggleKey = I(kMain, "ToggleKey", d.toggleKey, 0, 255);
		c.animThrottle = I(kMain, "AnimThrottle", 0, 0, 1) != 0;
		c.particleThrottle = I(kMain, "ParticleThrottle", 0, 0, 1) != 0;
		c.maxQueries = I(kMain, "MaxQueriesPerFrame", d.maxQueries, 0, 4000);
		c.maxAnswerAge = I(kMain, "MaxAnswerAge", d.maxAnswerAge, 1, 12);

		c.minRadius = F(kTune, "MinRadius", d.minRadius, 0.0f, 5000.0f);
		c.nearDistance = F(kTune, "NearDistance", d.nearDistance, 50.0f, 10000.0f);
		c.distantLOD = I(kTune, "DistantLOD", 0, 0, 1) != 0;
		c.imposters = I(kTune, "Imposters", 1, 0, 1) != 0;
		c.visibleRetest = I(kTune, "VisibleRetestFrames", d.visibleRetest, 1, 16);
		c.guardDegrees = F(kTune, "GuardDegreesPerFrame", d.guardDegrees, 0.1f, 45.0f);
		c.guardUnits = F(kTune, "GuardUnitsPerFrame", d.guardUnits, 1.0f, 1000.0f);
		c.revealDepth = F(kTune, "RevealDepth", d.revealDepth, 0.0f, 100000.0f);
		c.screenPad = F(kTune, "ScreenPadPixels", d.screenPad, 0.0f, 16.0f);
		c.landCulling = I(kTune, "LandCulling", 1, 0, 1) != 0;
		c.landMargin = F(kTune, "LandMargin", d.landMargin, 0.0f, 1024.0f);
		c.lodTerrain = I(kTune, "TerrainLOD", 1, 0, 1) != 0;
		c.grassCulling = I(kTune, "GrassCulling", 1, 0, 1) != 0;
		c.grassMargin = F(kTune, "GrassMargin", d.grassMargin, 0.0f, 1024.0f);
		c.lampCulling = I(kTune, "LampCulling", 0, 0, 1) != 0;
		c.actorCulling = I(kTune, "ActorCulling", 1, 0, 1) != 0;
		c.actorMargin = F(kTune, "ActorMargin", d.actorMargin, 0.0f, 512.0f);
		c.animInterval = I(kTune, "AnimInterval", d.animInterval, 1, 8);
		c.animNearDistance = F(kTune, "AnimNearDistance", d.animNearDistance, 0.0f, 100000.0f);
		c.particleInterval = I(kTune, "ParticleInterval", d.particleInterval, 1, 8);
		c.particleNearDistance = F(kTune, "ParticleNearDistance", d.particleNearDistance, 0.0f, 100000.0f);
		c.activatorCulling = I(kTune, "ActivatorCulling", 1, 0, 1) != 0;
		c.activatorMargin = F(kTune, "ActivatorMargin", d.activatorMargin, 0.0f, 512.0f);
		c.treeCulling = I(kTune, "TreeCulling", 1, 0, 1) != 0;
		c.treeMargin = F(kTune, "TreeMargin", d.treeMargin, 0.0f, 1024.0f);
		c.orientedBoxes = I(kTune, "OrientedBoxes", 1, 0, 1) != 0;
		c.devTools = I(kTune, "DevTools", 0, 0, 1) != 0;
		c.compareKey = I(kTune, "CompareKey", 0, 0, 255);
		c.logInterval = I(kTune, "LogInterval", d.logInterval, 60, 100000);
		c.debugBoxes = I(kTune, "DebugBoxes", 0, 0, 1) != 0;
		return c;
	}
}

namespace OcclusionBridge {
	void LogLine(const char* text) {
		Logger::Log("[Occlusion] %s", text);
	}

	// Called from our deferred init (Main.cpp).
	void InstallFromSettings() {
		Occlusion::Install(Read());
	}

	// Called at the end of SettingManager::LoadSettings (menu edits, toml reload).
	void OnSettingsLoaded() {
		if (Occlusion::GetState() == Occlusion::kInstalled) Occlusion::Reload(Read());
	}
}
