#pragma once

struct ImVec4;

namespace MenuTheme {
	struct Colors {
		UInt8	accent[3];
		UInt8	background[3];
		UInt8	panel[3];
		UInt8	frame[3];
		UInt8	text[3];
	};

	// Applies the theme built from the five base colors (0..255 RGB), scaled for the screen height.
	// Hover, pressed, border and muted shades are derived from them.
	void		Apply(float scale, const Colors& colors);
	// The mouse cursor fill: the accent color, or the given color when matchAccent is false.
	unsigned	CursorFill(bool matchAccent, const UInt8 custom[3]);

	// Colors the UI code draws with directly.
	ImVec4		Accent();
	ImVec4		AccentDim();
	ImVec4		Muted();
	ImVec4		Warning();
}
