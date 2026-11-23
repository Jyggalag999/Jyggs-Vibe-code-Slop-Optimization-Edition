#include "CellProfiler.h"
#include <deque>
#include <algorithm>

namespace CellProfiler {
	namespace {
		const int WindowFrames = 300;
		const double PreSeconds = 10.0;

		struct Counter { int n = 0; double ms = 0; void Add(double v) { n++; ms += v; } };
		struct Event { LONGLONG tick; double ms; };

		double gMsPerTick = 0.0;
		void* gLast = nullptr;				// location the window logic last saw
		bool gOpen = false;
		int gFrames = 0;
		LONGLONG gLastFrameTick = 0;
		double gMax = 0, gSum = 0;
		int gOver33 = 0, gOver50 = 0;
		Counter gSet, gReload, gSwitch, gPreSet;
		std::string gName;
		bool gInterior = false;
		bool gHaveApply = false;
		float gApplyMs = 0;
		int gApplyValues = 0;
		std::deque<Event> gRecentSets;		// script SetSetting calls of the last 10 s (for calls during the load)

		double Ms(LONGLONG start) {
			LARGE_INTEGER now; QueryPerformanceCounter(&now);
			return (now.QuadPart - start) * gMsPerTick;
		}
	}

	bool Active() { return TheSettingManager && TheSettingManager->SettingsMain.Profiler.CellChangeTiming; }

	LONGLONG Tick() {
		if (!Active()) return 0;
		if (gMsPerTick == 0.0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); gMsPerTick = 1000.0 / (double)f.QuadPart; }
		LARGE_INTEGER t; QueryPerformanceCounter(&t);
		return t.QuadPart;
	}

	void OnScriptSetting(LONGLONG start) {
		if (!start) return;
		double ms = Ms(start);
		LARGE_INTEGER now; QueryPerformanceCounter(&now);
		if (gOpen) { gSet.Add(ms); return; }	// already counted in the open window; don't count it again as "before" the next one
		gRecentSets.push_back({ now.QuadPart, ms });
		while (!gRecentSets.empty() && (now.QuadPart - gRecentSets.front().tick) * gMsPerTick > PreSeconds * 1000.0) gRecentSets.pop_front();
	}
	void OnReload(LONGLONG start) { if (start && gOpen) gReload.Add(Ms(start)); }
	void OnShaderSwitch(LONGLONG start) { if (start && gOpen) gSwitch.Add(Ms(start)); }

	void OnLocationsApply(float ms, int values) {
		if (!Active()) return;
		if (gOpen) { gApplyMs += ms; gApplyValues += values; gHaveApply = true; return; }
		gApplyMs = ms; gApplyValues = values; gHaveApply = true;	// kept for the next window (the hook apply runs before the load)
	}

	static void Close() {
		const char* path = gHaveApply ? "Locations" : (gSet.n + gPreSet.n > 0 ? "Script" : "None");
		char pre[64] = "";
		if (gPreSet.n) snprintf(pre, sizeof(pre), " (+%d before the window, %.1f ms)", gPreSet.n, gPreSet.ms);
		Logger::Log("[CellChange] %s (%s) path=%s  frames=%d max=%.1f ms avg=%.1f ms >33ms=%d >50ms=%d | SetSetting n=%d %.1f ms%s | reloads n=%d %.1f ms | shader switches n=%d %.1f ms | Locations apply %.1f ms (%d values)",
			gName.c_str(), gInterior ? "interior" : "exterior", path, gFrames, gMax, gFrames ? gSum / gFrames : 0.0, gOver33, gOver50,
			gSet.n, gSet.ms, pre, gReload.n, gReload.ms, gSwitch.n, gSwitch.ms, gHaveApply ? gApplyMs : 0.0f, gHaveApply ? gApplyValues : 0);
		gOpen = false;
		gHaveApply = false;
		gApplyMs = 0; gApplyValues = 0;
	}

	void Update() {
		if (!Active()) { if (gOpen) gOpen = false; return; }
		LONGLONG now = Tick();
		const double frameMs = gLastFrameTick ? (now - gLastFrameTick) * gMsPerTick : 0.0;
		gLastFrameTick = now;

		TESObjectCELL* cell = Player ? Player->parentCell : nullptr;
		void* form = nullptr;
		if (cell) form = (cell->IsInterior() || !cell->worldSpace) ? (void*)cell : (void*)cell->worldSpace;
		if (!form) { gLast = nullptr; return; }

		if (form != gLast) {
			gLast = form;
			if (gOpen) Close();	// moved again before 300 frames: log what was measured so far
			gOpen = true;
			gFrames = 0; gMax = 0; gSum = 0; gOver33 = gOver50 = 0;
			gSet = gReload = gSwitch = Counter();
			gPreSet = Counter();
			for (const Event& e : gRecentSets) gPreSet.Add(e.ms);
			gRecentSets.clear();
			gInterior = cell->IsInterior();
			const char* n = ((TESForm*)form)->GetEditorName();
			gName = n ? n : "(no editor id)";
			return;	// the window starts on the next frame
		}
		if (!gOpen) return;
		gFrames++;
		gSum += frameMs;
		if (frameMs > gMax) gMax = frameMs;
		if (frameMs > 33.3) gOver33++;
		if (frameMs > 50.0) gOver50++;
		if (gFrames >= WindowFrames) Close();
	}
}
