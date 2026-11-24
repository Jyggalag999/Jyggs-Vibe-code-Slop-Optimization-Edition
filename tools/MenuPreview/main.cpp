// Menu preview: runs the real menu pages (MenuUI, PresetsPage, LocationsWindow) against fake data, in a plain
// Win32 + Direct3D 9 window, or renders one frame to a PNG (--out) so the layout can be checked without the game.
//
//   MenuPreview.exe --window settings|locations [--tab presets|current] [--select <page path>]
//                   [--preset <name>] [--feature 0..3] [--size WxH] [--out file.png] [--frames N]
//                   [--target <editor id>] [--interior] [--defaults file.toml] [--data <Data folder>]
#include "PreviewData.h"
#include "../../src/core/Menu/MenuBackend.h"
#include "../../src/core/Menu/MenuTheme.h"
#include "../../src/core/Menu/MenuUI.h"
#include "../../src/core/Menu/MenuData.h"
#include "../../src/core/Menu/LocationsWindow.h"
#include "../../src/core/Menu/PresetsPage.h"
#include "../../src/core/Menu/Fonts.h"
#include "../../lib/imgui/imgui.h"
#include "../../lib/imgui/imgui_internal.h"
#include "../../lib/imgui/backends/imgui_impl_dx9.h"
#include "../../lib/imgui/backends/imgui_impl_win32.h"
#include <d3d9.h>
#include <wincodec.h>
#include <shlwapi.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void Logger::Log(const char* fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	printf("%s\n", buf);
	fflush(stdout);
}

// ------------------------------------------------------------------ MenuBackend (the game's is tied to the engine)
namespace {
	ImFont*	gRegular = nullptr;
	ImFont*	gSemiBold = nullptr;
	float	gScale = 1.0f;
}
ImFont* MenuBackend::FontRegular() { return gRegular; }
ImFont* MenuBackend::FontSemiBold() { return gSemiBold; }
float MenuBackend::Scale() { return gScale; }
void MenuBackend::RequestStyleRefresh() {}

// ------------------------------------------------------------------ globals
namespace {
	struct Options {
		std::string window = "settings", tab, sub, capture, select, preset, out, defaults, dataDir, target = "Goodsprings";
		int feature = 0, frames = 3, width = 1920, height = 1080;
		bool interior = false, conflict = false;
	} opt;

	HWND					gHwnd = nullptr;
	IDirect3D9*				gD3D = nullptr;
	IDirect3DDevice9*		gDev = nullptr;
	D3DPRESENT_PARAMETERS	gPP = {};
	bool					gQuit = false;
	UINT					gPendingW = 0, gPendingH = 0;
	unsigned char			gLastStyle[5][3] = {};

	std::string ExeDir() {
		char p[MAX_PATH];
		GetModuleFileNameA(nullptr, p, MAX_PATH);
		PathRemoveFileSpecA(p);
		return p;
	}

	// Searches upward from the exe folder for a relative path.
	std::string FindUp(const std::string& rel) {
		std::string dir = ExeDir();
		for (int i = 0; i < 8 && !dir.empty(); i++) {
			const std::string cand = dir + "\\" + rel;
			if (GetFileAttributesA(cand.c_str()) != INVALID_FILE_ATTRIBUTES) return cand;
			char p[MAX_PATH];
			strncpy(p, dir.c_str(), MAX_PATH - 1);
			p[MAX_PATH - 1] = 0;
			PathRemoveFileSpecA(p);
			if (dir == p) break;
			dir = p;
		}
		return "";
	}

	void BuildFonts(float scale) {
		ImGuiIO& io = ImGui::GetIO();
		const float size = floorf(17.0f * scale + 0.5f);
		ImGui_ImplDX9_InvalidateDeviceObjects();
		io.Fonts->Clear();
		gRegular = io.Fonts->AddFontFromMemoryCompressedTTF(InterRegular_compressed_data, InterRegular_compressed_size, size);
		gSemiBold = io.Fonts->AddFontFromMemoryCompressedTTF(InterSemiBold_compressed_data, InterSemiBold_compressed_size, size);
		io.FontDefault = gRegular;
		gScale = scale;
	}

	void ApplyTheme(float scale, bool force) {
		static const char* keys[5] = { "AccentColor", "BackgroundColor", "PanelColor", "FrameColor", "TextColor" };
		unsigned char cur[5][3];
		bool changed = force;
		for (int i = 0; i < 5; i++) {
			Data().StyleRgb(keys[i], cur[i]);
			if (memcmp(cur[i], gLastStyle[i], 3)) changed = true;
		}
		if (!changed) return;
		memcpy(gLastStyle, cur, sizeof(cur));
		MenuTheme::Colors c;
		memcpy(c.accent, cur[0], 3); memcpy(c.background, cur[1], 3); memcpy(c.panel, cur[2], 3);
		memcpy(c.frame, cur[3], 3); memcpy(c.text, cur[4], 3);
		MenuTheme::Apply(scale, c);
	}

