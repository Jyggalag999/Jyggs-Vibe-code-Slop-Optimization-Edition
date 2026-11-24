# What got faster, and by how much

Every optimization in this build, with the numbers I actually measured and where. If there's no number, it says
"not measured". I'd rather leave a gap than guess!

My test setup unless noted: RX 7900 XTX, A Blue Moon 2.0 modlist, November 2026.

**Your mileage may vary.** Most of this was tested quickly in one or two spots, not across many cells, weathers and
settings. Your gain depends on the cell, your GPU and CPU and your settings, and several features showed no measurable
gain where they were tested.

## From the Boutique fork

These came with the base and are all on by default (switches in `[_Main.Main.Performance]`). CountessAshe and Winston
measured the total on a GTX 1070 at 2560x1440, native D3D9, 4.4.1 default settings:

| Scene | 4.4.1 | Boutique |
|---|---|---|
| Lamp-lit casino interior | 24.7 ms (40.5 FPS) | 15.8 ms (63.1 FPS) |
| Desert at noon | 27.8 ms (36.0 FPS) | 19.7 ms (50.7 FPS) |

Nobody split that up per item, so only the totals are known. The short version of what's in there: effects chain into
each other without copying the whole frame after each one, the depth buffer is slimmer, AO and god rays get their own
small buffers, the sun-shadow and AO blends ride along in the fog pass, and lamp shadows skip cube map sides nobody can
see. Settings reads are cached too, which takes a full refresh at a cell change from about 1.4 ms to 0.05 ms. The details
are in [UNOFFICIAL-4.4.1-BOUTIQUE.md](UNOFFICIAL-4.4.1-BOUTIQUE.md).

## Added in this build

| Change | Before | After | Measured on |
|---|---|---|---|
| Script `SetSetting` calls batched into one settings reload per frame (`BatchScriptSettings`) | 95 reloads per door, 1060-1100 ms, worst frame 1101-1115 ms | 1 reload, 11-13 ms, worst frame 45-52 ms | DXVK, Mojave Outpost doors |
| Built-in Locations (Cartographer's job inside the DLL, applied during the loading screen) | worst frame 45-52 ms, 95 script calls | worst frame 34.0-34.7 ms, no script calls, nothing over 50 ms | same doors |
| Faster settings lookups when Locations applies a preset | apply 26.8 ms, worst frame at WastelandNV 222 ms | apply 7.7-12 ms, worst frame ~34 ms | same, `[Locations] phases` log |
| `PerformancePreset`, on by default (shorter, softer sun and lamp shadows) | NVR GPU ~6.2 ms per frame, 47-57 FPS | NVR GPU ~4.0 ms per frame, 52-59 FPS | native D3D9, WastelandNV, 6 benchmark windows |
| `PartialShadowAtlasUpdate`: only resolve and blur the shadow cascades that changed | blur 0.65 + resolve 0.55 ms every frame | not measured | - |
| `SharedShadowWalk`: one pass over the scene for all shadow cascades | ~0.44 ms CPU | not measured | - |
| `SpreadLod`: far shadow update spread over frames (part of the preset) | ~6 ms spike every 4th frame, up to 8-16 ms | not measured | - |
| Built-in occlusion culling (1.2, on by default): objects proven hidden aren't drawn | hiding off: 22.00 ms (45.5 FPS) | hiding on: 19.68 ms (50.8 FPS), -2.3 ms, +12% FPS, all 3 blocks faster | DXVK, WastelandNV (Mojave Outpost), blocks of 600 frames. Interiors and Freeside not measured |
| Terrain `SkipEmptyLayers` (1.2, on by default): land layers with zero weight skip their texture reads, same picture | 21.74 ms (46.0 FPS) | 21.57 ms (46.4 FPS), -0.18 ms, faster in 4 of 5 pairs | DXVK, WastelandNV, in-session A/B |
| Terrain `IsoHeightTaps` (1.2, on by default; slightly lossy) | 18.37 ms | 18.64 ms: no measurable gain | DXVK, WastelandNV, in-session A/B |
| `ParallaxLite` (1.2, on by default; lossy) | 19.45 ms (51.4 FPS) | 19.36 ms (51.7 FPS), about -0.1 ms, noise level | DXVK, WastelandNV, in-session A/B |
| `HiddenObjectCulling` (1.2, on by default): skips later lamp passes of hidden objects | same-spot pairs 19.52/19.12, 12.44, 16.73 ms | 19.29/19.25, 12.37/12.49, 16.87/16.85 ms: no measurable difference | DXVK, WastelandNV exteriors. Interiors not measured |

The preset's FPS range is wide because the scene kept moving around during the test. The NVR GPU number is the solid one.
It's also only my card, so numbers from other GPUs are very welcome!

There's tooling too, none of which changes FPS: a GPU profiler that writes `[GPU]` lines to the log, `[CellChange]` and
`[Locations]` timings, and `AutoBenchmark`, which flips the preset on and off on a timer and writes
`NewVegasReloaded.benchmark.csv`. All of it is off by default, with switches on the menu's Performance page.

## DXVK

Not an NVR change, but the biggest jump I've seen. DXVK 3.1.1 instead of AMD's native D3D9 driver went from 52-56 FPS to
about 130. World render GPU dropped from 11-13 ms to 4 ms and the game's own CPU work from 8-9 ms to 2.5 ms. NVR's passes
cost about 3-4 ms either way. That was two separate runs, not the exact same spot, so treat it as a ballpark.

## Still on the list

- Locations still spends 7.6-12 ms applying a preset during the loading screen. There's probably more to get there.
- A proper same-spot native vs DXVK comparison, and a closer look that every NVR effect renders right under DXVK.
