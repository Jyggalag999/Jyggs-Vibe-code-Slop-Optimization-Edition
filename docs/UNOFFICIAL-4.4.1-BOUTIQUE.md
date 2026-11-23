# NVR 4.4.1 UNOFFICIAL Boutique - technical notes

Base: the official New Vegas Reloaded 4.4.1 source (pr0bability/TESReloaded10 `86ce118a`, checked against the 4.4.1
release archive: same version, same shaders, same settings file). The optimizations come from NVR UNOFFICIAL Optimized
(adeesababa/NVR-Unofficial-Optimized); only the ones that keep the 4.4.1 picture were ported, and several were adapted
where 4.4.1 differs from the code they were written for.

Rules for this build: same picture as stock 4.4.1, full resolution, no new shader features, no frame-skipping.
"Same up to rounding" below means: the stock maths in the stock order, without the 16-bit float storage of the image
between two passes - far below what an 8-bit screen shows.

## Settings

All in `[_Main.Main.Performance]` of `NewVegasReloaded.dll.defaults.toml`, shown by 4.4.1's menu under
Main > Main > Performance, on by default, `false` = the original 4.4.1 path. All switch live except SlimDepthBuffer.

| Setting | What it does | Same picture? |
|---|---|---|
| FrameChain | Effects alternate between two buffers (the game's HDR target is one of them) instead of copying the whole frame after every pass. Passes that clip, discard, blend or use stencil get their destination pre-filled first. The last post-tonemap effect writes the back buffer directly. | Yes (same formats, passes and order) |
| ChainUsesGameTexture | The game's own HDR image is one of the two chain buffers (saves a copy at the start). | Yes |
| SlimDepthBuffer | The effects' depth buffer keeps one 32-bit value per pixel; the second value (post-projection depth) is recomputed where it is read. Restart. | Up to float rounding |
| DedicatedAO | Ambient occlusion at full resolution as in 4.4.1, but its term lives in two small G16R16F targets, the x and y SSAO kernels run in one pass (they share origin, normal and kernel basis), the blurs ping-pong, and only the combine touches the HDR frame. Pixels beyond the AO range pass through, which is what the stock in-place `clip()` left. | Up to rounding |
| CompositeFog | When no effect renders between them (snow accumulation, wet world, flashlight, specular, underwater - off by default, or only in rain / under water), the exterior sun-shadow blend and the AO blend are applied inside the fog pass: two full-resolution HDR passes fewer. The fog is 4.4.1's own; its maths in the combined pass is the stock text. If anything fails, the original three passes run. | Up to rounding |
| DedicatedGodRays | God rays run the same half-resolution passes as 4.4.1, but in their own half-resolution targets instead of a corner of full-size ones: no copies, no clears. The ray falloff is still computed per full-resolution pixel. The sky-mask pass returns black straight away for non-sky pixels (bit-identical). | Yes, except the texels at the right and bottom screen edge of the ray buffer, where the stock pass read outside its own corner |
| MergedPointShadows | The shadow term of all twelve lamps in one pass instead of two (when LightPoints > 6). | Up to rounding |
| CubeMapFaceCulling | A lamp's shadow cubemap face skips objects whose bounding sphere cannot touch it (skinned and SpeedTree objects are always drawn). | Yes (those objects were clipped) |
| SkipUnusedCubeMap | The twelfth lamp is lit without a shadow lookup in 4.4.1, so its cubemap is no longer drawn. | Yes |
| CubeMapCameraCulling | A lamp cubemap face is not drawn when no on-screen pixel can read it this frame: the face's 90-degree pyramid, cut at the lamp radius and widened 5% for cube filtering across face edges, lies wholly outside one side plane of the camera view. The lamp shadow pass is the only reader and only reads within the lamp radius along the camera's pixel rays. If the camera changes between the shadow maps and the lamp shadow pass, the switch turns itself off until restart and logs a warning. | Yes (skipped faces are never read) |

Always on (no switch):
- Effects that never read `TESR_SourceBuffer` no longer copy the frame into it (and there is no copy at the start).
- Interior lamp shadows: the shadow-term blurs run on two G16R16 targets and only the last pass touches the HDR
  frame (with FrameChain); blur taps that are all equal skip the depth reads.
- Lamp shadow pass: a lamp's cubemap is not read where the pixel is beyond the lamp's radius (the stock result is
  multiplied by exactly 0 there); empty lamp slots are skipped.
- Normals: the first pass reuses its nine depth reads for the positions (stock read five of them twice). The two
  stock normal blurs are kept.
- Exterior shadow blend: the underwater water-plane test only runs when the camera is under water.
- Cinema: no per-pixel depth read when no letterbox is active. ImageAdjust: skipped at its identity settings.
- Shadow bone matrices are uploaded in runs instead of one call per bone.
- Log: graphics adapter, depth-resolve path and D3D9 runtime; a reference leak in the depth resolve fixed.
- Settings: reads are cached (a full refresh at each cell change cost ~1.4 ms of CPU on the TOML path, now ~0.05 ms);
  reading a setting no longer writes it back into the user config (saving from the menu now only writes changed and
  missing keys).
- Lamp shadow cube maps: per-object checks that don't depend on the face run once per lamp instead of six times.
- Missing texture/constant errors are logged once instead of every draw.

## Locations (built-in Cartographer)

The DLL reads the same files Cartographer 1.2.0 reads and does its job without the ESP, JIP, JohnnyGuitar or ShowOff.
Existing preset mods (Mojave Nights etc.) work unchanged.

- Files: `Data\Config\Cartographer\Settings.ini` (switches `bNVR`, `bInteriorLighting`, `bFogControl`, `bGameSettings`,
  `[Keywords]` aliases), `NVR.ini`, `Lighting.ini`, `Fog.ini`, `GameSettings.ini`, the matching `*Presets\*.ini` folders and
  `Data\KEYWORDS\*.ini` (all three formats). Everything is read once at startup and cached.
- Preset choice is Cartographer's: default preset, then alias presets, then keyword presets (a later name in the alphabet
  is added later and wins), then `[Overrides]`; a repeated name moves to the end; only keys of the default preset count; the
  first non-empty value from the end wins. Interiors are chosen per cell, exteriors per worldspace.
