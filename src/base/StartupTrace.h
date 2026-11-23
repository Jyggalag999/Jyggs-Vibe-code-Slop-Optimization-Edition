#pragma once

// Startup trace for "game won't reach the main menu" reports.
// Writes [Startup] lines to NewVegasReloaded.log (flushed per line, so the last line shows how far
// NVR got before a hang or crash) and an environment dump (d3d9 runtime / DXVK version, every
// non-system DLL loaded with its version). No hooks, patches or exception filters: crashes are
// left to Crash Logger. Off by default ([_Main.Main.Profiler] StartupLog = true to turn on).
namespace StartupTrace {
	void Begin();                    // right after Logger::Initialize
	void Stage(const char* Name);    // "[Startup] +1.23s Name"
	void DumpModules(const char* When); // all non-system modules loaded at this point
	void FirstFrame();               // logs once, on the first presented frame
	void SetEnabled(bool On);        // after the toml is read ([_Main.Main.Profiler] StartupLog): writes or drops the held lines
}
