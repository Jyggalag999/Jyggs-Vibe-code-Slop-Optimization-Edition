#include "GpuProfiler.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
	constexpr int		kFrames = 4;		// results are read kFrames - 1 frames after they were issued
	constexpr size_t	kMaxScopes = 160;	// per frame
	constexpr double	kMaxFrameMs = 250.0;	// longer frames (loading, pauses) are left out of the frame averages

	struct Scope {
		const char*			name = nullptr;
		IDirect3DQuery9*	begin = nullptr;
		IDirect3DQuery9*	end = nullptr;
		int					depth = 0;
		LONGLONG			cpuStart = 0;
	};

	/*
	* GPU timeline of one frame:
	*   start   issued in EndFrame of the previous frame (after its last NVR pass)
	*   work    issued when the next frame's rendering begins (Main::Render hook), or the first scope if earlier
	*   stop    issued in EndFrame of this frame
	* work - start = HUD/menus, Present and the time the GPU waited for the CPU. stop - work = GPU busy on the frame.
	*/
	struct Frame {
		IDirect3DQuery9*	disjoint = nullptr;
		IDirect3DQuery9*	freq = nullptr;
		IDirect3DQuery9*	start = nullptr;
		IDirect3DQuery9*	work = nullptr;
		IDirect3DQuery9*	stop = nullptr;
		std::vector<Scope>	scopes;		// grows to the most scopes seen in one frame, queries are reused
		size_t				used = 0;
		bool				open = false;
		bool				pending = false;	// issued, not read yet
		bool				workIssued = false;
	};

	struct Stat {
		double	totalMs = 0.0;	// GPU, summed over measured frames
		double	maxMs = 0.0;	// GPU, highest single frame
		UInt32	frames = 0;		// measured frames it ran in
		double	cpuMs = 0.0;	// CPU time spent issuing it, summed over all frames
	};

	Frame		gFrames[kFrames];
	int			gCurrent = 0;
	bool		gBroken = false;	// a query could not be created; everything is rebuilt next frame
	std::vector<int> gStack;		// open scopes of the current frame

	std::unordered_map<std::string, Stat> gStats;
	std::unordered_map<std::string, double> gCpuOnly;	// CPU ms summed over the interval (AddCpu)
	std::unordered_map<std::string, double> gCounts;	// counters summed over the interval (AddCount)
	double		gGpuFrameMs = 0.0;	// start -> stop
	double		gGpuBusyMs = 0.0;	// work -> stop
	UInt32		gGpuFrames = 0;
	double		gNvrMs = 0.0;		// GPU, top-level scopes
	double		gNvrCpuMs = 0.0;	// CPU, top-level scopes
	double		gRenderCpuMs = 0.0;	// CPU, Main::Render start -> last NVR pass
	UInt32		gRenderFrames = 0;
	UInt32		gResolvedFrames = 0;
	UInt32		gDroppedFrames = 0;
	UInt32		gCpuFrames = 0;
	LONGLONG	gIntervalStart = 0;

	// Auto A/B benchmark (Main.Main.Profiler AutoBenchmark): flips PerformancePreset every window.
	bool		gBenchActive = false;
	bool		gBenchOrig = false;		// the user's own PerformancePreset value, put back when the benchmark stops
	bool		gBenchState = false;	// preset state of the current window
	LONGLONG	gBenchSettleUntil = 0;
	std::string	gHw;					// "gpu","resolution","renderer" CSV fields, filled once
	std::string	gBenchSpot;				// cell/worldspace when the window started
	const char*	gTag = "";
	constexpr double kSettleSeconds = 3.0;
	LONGLONG	gRenderStart = 0;
	double		gQpcToMs = 0.0;

	bool Enabled() {
		return TheSettingManager && TheSettingManager->SettingsMain.Profiler.Enabled;
	}

	LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
	double QpcMs(LONGLONG aTicks) {
		if (gQpcToMs == 0.0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); gQpcToMs = 1000.0 / double(f.QuadPart); }
		return double(aTicks) * gQpcToMs;
	}

	void SafeRelease(IDirect3DQuery9*& q) { if (q) { q->Release(); q = nullptr; } }

	bool Create(IDirect3DDevice9* Device, D3DQUERYTYPE Type, IDirect3DQuery9*& q) {
		if (q) return true;
		if (FAILED(Device->CreateQuery(Type, &q)) || !q) { q = nullptr; gBroken = true; return false; }
		return true;
	}

	// S_OK = read, S_FALSE = not ready yet, anything else = lost or broken.
	template <typename T> HRESULT Read(IDirect3DQuery9* q, T& v) {
		return q->GetData(&v, sizeof(T), 0);
	}

	void ResetStats() {
		gStats.clear();
		gCpuOnly.clear();
		gCounts.clear();
		gGpuFrameMs = gGpuBusyMs = gNvrMs = gNvrCpuMs = gRenderCpuMs = 0.0;
		gGpuFrames = gRenderFrames = gResolvedFrames = gDroppedFrames = gCpuFrames = 0;
		gIntervalStart = Now();
	}

	// Returns false if the device is lost or the queries broke.
	bool Resolve(Frame& f) {
		f.pending = false;
		BOOL disjoint = TRUE;
		UINT64 freq = 0, start = 0, stop = 0, work = 0;
		HRESULT hr = Read(f.disjoint, disjoint);
		if (hr == S_FALSE) { gDroppedFrames++; return true; }
		if (hr != S_OK) return false;
		if (Read(f.freq, freq) != S_OK || Read(f.start, start) != S_OK || Read(f.stop, stop) != S_OK || disjoint || !freq ||
			(f.workIssued && Read(f.work, work) != S_OK)) {
			gDroppedFrames++;
			return true;
		}
		const double toMs = 1000.0 / double(freq);

		std::unordered_map<std::string, double> frame;	// one frame's time per name
		double nvr = 0.0;
		for (size_t i = 0; i < f.used; i++) {
			Scope& s = f.scopes[i];
			UINT64 b = 0, e = 0;
			if (Read(s.begin, b) != S_OK || Read(s.end, e) != S_OK) { gDroppedFrames++; return true; }
			const double ms = e > b ? double(e - b) * toMs : 0.0;
			frame[s.name] += ms;
			if (s.depth == 0) nvr += ms;
			if (b && (!work || b < work)) work = b;	// a pass that ran before the Main::Render hook
		}

		for (auto& it : frame) {
			Stat& st = gStats[it.first];
			st.totalMs += it.second;
			if (it.second > st.maxMs) st.maxMs = it.second;
			st.frames++;
		}
		gNvrMs += nvr;
		gResolvedFrames++;

		const double frameMs = stop > start ? double(stop - start) * toMs : 0.0;
		if (frameMs > 0.0 && frameMs < kMaxFrameMs) {
			if (!work || work < start || work > stop) work = start;	// no marker: count the whole frame as busy
			gGpuFrameMs += frameMs;
			gGpuBusyMs += double(stop - work) * toMs;
			gGpuFrames++;
		}
		return true;
	}

	void FillHw(IDirect3DDevice9* Device) {
		std::string gpu = "?", res = "?", rend = "native D3D9";
		IDirect3D9* d3d = nullptr;
		if (SUCCEEDED(Device->GetDirect3D(&d3d)) && d3d) {
			D3DADAPTER_IDENTIFIER9 id;
			if (SUCCEEDED(d3d->GetAdapterIdentifier(0, 0, &id))) gpu = id.Description;
			d3d->Release();
		}
		IDirect3DSurface9* bb = nullptr;
		if (SUCCEEDED(Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
			D3DSURFACE_DESC d;
			if (SUCCEEDED(bb->GetDesc(&d))) res = std::to_string(d.Width) + "x" + std::to_string(d.Height);
			bb->Release();
		}
		char path[MAX_PATH] = {};
		if (HMODULE m = GetModuleHandleA("d3d9.dll")) {
			if (GetModuleFileNameA(m, path, MAX_PATH)) {
				std::string p = path;
				for (char& c : p) c = (char)tolower((unsigned char)c);
				if (p.find("\\windows\\") == std::string::npos) rend = "wrapper (DXVK?)";
			}
		}
		gHw = "\""+gpu+"\"" + "," + res + "," + "\""+rend+"\"";
	}

	void BenchSet(bool On) {
		TheSettingManager->SetSetting("Main.Main.Performance", "PerformancePreset", On);
		TheSettingManager->LoadSettings();
	}

	std::string Spot() {
		std::string s = "(none)";
		if (Player && Player->parentCell) {
			const char* cell = Player->parentCell->GetEditorName();
			const char* ws = nullptr;
			if (TESWorldSpace* w = Player->GetWorldSpace()) ws = w->GetEditorName();
			s = std::string(ws ? ws : "interior") + "/" + (cell ? cell : "?");
		}
		return s;
	}

	void LogStats(double aSeconds, bool aCsv = false) {
		if (!gResolvedFrames) return;
		const double frames = double(gResolvedFrames);
		const double cpuFrames = gCpuFrames ? double(gCpuFrames) : 1.0;
		const double gpuFrame = gGpuFrames ? gGpuFrameMs / gGpuFrames : 0.0;
		const double gpuBusy = gGpuFrames ? gGpuBusyMs / gGpuFrames : 0.0;
		const double gpuIdle = gpuFrame > gpuBusy ? gpuFrame - gpuBusy : 0.0;
		const double idleShare = gpuFrame > 0.0 ? gpuIdle / gpuFrame : 0.0;
		const double nvr = gNvrMs / frames;
		const double nvrCpu = gNvrCpuMs / cpuFrames;
		const double renderCpu = gRenderFrames ? gRenderCpuMs / gRenderFrames : 0.0;
		const double fps = aSeconds > 0.0 ? gCpuFrames / aSeconds : 0.0;

		Logger::Log("[GPU]%s %.1f s, %.1f FPS (%.2f ms per frame). %u frames measured, %u skipped.",
			gTag, aSeconds, fps, fps > 0.0 ? 1000.0 / fps : 0.0, gResolvedFrames, gDroppedFrames);
		// The world render (everything but NVR's passes) is CPU-bound when the GPU finishes it no later than the CPU
		// finishes sending it: the GPU is drawing as fast as the draw calls arrive. NVR's passes run after it, on the
		// GPU only, overlapping the CPU's game logic. Idle GPU time left at the end of the frame is the GPU headroom.
		const double worldGpu = gpuBusy > nvr ? gpuBusy - nvr : 0.0;
		const double worldCpu = renderCpu > nvrCpu ? renderCpu - nvrCpu : 0.0;
		const bool worldCpuBound = worldGpu <= worldCpu * 1.05 + 0.3;
		Logger::Log("[GPU]   World render: GPU %.2f ms, CPU draw calls %.2f ms -> %s.", worldGpu, worldCpu,
			worldCpuBound ? "CPU-bound (the GPU keeps pace with the draw calls)" : "GPU-bound (the GPU finishes after the CPU)");
		Logger::Log("[GPU]   NVR passes: GPU %.2f ms, CPU %.2f ms. CPU game logic, HUD and Present: %.2f ms.",
			nvr, nvrCpu, fps > 0.0 && 1000.0 / fps > renderCpu ? 1000.0 / fps - renderCpu : 0.0);
		Logger::Log("[GPU]   GPU frame %.2f ms: busy %.2f ms, idle %.2f ms (%.0f%%, includes HUD and Present).",
			gpuFrame, gpuBusy, gpuIdle, idleShare * 100.0);
		const char* verdict;
		if (idleShare < 0.05)
			verdict = "GPU-bound. The GPU has no spare time, so cutting GPU cost (NVR passes first) raises FPS";
		else if (worldCpuBound)
			verdict = "CPU-bound. The CPU sets the frame time (world draw calls + game logic); GPU savings won't raise FPS until the headroom is gone";
		else
			verdict = "Mixed. The world render waits on the GPU, but the GPU still idles at the end of the frame";
		Logger::Log("[GPU]   Verdict: %s. GPU headroom %.2f ms per frame.", verdict, gpuIdle);

		std::vector<std::pair<std::string, Stat>> rows(gStats.begin(), gStats.end());
		std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.totalMs + a.second.cpuMs > b.second.totalMs + b.second.cpuMs; });
		Logger::Log("[GPU]   %-28s %9s %9s %7s %9s %8s", "Pass", "GPU ms", "GPU max", "% busy", "CPU ms", "% frames");
		for (const auto& r : rows) {
			const double avg = r.second.totalMs / frames;	// per measured frame, so passes that run rarely rank lower
			Logger::Log("[GPU]   %-28s %9.3f %9.3f %6.1f%% %9.3f %7.0f%%", r.first.c_str(), avg, r.second.maxMs,
				gpuBusy > 0.0 ? avg * 100.0 / gpuBusy : 0.0, r.second.cpuMs / cpuFrames, r.second.frames * 100.0 / frames);
		}
		if (!gCpuOnly.empty() || !gCounts.empty()) {
			std::vector<std::pair<std::string, double>> cpu(gCpuOnly.begin(), gCpuOnly.end());
			std::sort(cpu.begin(), cpu.end());
			Logger::Log("[GPU]   CPU-only timings (avg per frame, all frames):");
			for (const auto& r : cpu) Logger::Log("[GPU]   %-28s %9.3f ms", r.first.c_str(), r.second / cpuFrames);
			std::vector<std::pair<std::string, double>> cnt(gCounts.begin(), gCounts.end());
			std::sort(cnt.begin(), cnt.end());
			Logger::Log("[GPU]   Counts (avg per frame, all frames):");
			for (const auto& r : cnt) Logger::Log("[GPU]   %-28s %9.1f", r.first.c_str(), r.second / cpuFrames);
		}

		if (aCsv) {
			const std::string spotEnd = Spot();
			std::string passes;
			char buf[160];
			for (const auto& r : rows) {
				const double g = r.second.totalMs / frames, c = r.second.cpuMs / cpuFrames;
				if (g < 0.05 && c < 0.05) continue;
				snprintf(buf, sizeof(buf), "%s%s=%.3f/%.3f", passes.empty() ? "" : ";", r.first.c_str(), g, c);
				passes += buf;
			}
			for (const auto& r : gCpuOnly) {
				snprintf(buf, sizeof(buf), "%scpu:%s=%.3f", passes.empty() ? "" : ";", r.first.c_str(), r.second / cpuFrames);
				passes += buf;
			}
			if (FILE* f = fopen("NewVegasReloaded.benchmark.csv", "ab")) {
				fseek(f, 0, SEEK_END);
				if (ftell(f) == 0)
					fputs("time,gpu,resolution,renderer,spot_start,spot_end,preset,seconds,fps,frame_ms,world_gpu_ms,world_cpu_ms,nvr_gpu_ms,nvr_cpu_ms,gpu_busy_ms,gpu_idle_ms,passes_gpu_cpu_ms\r\n", f);
				SYSTEMTIME t; GetLocalTime(&t);
				fprintf(f, "%02d:%02d:%02d,%s,\"%s\",\"%s\",%s,%.1f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,\"%s\"\r\n",
					t.wHour, t.wMinute, t.wSecond, gHw.c_str(), gBenchSpot.c_str(), spotEnd.c_str(), gBenchState ? "ON" : "OFF",
					aSeconds, fps, fps > 0.0 ? 1000.0 / fps : 0.0, worldGpu, worldCpu, nvr, nvrCpu, gpuBusy, gpuIdle, passes.c_str());
				fclose(f);
			}
		}
	}
}