- Applied **before the destination loads**: the 7 callers of `PlayerCharacter::PositionPlayer` (save loads, doors,
  `MoveTo`, fast travel, `coc`) are call-hooked, chained to whatever call is there. The per-frame cell-change check is
  kept as a fallback.
- NVR presets are written as one batch with one settings reload. Only values that differ are written.
- Interior lighting touches **only the cell the player is in**: its image space, lighting template and inherit flags are
  remembered, replaced with our own forms (created at startup, no ESP needed), and restored when you leave. The dynamic
  ambient cycle runs natively every frame. Cells in `[Exclude]` and "behave as exterior" cells are skipped.
- Fog distances are written to every loaded weather for exteriors. Game settings are written by name.
- Settings (`[_Main.Main.Locations]`): `Enabled`, `BlendSeconds` (blends numeric values when presets change without a
  load: menu edits and reload; loads always switch instantly), `DebugLog`.
- If `Cartographer.esp` is loaded, Locations stays off and logs one warning line.
- Compatibility: nothing patches bytes another DLL patches. The call sites and `PositionPlayer` are not hooked by any
  repo in the toolkit (checked against all 1,540 recorded hook sites). Better Transitions hooks earlier calls
  (`RequestPositionPlayer`, `HandlePositionPlayerRequest`), so both run.
- Not copied: keywords added by scripts at runtime (`AssignKeyword`); only the `KEYWORDS` ini files are read.

## Not ported (would change the 4.4.1 picture or resolution)

- Half-resolution ambient occlusion (4.4.1 renders AO at full resolution), half-resolution fog, half-resolution god-ray
  falloff, the removal of the normal blurs, the distant-only depth-of-field shortcut.
- Staggered sun-cascade refresh and lamp shadow refresh intervals (moving shadows would lag), partial shadow-atlas
  resolve/blur (the stock blur's edge taps cross cascades).
- Bug fixes that change the picture (contact-shadow lines, depth of field in third person), and everything that needs
  files 4.4.1 does not have.

## Measured

GTX 1070, 2560x1440, native Direct3D 9, 4.4.1 default settings; two sessions per build, alternating:

| Scene | 4.4.1 | 4.4.1 UNOFFICIAL Boutique |
|---|---|---|
| Lamp-lit casino interior | 24.7 ms (40.5 FPS) | 15.8 ms (63.1 FPS) |
| Desert at noon | 27.8 ms (36.0 FPS) | 19.7 ms (50.7 FPS) |

Same picture: inside one session the seven live switches were turned off and on, with screenshots a few frames apart
across each switch. The NVIDIA FLIP difference stayed at the frame-to-frame noise level, with no visible change (the
only differences are animated flames and one-level band edges in the sky gradient), and average brightness moved by
less than 0.01 of a colour level.
