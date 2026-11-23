#pragma once
/*
* Cell change profiling (Main.Main.Profiler CellChangeTiming): when the player's location changes, measures the next
* 300 frames (frame times, script SetSetting calls, settings reloads, shader switches, Locations apply) and logs one
* [CellChange] line. Runs on the main thread, independent of Locations, so Cartographer.esp (script) and the built-in
* Locations can be compared with the same measurement.
*/
namespace CellProfiler {
	void Update();								// once per frame, main thread
	bool Active();
	LONGLONG Tick();							// QueryPerformanceCounter ticks, 0 when off
	void OnScriptSetting(LONGLONG StartTick);	// a script called NVRSetSetting (time since StartTick)
	void OnReload(LONGLONG StartTick);			// SettingManager::LoadSettings ran
	void OnShaderSwitch(LONGLONG StartTick);	// ShaderManager::SwitchShaderStatus ran
	void OnLocationsApply(float Ms, int Values);	// Locations applied presets (may come before the window opens)

	class Scope {
	public:
		enum Kind { Reload, Switch };
		Scope(Kind K) : kind(K), start(Tick()) {}
		~Scope() { if (!start) return; if (kind == Reload) OnReload(start); else OnShaderSwitch(start); }
	private:
		Kind kind;
		LONGLONG start;
	};
}
