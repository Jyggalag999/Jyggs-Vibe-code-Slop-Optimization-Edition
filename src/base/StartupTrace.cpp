#include "StartupTrace.h"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include <string>
#include <vector>

namespace {
	ULONGLONG StartTick = 0;
	bool FirstFrameLogged = false;
	// Lines are held until the toml has been read (SetEnabled), then written or dropped.
	bool Decided = false;
	bool Enabled = false;
	std::vector<std::string> Pending;

	void Line(const char* Format, ...) {
		if (Decided && !Enabled) return;
		char text[1024];
		va_list args;
		va_start(args, Format);
		_vsnprintf_s(text, sizeof(text), _TRUNCATE, Format, args);
		va_end(args);
		if (Decided) Logger::Log("%s", text);
		else Pending.push_back(text);
	}

	// "ProductName ProductVersion" (or FileVersion) from a module's version resource; "" when absent.
	void VersionOf(const char* Path, char* Out, size_t OutSize) {
		Out[0] = 0;
		DWORD handle = 0;
		DWORD size = GetFileVersionInfoSizeA(Path, &handle);
		if (!size) return;
		char* data = new char[size];
		if (GetFileVersionInfoA(Path, handle, size, data)) {
			struct LangCp { WORD Lang; WORD Cp; } *tr = NULL;
			UINT trSize = 0;
			char* product = NULL; char* version = NULL; UINT len = 0;
			if (VerQueryValueA(data, "\\VarFileInfo\\Translation", (LPVOID*)&tr, &trSize) && trSize >= sizeof(LangCp)) {
				char key[64];
				_snprintf_s(key, sizeof(key), _TRUNCATE, "\\StringFileInfo\\%04x%04x\\ProductName", tr->Lang, tr->Cp);
				if (!VerQueryValueA(data, key, (LPVOID*)&product, &len) || !len) product = NULL;
				_snprintf_s(key, sizeof(key), _TRUNCATE, "\\StringFileInfo\\%04x%04x\\ProductVersion", tr->Lang, tr->Cp);
				if (!VerQueryValueA(data, key, (LPVOID*)&version, &len) || !len) version = NULL;
			}
			if (version) {
				_snprintf_s(Out, OutSize, _TRUNCATE, "%s %s", product ? product : "", version);
			}
			else {
				VS_FIXEDFILEINFO* fixed = NULL;
				if (VerQueryValueA(data, "\\", (LPVOID*)&fixed, &len) && fixed)
					_snprintf_s(Out, OutSize, _TRUNCATE, "%s %u.%u.%u.%u", product ? product : "",
						HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
						HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
			}
		}
		delete[] data;
	}

	void LogModule(const char* Label, HMODULE Module) {
		char path[MAX_PATH] = {};
		char version[160] = {};
		if (!Module || !GetModuleFileNameA(Module, path, MAX_PATH)) {
			Line("[Startup]   %s: not loaded", Label);
			return;
		}
		VersionOf(path, version, sizeof(version));
		// DXVK stamps its DLLs with a Windows-style version (10.0.x), not its own; the real one is in FalloutNV_d3d9.log.
		if (!strncmp(version, "DXVK", 4))
			strcpy_s(version, "DXVK, real version in FalloutNV_d3d9.log");
		Line("[Startup]   %s: %s [%s]", Label, path, version[0] ? version : "no version info");
	}
}

void StartupTrace::Begin() {
	StartTick = GetTickCount64();
	Stage("NVSE plugin load");

	typedef LONG(WINAPI* RtlGetVersionFn)(OSVERSIONINFOW*);
	OSVERSIONINFOW os = { sizeof(os) };
	RtlGetVersionFn rtlGetVersion = (RtlGetVersionFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
	if (rtlGetVersion && rtlGetVersion(&os) == 0)
		Line("[Startup]   Windows %lu.%lu build %lu", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);
	if (GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version"))
		Line("[Startup]   Running under Wine/Proton");

	LogModule("Game", GetModuleHandleA(NULL));
	LogModule("d3d9.dll", GetModuleHandleA("d3d9.dll"));

	char cwd[MAX_PATH] = {};
	GetCurrentDirectoryA(MAX_PATH, cwd);
	Line("[Startup]   Working folder: %s", cwd);
}

void StartupTrace::Stage(const char* Name) {
	Line("[Startup] +%.2fs %s", (GetTickCount64() - StartTick) / 1000.0, Name);
}

void StartupTrace::DumpModules(const char* When) {
	if (Decided && !Enabled) return;
	char windowsDir[MAX_PATH] = {};
	UINT windowsLen = GetWindowsDirectoryA(windowsDir, MAX_PATH);

	HMODULE modules[1024];
	DWORD needed = 0;
	if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
		Line("[Startup] Module list (%s): EnumProcessModules failed (%lu)", When, GetLastError());
		return;
	}
	DWORD count = min(needed / sizeof(HMODULE), (DWORD)(sizeof(modules) / sizeof(HMODULE)));
	Line("[Startup] Non-system modules loaded (%s), %lu modules total:", When, count);
	for (DWORD i = 1; i < count; i++) { // 0 is the exe, logged in Begin
		char path[MAX_PATH] = {};
		if (!GetModuleFileNameA(modules[i], path, MAX_PATH)) continue;
		bool system = windowsLen && !_strnicmp(path, windowsDir, windowsLen);
		const char* name = strrchr(path, '\\');
		name = name ? name + 1 : path;
		// d3d9 is always worth seeing, system or not.
		if (system && _stricmp(name, "d3d9.dll")) continue;
		LogModule(name, modules[i]);
	}
	Line("[Startup] End of module list");
}

void StartupTrace::FirstFrame() {
	if (FirstFrameLogged) return;
	FirstFrameLogged = true;
	Stage("First frame presented (renderer is running)");
}

void StartupTrace::SetEnabled(bool On) {
	Decided = true;
	Enabled = On;
	if (On)
		for (const std::string& text : Pending) Logger::Log("%s", text.c_str());
	Pending.clear();
	Pending.shrink_to_fit();
}