namespace GpuProfiler {

	bool Active() { return Enabled(); }

	LONGLONG Tick() { return Enabled() ? Now() : 0; }

	void AddCpu(const char* Name, LONGLONG StartTick) {
		if (!StartTick || !Name || !Enabled()) return;
		gCpuOnly[Name] += QpcMs(Now() - StartTick);
	}

	void AddCount(const char* Name, UInt32 Count) {
		if (!Name || !Enabled()) return;
		gCounts[Name] += Count;
	}

	void Release() {
		for (Frame& f : gFrames) {
			SafeRelease(f.disjoint); SafeRelease(f.freq); SafeRelease(f.start); SafeRelease(f.work); SafeRelease(f.stop);
			for (Scope& s : f.scopes) { SafeRelease(s.begin); SafeRelease(s.end); }
			f.scopes.clear();
			f.used = 0; f.open = false; f.pending = false; f.workIssued = false;
		}
		gStack.clear();
		gRenderStart = 0;
		gBroken = false;
	}

	void FrameStart(IDirect3DDevice9* Device) {
		if (!Device || gBroken || !Enabled()) return;
		Frame& f = gFrames[gCurrent];
		if (!f.open || f.workIssued) return;	// only the first world render of a frame counts
		if (!Create(Device, D3DQUERYTYPE_TIMESTAMP, f.work)) return;
		f.work->Issue(D3DISSUE_END);
		f.workIssued = true;
		gRenderStart = Now();
	}