	float ScaleFor(int height) {
		float s = (float)height / 1080.0f;
		return std::min(3.0f, std::max(0.5f, s));
	}

	// ---- PNG
	bool WritePng(const std::string& path, const unsigned char* bgra, UINT w, UINT h, UINT pitch) {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		IWICImagingFactory* factory = nullptr;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
		IWICStream* stream = nullptr;
		IWICBitmapEncoder* enc = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		IPropertyBag2* props = nullptr;
		bool ok = false;
		std::wstring wp(path.begin(), path.end());
		if (SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(wp.c_str(), GENERIC_WRITE)) &&
			SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
			SUCCEEDED(enc->CreateNewFrame(&frame, &props)) && SUCCEEDED(frame->Initialize(props)) && SUCCEEDED(frame->SetSize(w, h))) {
			WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
			if (SUCCEEDED(frame->SetPixelFormat(&fmt)) && SUCCEEDED(frame->WritePixels(h, pitch, pitch * h, (BYTE*)bgra)) &&
				SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit())) ok = true;
		}
		if (props) props->Release();
		if (frame) frame->Release();
		if (enc) enc->Release();
		if (stream) stream->Release();
		factory->Release();
		return ok;
	}

	// ---- device
	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
		switch (msg) {
		case WM_SIZE:
			if (wp != SIZE_MINIMIZED) { gPendingW = LOWORD(lp); gPendingH = HIWORD(lp); }
			return 0;
		case WM_DESTROY:
			gQuit = true;
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcA(hwnd, msg, wp, lp);
	}

	bool CreateDevice(bool offscreen) {
		gD3D = Direct3DCreate9(D3D_SDK_VERSION);
		if (!gD3D) return false;
		gPP = {};
		gPP.Windowed = TRUE;
		gPP.SwapEffect = D3DSWAPEFFECT_DISCARD;
		gPP.BackBufferFormat = D3DFMT_UNKNOWN;
		gPP.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
		if (offscreen) { gPP.BackBufferWidth = 64; gPP.BackBufferHeight = 64; }
		return SUCCEEDED(gD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, gHwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &gPP, &gDev)) ||
			SUCCEEDED(gD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, gHwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &gPP, &gDev));
	}

	void ResetDevice(UINT w, UINT h) {
		ImGui_ImplDX9_InvalidateDeviceObjects();
		gPP.BackBufferWidth = w;
		gPP.BackBufferHeight = h;
		if (SUCCEEDED(gDev->Reset(&gPP))) ImGui_ImplDX9_CreateDeviceObjects();
	}

	// ---- one frame of the chosen window
	void DrawMenu(int w, int h, bool offscreen) {
		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGuiIO& io = ImGui::GetIO();
		if (offscreen) { io.DisplaySize = ImVec2((float)w, (float)h); io.DeltaTime = 1.0f / 60.0f; }
		ImGui::NewFrame();
		if (opt.window == "locations") LocationsWindow::Draw();
		else MenuUI::Draw();
		ImGui::EndFrame();
		ImGui::Render();
	}
}

