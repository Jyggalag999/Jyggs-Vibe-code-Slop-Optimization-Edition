#pragma once
#include <string>

// Small widgets shared by the settings window, the Locations window and the preset editor.
namespace MenuWidgets {
	// Tooltip for the last item (wrapped, after a short hover delay). Empty text does nothing.
	void		Tip(const std::string& text);
	// Muted, wrapped text.
	void		Muted(const char* text);
	void		MutedF(const char* fmt, ...);
	// Semi-bold title, optionally with a muted wrapped subtitle below.
	void		PageHeader(const char* title, const char* subtitle = nullptr);
	bool		ToggleSwitch(const char* id, bool* value);
	float		ToggleSwitchWidth();
	// Small outlined label (accent colour, or warning colour).
	void		Badge(const char* text, bool warning = false);
	float		BadgeWidth(const char* text);
	// N buttons that share the available width (never narrower than their text + padding).
	// enabled may be null. Returns the index of the clicked button or -1. primary = index drawn with the accent fill or -1.
	int			ToolbarButtons(const char* const* labels, int n, const bool* enabled = nullptr, int primary = -1);
	// Status text (error = warning colour), wrapped. Empty text does nothing.
	void		StatusLine(const std::string& text, bool error);
	// Draggable vertical divider between two panes laid out with SameLine. width is the left pane's width.
	// Returns true while dragging. Call right after the left pane's EndChild.
	bool		SplitterV(const char* id, float* width, float minW, float maxW, float height);
	// Flat tab-like button; the active one has the accent fill and an underline. Returns true when clicked.
	bool		TabButton(const char* label, bool active);
	// One line of text cut with "..." to the width (default: the rest of the line). Returns true when it was cut (the caller
	// shows the full text as a tooltip).
	bool		Ellipsized(const std::string& text, float width = -1.0f);
}
