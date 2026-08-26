<p align="center">
  <img src="https://raw.githubusercontent.com/raziell74/water-lod-cold-load-fix/main/assets/Water%20LOD%20Fix%20Icon.png" alt="Water LOD Fix" width="256">
</p>

**Water LOD Cold Load Fix** is an SKSE plugin that hides leftover distant water LOD after a cold load. Load a save from the main menu while standing outdoors near a river, lake, or coast and you can see a second, low-detail water plane sitting on top of the real water. That's leftover **water LOD**: the distant water tiles the engine uses when those cells aren't loaded. On a cold boot they don't always get cleaned up when the full cells attach.

Hot-loading (loading a save while already in-game) usually doesn't show it, which is why "load a clean indoor save first" became a common workaround. This plugin waits until you're actually in the world, then walks the water LOD scene graph and hides leftover tiles that overlap the cells currently attached around you. Distant water beyond the loaded grid is left alone. Near water is left alone. No ESP, no MCM, no settings. Using this should significantly cut down on the amount of time you spend wondering if your water overhaul is broken and get back to what really matters... WADING INTO EVERY RIVER YOU SEE.

## Features

- Hides leftover water LOD tiles after a cold load from the main menu.
- Only touches water LOD that overlaps currently attached exterior cells.
- Distant water beyond the loaded grid is left alone.
- Near water is left alone. This plugin does not change how water looks.
- No ESP, no MCM, no settings. Drop it in and forget about it.
- One DLL covers SE 1.5.97, AE 1.6.1170, and AE 1.7.99 via Address Library.

## Requirements

- [SKSE](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Installation

Install using your favorite mod manager or manually extract the contents of the archive to your Skyrim Special Edition Data folder. Please also make sure you also have all of the required mods installed.

Load order doesn't matter. There is no plugin file.

## Uninstallation

Uninstall using your favorite mod manager or manually delete the files from your Skyrim Special Edition Data folder.

## Compatibility

**Water LOD Cold Load Fix** should be compatible with water visual overhauls, ENB, Community Shaders, and water mesh/texture mods. This plugin doesn't change how water looks, it only hides leftover LOD tiles the engine forgot to clean up.

Anything that doesn't yank the water LOD root out from under the engine should be fine. No patches should be needed.

The SKSE plugin was built using [CommonLibSSE NG](https://github.com/alandtse/CommonLibSSE-NG), it supports Skyrim SE, AE, GOG, and VR. This is aimed at SE/AE.

## Known Issues

The cleanup runs after the loading menu closes, once you're in an attached exterior cell. Interior-first sessions disable the plugin for the rest of that play session, same as loading a clean indoor save first.

Leftover tiles are hidden, not deleted, so the engine can still own them. If you still see stacked water after a cold load, give it a moment after the world appears before assuming something else is wrong.

## Credits

SKSE Source Code: [Water LOD Cold Load Fix](https://github.com/raziell74/water-lod-cold-load-fix) - Licensed under GPL-3.0-or-later, same as [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG).