int main(int argc, char** argv) {
	SetProcessDPIAware();
	for (int i = 1; i < argc; i++) {
		const std::string a = argv[i];
		auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
		if (a == "--window") opt.window = next();
		else if (a == "--tab") opt.tab = next();
		else if (a == "--select") opt.select = next();
		else if (a == "--sub") opt.sub = next();
		else if (a == "--capture") opt.capture = next();
		else if (a == "--conflict") opt.conflict = true;
		else if (a == "--preset") opt.preset = next();
		else if (a == "--feature") opt.feature = atoi(next().c_str());
		else if (a == "--out") opt.out = next();
		else if (a == "--frames") opt.frames = std::max(1, atoi(next().c_str()));
		else if (a == "--target") opt.target = next();
		else if (a == "--interior") opt.interior = true;
		else if (a == "--defaults") opt.defaults = next();
		else if (a == "--data") opt.dataDir = next();
		else if (a == "--size") sscanf(next().c_str(), "%dx%d", &opt.width, &opt.height);
		else { printf("unknown option %s\n", a.c_str()); return 2; }
	}
	if (opt.defaults.empty()) opt.defaults = FindUp("resource\\NewVegasReloaded.dll.defaults.toml");
	if (opt.dataDir.empty()) opt.dataDir = FindUp("tools\\MenuPreview\\sample\\Data");
	if (opt.defaults.empty() || opt.dataDir.empty()) { printf("Could not find the defaults file or the sample data (use --defaults / --data).\n"); return 1; }
	if (!Preview::InitData(opt.defaults, opt.dataDir, opt.target, opt.interior)) return 1;

	const bool offscreen = !opt.out.empty();
	WNDCLASSA wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = "MenuPreviewWnd";
	RegisterClassA(&wc);
	RECT r = { 0, 0, opt.width, opt.height };
	DWORD style = WS_OVERLAPPEDWINDOW;
	AdjustWindowRect(&r, style, FALSE);
	gHwnd = CreateWindowA(wc.lpszClassName, "Menu preview", style, 40, 40, r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
	if (!offscreen) ShowWindow(gHwnd, SW_SHOW);
	if (!CreateDevice(offscreen)) { printf("Direct3D 9 device creation failed.\n"); return 1; }

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	ImGui_ImplWin32_Init(gHwnd);
	ImGui_ImplDX9_Init(gDev);
	io.MouseDrawCursor = !offscreen;

	int curH = opt.height;
	BuildFonts(ScaleFor(curH));
	ApplyTheme(gScale, true);

	// What to show first.
	if (opt.window == "locations") {
		LocationsWindow::OnOpened();
		LocationsWindow::ShowTab(opt.tab == "current" ? 1 : 0);
		if (!opt.preset.empty()) PresetsPage::SelectPreset(opt.feature, opt.preset.c_str());
		PresetsPage::ShowSubTab(opt.sub == "used" ? 1 : 0);
	}
	else {
		MenuUI::OnOpened();
		if (!opt.select.empty()) MenuUI::SelectPage(opt.select.c_str());
		if (opt.conflict) Preview::SetUserValue("Main.Main.Occlusion", "ToggleKey", "207");
		if (!opt.capture.empty()) MenuUI::BeginRebind(opt.capture.c_str());
	}

	if (offscreen) {
		IDirect3DSurface9 *rt = nullptr, *sys = nullptr, *oldRt = nullptr;
		if (FAILED(gDev->CreateRenderTarget(opt.width, opt.height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) ||
			FAILED(gDev->CreateOffscreenPlainSurface(opt.width, opt.height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr))) {
			printf("Could not create the %dx%d render target.\n", opt.width, opt.height);
			return 1;
		}
		gDev->GetRenderTarget(0, &oldRt);
		gDev->SetRenderTarget(0, rt);
		for (int f = 0; f < opt.frames; f++) {
			ApplyTheme(gScale, false);
			DrawMenu(opt.width, opt.height, true);
			gDev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 8, 9, 12), 1.0f, 0);
			gDev->BeginScene();
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			gDev->EndScene();
		}
		if (FAILED(gDev->GetRenderTargetData(rt, sys))) { printf("GetRenderTargetData failed.\n"); return 1; }
		D3DLOCKED_RECT lr;
		if (FAILED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
		std::vector<unsigned char> pix((size_t)lr.Pitch * opt.height);
		memcpy(pix.data(), lr.pBits, pix.size());
		sys->UnlockRect();
		for (size_t i = 3; i < pix.size(); i += 4) pix[i] = 255;
		const bool ok = WritePng(opt.out, pix.data(), opt.width, opt.height, lr.Pitch);
		printf(ok ? "wrote %s (%dx%d)\n" : "could not write %s (%dx%d)\n", opt.out.c_str(), opt.width, opt.height);
		return ok ? 0 : 1;
	}

	MSG msg;
	while (!gQuit && !Preview::CloseRequested()) {
		while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
			if (msg.message == WM_QUIT) gQuit = true;
		}
		if (gQuit) break;
		if (gPendingW && gPendingH) {
			ResetDevice(gPendingW, gPendingH);
			if ((int)gPendingH != curH) { curH = (int)gPendingH; BuildFonts(ScaleFor(curH)); ApplyTheme(gScale, true); }
			gPendingW = gPendingH = 0;
		}
		ApplyTheme(gScale, false);
		DrawMenu(0, 0, false);
		gDev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 8, 9, 12), 1.0f, 0);
		if (gDev->BeginScene() >= 0) {
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			gDev->EndScene();
		}
		if (gDev->Present(nullptr, nullptr, nullptr, nullptr) == D3DERR_DEVICELOST && gDev->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
			ResetDevice(gPP.BackBufferWidth, gPP.BackBufferHeight);
	}
	return 0;
}
