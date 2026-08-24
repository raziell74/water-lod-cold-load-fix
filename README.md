# Water LOD Cold Load Fix

An SKSE plugin that hides leftover distant water LOD after a cold load.

## The problem

Load a save from the main menu while standing outdoors near a river, lake, or coast. For a while — and sometimes until you leave the area — you can see a second, low-detail water plane sitting on top of the real water.

That's leftover **water LOD**: the distant water tiles the engine uses when those cells aren't loaded. On a cold boot they don't always get cleaned up when the full cells attach.

Hot-loading (loading a save while already in-game) usually doesn't show it. That's why "load a clean indoor save first" became a common workaround.

## What this does

Nothing fancy. No ESP, no MCM, no settings.

After you load a game, the plugin waits until you're actually in the world — main menu and loading screen closed, player 3D live, exterior cell. Then it walks the **water LOD** scene graph (not near water, not your character's 3D) and hides leftover LOD tiles that overlap the cells currently attached around you.

Distant water beyond the loaded grid is left alone. Near water is left alone.

## Requirements

- Skyrim Special Edition or Anniversary Edition
- [SKSE](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

One DLL covers SE 1.5.97, AE 1.6.1170, and AE 1.7.99 via Address Library.

## Installation

Drop `WaterLODCleanupFix.dll` into `Data/SKSE/Plugins/`, or install with Mod Organizer 2 / Vortex like any other SKSE plugin.

Load order doesn't matter. There is no plugin file.

## Compatibility

- Water visual overhauls (ENB, Community Shaders, water mesh/texture mods): compatible. This plugin doesn't change how water looks.
- Anything that doesn't yank the water LOD root out from under the engine should be fine.
- VR is enabled in the CommonLibSSE-NG build, but this is aimed at SE/AE.

## Known limitations

- Only runs after a load, with an ~8 second delay (gives up after ~25 seconds if the world never becomes ready).
- Interior loads are skipped.
- Leftover tiles are hidden (`SetAppCulled`), not deleted, so the engine can still own them.

## Building from source

CMake 3.21+, MSVC, Ninja, and vcpkg. CommonLibSSE-NG is pulled from an overlay port (`cmake/ports/commonlibsse-ng`).

```bat
cmake --preset release
cmake --build --preset release
```

The debug preset works the same way (`--preset debug`). `CMakeLists.txt` copies the built DLL into a local MO2 mods folder; change `OUTPUT_FOLDER` if that path isn't yours.

## License

GPL-3.0-or-later, same as [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG).
