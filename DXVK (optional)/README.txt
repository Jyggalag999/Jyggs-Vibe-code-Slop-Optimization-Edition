DXVK runs the game's Direct3D 9 on Vulkan. On my RX 7900 XTX it took the game from 52-56 FPS to about 130, which is more
than everything else in this build put together!

To use it, copy d3d9.dll and dxvk.conf next to FalloutNV.exe. You need a GPU and driver
with Vulkan 1.3. I've only tried it on my AMD card, so if the game won't start or something looks wrong, just delete
both files and you're back to the normal D3D9.

The d3d9.dll is the unmodified 32-bit DLL from the official DXVK 3.1.1 release, made by Philip Rebohle and the DXVK
contributors (https://github.com/doitsujin/dxvk). It's under the zlib/libpng license, see LICENSE-DXVK.txt. The
dxvk.conf is mine.