	int Begin(IDirect3DDevice9* Device, const char* Name) {
		if (!Device || !Name || gBroken || !Enabled()) return -1;
		Frame& f = gFrames[gCurrent];
		if (!f.open || f.used >= kMaxScopes) return -1;
		for (int i : gStack)
			if (!strcmp(f.scopes[i].name, Name)) return -1;	// same pass already being timed (dedicated path -> base Render)

		if (f.used == f.scopes.size()) f.scopes.emplace_back();
		Scope& s = f.scopes[f.used];
		if (!Create(Device, D3DQUERYTYPE_TIMESTAMP, s.begin) || !Create(Device, D3DQUERYTYPE_TIMESTAMP, s.end)) return -1;
		s.name = Name;
		s.depth = (int)gStack.size();
		s.begin->Issue(D3DISSUE_END);
		s.cpuStart = Now();
		gStack.push_back((int)f.used);
		return (int)f.used++;
	}

	void End(IDirect3DDevice9* Device, int Index) {
		Frame& f = gFrames[gCurrent];
		if (Index < 0 || (size_t)Index >= f.used) return;
		auto it = std::find(gStack.begin(), gStack.end(), Index);
		if (it == gStack.end()) return;	// frame changed under it
		gStack.erase(it);
		Scope& s = f.scopes[Index];
		s.end->Issue(D3DISSUE_END);
		const double cpu = QpcMs(Now() - s.cpuStart);
		gStats[s.name].cpuMs += cpu;
		if (s.depth == 0) gNvrCpuMs += cpu;
	}

