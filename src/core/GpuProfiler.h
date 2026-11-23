#pragma once
/*
* GPU profiler: measures how long the GPU spends on each NVR effect and the shadow maps, with D3D9 timestamp
* queries, and logs a table sorted by cost every Main.Main.Profiler IntervalSeconds.
*
* Results are read back kFrames frames later and never waited on, so the profiler doesn't stall the GPU.
* Scopes nest; a scope inside another one with the same name is ignored, so an effect whose dedicated path calls
* EffectRecord::Render is counted once. Names must be strings that live for the whole session (effect names,
* literals). Off (no queries at all) when Main.Main.Profiler Enabled is false.
*
* Each report also says how long the GPU was busy vs waiting per frame (CPU-bound or GPU-bound), the CPU time
* from the start of the world render to the last NVR pass, and the CPU time spent issuing each pass.
*/
#include <d3d9.h>

namespace GpuProfiler {
	void	FrameStart(IDirect3DDevice9* Device);	// when the world render of a frame begins (Main::Render hook)
	void	EndFrame(IDirect3DDevice9* Device);	// once per frame, after the last NVR pass
	void	Release();							// drop all queries (device reset / shutdown)

	int		Begin(IDirect3DDevice9* Device, const char* Name);	// -1 when off or ignored
	void	End(IDirect3DDevice9* Device, int Index);

	// CPU-only measurements that fold into the same report. Names must be literals.
	bool	Active();										// profiler on
	LONGLONG Tick();										// QueryPerformanceCounter ticks (0 when off)
	void	AddCpu(const char* Name, LONGLONG StartTick);	// adds CPU time since StartTick to Name
	void	AddCount(const char* Name, UInt32 Count);		// adds to a counter, reported as avg per frame
}

class GpuScope {
public:
	GpuScope(IDirect3DDevice9* Device, const char* Name) : device(Device), index(GpuProfiler::Begin(Device, Name)) {}
	~GpuScope() { if (index >= 0) GpuProfiler::End(device, index); }
	GpuScope(const GpuScope&) = delete;
	GpuScope& operator=(const GpuScope&) = delete;
private:
	IDirect3DDevice9*	device;
	int					index;
};
