# Changelog

NVR Unofficial Jyggs Vibecoded Slop, newest first. Dates are YYYY-MM-DD.

## v1.2 (2026-11-23)

New name: **NVR Unofficial Jyggs Vibecoded Slop** (it used to be "Jyggs Vibe-code Slop (Optimization Edition)").

**Speed**
- Built-in **occlusion culling** (Conor's Occlusion Culling plugin, integrated): objects, land, grass, trees, NPCs and doors that are completely hidden behind something are not drawn. On by default; you no longer need the separate plugin. Under *Main > Main > Occlusion*.
- Three parallax/terrain speed-ups ported from Vegas Shaders: **SkipEmptyLayers** (land layers that contribute nothing are skipped), **IsoHeightTaps** and **ParallaxLite** (cheaper parallax, a little lossy; both are part of the Performance preset).
- **HiddenObjectCulling** (from Vegas Shaders): later lamp passes of objects that were not visible are skipped.
- The occlusion culling, HiddenObjectCulling, IsoHeightTaps and ParallaxLite defaults are now **on** (each can be switched off in *Main > Main > Performance* / *Occlusion*). See the performance section of the README for what was measured, and what was not.

**Locations (built-in Cartographer)**
- New **Locations window** (Delete key by default): a preset manager for the Cartographer files (create, duplicate, delete, edit and save presets, see which locations use a preset) and a *Current location* tab showing what was applied where you stand.
- **Go**: teleport to any location a preset is used for, from the "Used by" list. Worldspaces land in the middle of their map instead of cell 0,0, and the teleport runs from the game's main loop (an earlier version froze the game).

**Menu**
- The Settings and Locations windows were redesigned: a fixed header bar with search and Save/Revert, a resizable sidebar, value columns that line up (and scroll sideways instead of squashing), sliders whose value no longer sits under the handle, and no more cut-off text.
- **Typing now works** in the menu (search box, value boxes, preset names): keys are read from DirectInput, because the game window never received them in some setups.
- Rows locked by the **Performance preset are editable**. They carry a PRESET badge, Reset goes back to the preset's value, and your edits are saved under `_Preset.Performance` in your toml.
- New **Keybinds** page: rebind every hotkey (menu, Locations, save, occlusion toggle and compare, screenshot, shader trace, fly camera) by pressing the new key, with Clear and Reset, and a warning when two bindings use the same key.
- Preview program for the menu layout (`tools/MenuPreview`, developers only).

## v1.1 (2026-11-23)

Fixes for people whose game wouldn't reach the main menu.
- A popup tells you what is missing when only part of the zip was installed (the DLL without its defaults file or shaders), instead of a black screen and a crash.
- No more crash in the shadow setup when a shadow shader fails to load.
- New `StartupLog` switch under `[_Main.Main.Profiler]` (off by default): logs every startup step, your Windows/game/DXVK versions and every plugin DLL.
- The GPU profiler and cell-change timing logs are off by default.
- DXVK `dxvk.conf` makes the launcher report the same AMD GPU as the game.

## v1.0 (2026-11-23)

First release, everything in one zip. Official NVR 4.4.1 with the Boutique optimizations (no full-frame copies between effects, smaller AO and god-ray buffers, merged lamp shadows, culled shadow cube maps, a lighter depth buffer), a Performance preset (on by default, trims sun and lamp shadows for about 2 ms per frame), the built-in Locations feature (Cartographer inside the DLL, applied during the loading screen), batched script settings, and an optional DXVK 3.1.1 package.