	void EndFrame(IDirect3DDevice9* Device) {
		if (!Device) return;
		if (!Enabled()) {
			if (gFrames[0].disjoint || gFrames[1].disjoint) { Release(); ResetStats(); }
			return;
		}
		if (gBroken) Release();	// a query failed (device lost or reset): rebuild from scratch
		if (!gIntervalStart) ResetStats();

		Frame& cur = gFrames[gCurrent];
		if (cur.open) {
			// Close scopes left open (an early return between Begin and End).
			for (int i : gStack) cur.scopes[i].end->Issue(D3DISSUE_END);
			gStack.clear();
			cur.stop->Issue(D3DISSUE_END);
			cur.disjoint->Issue(D3DISSUE_END);
			cur.freq->Issue(D3DISSUE_END);
			cur.open = false;
			cur.pending = true;
		}
		if (gRenderStart) {
			const double ms = QpcMs(Now() - gRenderStart);
			if (ms < kMaxFrameMs) { gRenderCpuMs += ms; gRenderFrames++; }
			gRenderStart = 0;
		}
		gCpuFrames++;

		gCurrent = (gCurrent + 1) % kFrames;
		Frame& f = gFrames[gCurrent];
		if (f.pending && !Resolve(f)) { Release(); return; }	// device lost: start over next frame

		if (!Create(Device, D3DQUERYTYPE_TIMESTAMPDISJOINT, f.disjoint) || !Create(Device, D3DQUERYTYPE_TIMESTAMPFREQ, f.freq) ||
			!Create(Device, D3DQUERYTYPE_TIMESTAMP, f.start) || !Create(Device, D3DQUERYTYPE_TIMESTAMP, f.stop)) {
			static bool sLogged = false;
			if (!sLogged) { sLogged = true; Logger::Log("[GPU] Could not create timestamp queries; the GPU profiler retries every frame."); }
			return;
		}
		f.disjoint->Issue(D3DISSUE_BEGIN);
		f.start->Issue(D3DISSUE_END);
		f.used = 0;
		f.workIssued = false;
		f.open = true;

		if (gHw.empty()) FillHw(Device);
		const auto& prof = TheSettingManager->SettingsMain.Profiler;
		const bool bench = prof.AutoBenchmark;
		if (bench && !gBenchActive) {
			gBenchActive = true;
			gBenchOrig = TheSettingManager->GetSettingI("Main.Main.Performance", "PerformancePreset") != 0;
			gBenchState = false;
			BenchSet(false);
			gBenchSettleUntil = Now() + LONGLONG(kSettleSeconds * 1000.0 / QpcMs(1));
			gBenchSpot = Spot();
			gTag = " [preset OFF]";
			Logger::Log("[GPU] Auto benchmark started (preset OFF first, %.0f s windows, %.0f s settling after each flip).", prof.AutoBenchmarkSeconds, kSettleSeconds);
			ResetStats();
			return;
		}
		if (!bench && gBenchActive) {
			gBenchActive = false;
			gTag = "";
			BenchSet(gBenchOrig);
			Logger::Log("[GPU] Auto benchmark stopped; PerformancePreset back to %s.", gBenchOrig ? "true" : "false");
			ResetStats();
			return;
		}
		if (bench && Now() < gBenchSettleUntil) { ResetStats(); return; }	// settling after a flip: texture rebuilds, SpreadLod warm-up
		const double seconds = QpcMs(Now() - gIntervalStart) / 1000.0;
		float interval = bench ? prof.AutoBenchmarkSeconds : prof.IntervalSeconds;
		if (interval < 1.0f) interval = 1.0f;
		if (seconds >= interval) {
			LogStats(seconds, bench);
			if (bench) {
				gBenchState = !gBenchState;
				BenchSet(gBenchState);
				gTag = gBenchState ? " [preset ON]" : " [preset OFF]";
				gBenchSettleUntil = Now() + LONGLONG(kSettleSeconds * 1000.0 / QpcMs(1));
				gBenchSpot = Spot();
			}
			ResetStats();
		}
	}
}
