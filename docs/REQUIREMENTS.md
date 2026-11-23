# What you need, and what plays nice with it

Good news first: the new menu and the built-in Locations don't need anything that NVR 4.4.1 doesn't already need.

## Required

- **Fallout: New Vegas 1.4.0.525**, the normal Steam or GOG exe (4GB-patched like always). Every engine hook uses
  addresses from that exe.
- **xNVSE.** NVR is an xNVSE plugin anyway, and xNVSE also puts the keyboard and mouse in shared mode, which is what lets
  the menu use your mouse normally.
- **Everything NVR 4.4.1 already asks for**, so just follow the official 4.4.1 install guide (LOD Flicker Fix and friends).

## Cartographer users, read this

The built-in Locations does Cartographer's job inside the DLL, so **disable `Cartographer.esp`**. Seriously, if the DLL
sees it loaded, it keeps its own Locations off and tells you so in the log and the menu, so the two don't fight.

Your preset mods keep working though! The DLL reads everything in `Data\Config\Cartographer\` (`Settings.ini`, `NVR.ini`,
`Lighting.ini`, `Fog.ini`, `GameSettings.ini` and the `*Presets` folders), so Mojave Nights NVR, Mojave Nights Performance
Mode and the like are fine. Cell keywords come straight from `Data\KEYWORDS\*.ini`, in all three formats (`[0]`, `[1]` and
`[Keyword]`).

Cartographer itself needed JIP LN NVSE, JohnnyGuitar NVSE, ShowOff NVSE and the KEYWORDS framework. Location presets
don't anymore. Keep them installed if anything else uses them, though, and in most modlists something does.

**Got interior lighting mods (ALT, SILO, GLO, Natural Interiors...)?** They get along better than they did with
Cartographer. The built-in version only touches the lighting of the cell you're standing in, and only when a lighting
preset applies there, instead of rewiring every interior at startup.

## Don't mix with

- **Atlas**, which also swaps NVR settings per location. Pick one.
- **Other NVR builds** (official 4.4.1, Unofficial Optimized, Boutique). Same DLL name, so this one replaces them.

## Other DLLs I checked

I went through the source of these looking for hooks or patches that overlap with this build's (November 2026). Nothing
clashed: xNVSE, JIP LN NVSE, JohnnyGuitar NVSE, ShowOff NVSE, kNVSE, yUI (CrashLogger etc.), New Vegas Tick Fix, Heap
Replacer, Better Transitions, Dynamic Reflections, Real-Time Menus, HDR Patch, Shader Loader, High-Res Bloom / Water Fog,
LOD Fixes, Moonlight NVSE, Mobile PipBoy Light, Alpha Rendering Tweaks, DoF Fix, External Emittance Fix, Muzzle Flash
Fix, the viewmodel fixes, Audio Region Fix, OneTweak, Mod Limit Fix, NVUnloaded, Base Object Swapper NV,
fnv-depth-resolve, vanilla-plus particles, zlib update and DXVK.

That's a code read, not a play test of every one. If something breaks with one of them, please open an issue!

**One known quirk:** a mod's script hotkey can still fire while you're typing in the NVR menu, because xNVSE hands scripts
their own copy of the keyboard.
