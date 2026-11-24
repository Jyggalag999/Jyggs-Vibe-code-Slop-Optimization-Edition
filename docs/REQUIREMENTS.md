# NVR Unofficial Jyggs Vibecoded Slop - requirements and compatibility

Covers the build with the new menu and the built-in Cartographer ("Locations"). Status: planned, not
built or tested in game yet. "Checked" below means checked against the plugins' source code, not tested in
game.

## Required

| Mod | Why |
|---|---|
| Fallout: New Vegas 1.4.0.525 (the standard Steam/GOG exe, 4GB-patched as usual) | All engine hooks use addresses from this exe |
| xNVSE | NVR is an xNVSE plugin. xNVSE also switches the game's keyboard/mouse to shared mode, which the new menu relies on for normal mouse input |
| Everything NVR 4.4.1 already needs | Same as the official NVR 4.4.1 install guide (xNVSE, LOD Flicker Fix, etc.) |

Nothing else is needed for the new features.

## No longer needed for location presets

These were only needed because Cartographer is a script mod. The built-in version does the work inside the
DLL:

| Mod | Before | Now |
|---|---|---|
| Cartographer (`Cartographer.esp`) | The location preset framework | **Replaced. Disable it** (see Conflicts) |
| JIP LN NVSE | Cartographer's cell, image space, weather and keyword commands | Not needed for presets (keep it if other mods need it, most lists do) |
| JohnnyGuitar NVSE | Lighting template commands, editor-ID lookup | Not needed for presets |
| ShowOff NVSE | Cartographer's file/INI helpers | Not needed for presets |
| KEYWORDS framework | Assigned keywords to cells | Not needed for presets: the DLL reads `Data\KEYWORDS\*.ini` itself. Keep it if other mods use keywords |

## Works with (reads the same files)

- Any Cartographer preset mod: everything in `Data\Config\Cartographer\` (`Settings.ini`, `NVR.ini`,
  `Lighting.ini`, `Fog.ini`, `GameSettings.ini` and the `*Presets` folders), e.g. Mojave Nights NVR and Mojave
  Nights Performance Mode.
- Cell keywords from `Data\KEYWORDS\*.ini` (all three formats: `[0]`, `[1]`, `[Keyword]`).

## Conflicts

| Mod | Why | What happens |
|---|---|---|
| Cartographer.esp | Does the same job; both would fight | If the DLL sees `Cartographer.esp` loaded, the built-in Locations stays off and shows a warning in the log and menu. Disable the ESP to use the built-in version (preset mods keep working) |
| Atlas | Also swaps NVR settings per location | Use one or the other |
| Other NVR builds (official 4.4.1, Unofficial Optimized/Boutique) | Same DLL name | Replace them |

Interior lighting mods (ALT, SILO, GLO, Natural Interiors...): the built-in version only changes the lighting
of the cell you're in, and only when a lighting preset applies there. They coexist better than with
Cartographer, which rewired every interior at startup.

## Plugins checked for code conflicts (source review, 2026-11-21)

No byte-level overlap and no conflicting behaviour found with: xNVSE, JIP LN NVSE, JohnnyGuitar NVSE, ShowOff
NVSE, kNVSE, yUI (CrashLogger etc.), New Vegas Tick Fix, Heap Replacer, Better Transitions, Dynamic
Reflections, Real-Time Menus, HDR Patch, Shader Loader, High-Res Bloom / Water Fog, LOD Fixes, Moonlight NVSE,
Mobile PipBoy Light, Alpha Rendering Tweaks, DoF Fix, External Emittance Fix, Muzzle Flash Fix, Viewmodel
fixes, Audio Region Fix, OneTweak, Mod Limit Fix, NVUnloaded, Base Object Swapper NV, fnv-depth-resolve,
vanilla-plus particles, zlib update, dxvk. Details: the "Compatibility with other DLLs" section of
`GUI_AND_CARTOGRAPHER_PLAN.md`.

Known limitation: a mod's script hotkey can still fire while you type in the NVR menu (xNVSE gives scripts
their own copy of the keyboard).
