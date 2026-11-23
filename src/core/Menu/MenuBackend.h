#pragma once

struct ImFont;

// ImGui setup, per-frame begin/end and device reset handling (Direct3D 9 + Win32 backends).
namespace MenuBackend {
	// Creates the ImGui context and backends the first time it is called with a valid device and window.
	// Returns false if ImGui is not (yet) usable. Cheap once initialised.
	bool		EnsureInit();
	bool		IsInitialised();

	// Marks the style (scale, accent color) for rebuilding before the next frame.
	void		RequestStyleRefresh();

	// Rebuilds the font/style if needed, starts an ImGui frame. Returns false if nothing can be drawn.
	bool		BeginFrame(bool menuOpen);
	// Renders the frame started by BeginFrame to the device.
	void		EndFrame();

	ImFont*		FontRegular();
	ImFont*		FontSemiBold();
	float		Scale();
}
