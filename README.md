# Purple Map Fix

An SKSE plugin for Skyrim Special Edition / Anniversary Edition that keeps the world map
readable on big mod lists: vanilla 3D maps, paper maps, and flat maps built on
[Flat World Map Framework (FWMF)](https://www.nexusmods.com/skyrimspecialedition/mods/29932).

It fixes the two things that turn the map purple, **without changing your load order or
any files on disk**:

1. **The blank purple/blue map** on flat and paper maps, caused by a later plugin
   resetting the map camera. This is the common one on lists like LoreRim and Nolvus.
2. **Purple patches** where the map's textures are missing or broken.

Tested on LoreRim (3,600+ plugins, game version 1.6.1170), where it restored the Tamriel
and Solstheim maps that later plugins had reset.

## Install

1. Requirements:
   - Skyrim SE 1.5.97 or AE 1.6.x (not VR)
   - [SKSE64](https://skse.silverlock.org/)
   - [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
2. Download `PurpleMapFix-x.y.z.zip` from the [Releases](../../releases) page.
3. Install it with Mod Organizer 2 or Vortex like any other mod.

There's no .esp, so load order doesn't matter and it doesn't use a plugin slot.

Optional: `PurpleMapFix-FixLoadOrder-x.y.z.zip` on the same page is a separate tool that
makes the fix permanent in your load order. It isn't a mod: extract it anywhere and run it
(see [Making it permanent](#making-it-permanent-fix-load-order)).

## Fix 1: map settings reset by load order

Flat map mods like FWMF change two map settings on each worldspace record: the map camera
and bounds (`MNAM`: height, angle, visible area) and the map offset and scale (`ONAM`).
That puts the camera high above a flat paper map.

Any plugin that edits anything inside a worldspace (one placed object, an NPC, a cell
tweak) carries a full copy of that worldspace record, taken from its own masters. If it
loads after the map mod, that stale copy wins and resets the map camera to vanilla. The
camera ends up under the flat map and you see a blank purple or pink sheet. The usual
advice is "keep the map plugins last", which breaks again every time a mod is added.

At startup, Purple Map Fix reads the worldspace records of every loaded plugin straight
from the plugin files. For each worldspace and each map setting:

- the value in the plugin that defines the worldspace (e.g. `Skyrim.esm`) is the base;
- a later plugin with the same value as the base is just a copy, and is ignored;
- the **last plugin that actually changed the value** wins.

Those values are put back in memory. It works for every worldspace (Tamriel, DLC, and new
lands such as Bruma, Wyrmstooth or Falskaar) and for any map mod, whatever the load order.
The log says what was restored and which plugin had reset it:

```
Map data: Tamriel map bounds and camera (MNAM) restored from 'FWMF - Ultimate Patch.esp' (it was reset by 'ahzFleetKnight.esp', which loads later)
```

### Making it permanent: Fix Load Order

Purple Map Fix fixes the map every session without touching your files. If you'd rather
have the load order itself be right, it tells you what's wrong and can fix it for you.

Each time the game starts, the log lists every plugin that resets the map settings, and the
mod writes `PurpleMapFix-LoadOrder.txt` next to its log with the change that fixes it: those
plugins move **up**, to just before the map plugin (for example `FWMF - Ultimate Patch.esp`),
keeping their order. A plugin can't load before its own masters, so if one of them needs a
master that loads after that point, the master moves up with it (the log and the script say
which). If a plugin needs the map plugin itself as a master, it can't move above it; the
report says so, and Purple Map Fix keeps fixing it in game.

To apply it:

1. Start the game once with Purple Map Fix installed (reaching the main menu is enough).
2. Close the game and Mod Organizer 2.
3. Download **PurpleMapFix-FixLoadOrder-x.y.z.zip** (a separate download, so the mod itself
   installs like any other) and extract it anywhere, your Downloads folder is fine.
4. Run **Preview Fix Load Order.bat** to see the change, then **Fix Load Order.bat** to
   make it.

The script finds your MO2 profile on its own, wherever it was run from: it looks above the
game folder the report names (Wabbajack lists like LoreRim and Nolvus keep the game inside
the MO2 folder), in MO2's global instances, and in every MO2 registered to handle Nexus
downloads (portable installs). It picks the profile that matches the load order the game
actually used, backs up `plugins.txt` and `loadorder.txt` (`.before-purplemapfix-<date>`), and moves
only those lines. It refuses to run while MO2 or the game is open, and if a plugin it would
move is locked in MO2. Moving a plugin up can change which plugin wins its other conflicts,
so check your game after; the backups undo it. Vortex users get the equivalent "Load after"
rule to add instead, since Vortex sorts plugins itself. If you sort with LOOT later it may
undo the move; Purple Map Fix still fixes the map in game either way.

## Fix 2: missing or broken map textures

Purple is also what Skyrim draws for a texture it couldn't load. When the world map opens,
the plugin checks the textures of the meshes the map is drawn from (distant terrain and
object LOD, and the big mesh a flat map draws on), in loose files and BSAs, the same way
the game looks them up. Anything missing is handled based on what kind of mesh it is:

| Mesh | Example | What happens |
|---|---|---|
| Terrain LOD | the ground and mountains of the map | patched with a map-style texture |
| Object LOD | distant buildings, trees, rocks | hidden (paper/flat maps) or patched |
| Flat map mesh | the big image a flat map draws on | patched with parchment |
| Nearby object | a purple barrel next to you | off by default; patched with stone if enabled |
| Small unlit effect | fire, glows | never touched |

The patch texture is parchment on paper and flat maps, so patched spots blend in, and gray
stone on the vanilla-style map. The style is detected from plugin names, or set it in the
ini. Every broken file is logged once with the mesh that uses it, so the real cause can be
fixed. Only diffuse (color) and normal textures are checked: those are the ones that turn
a mesh purple.

## Performance

Measured on LoreRim (3,646 plugins):

| What | When | Time |
|---|---|---|
| Map settings check (Fix 1) | once, on the loading screen before the main menu | 128 ms (8 threads) |
| Texture check when the map opens (Fix 2) | each time the map opens | 4.4 ms |
| Two follow-up checks | 1 s and 4 s after opening | 1.0 ms and 0.3 ms |
| During normal play | always | nothing |

- **During normal play** there are no hooks, no per-frame work and no timers. The only
  thing installed is a listener the game calls when a menu opens or closes.
- **At startup** only the worldspace section of each plugin file is read; everything else
  is skipped. Files are read on several threads.
- **When the map opens** only the LOD node the map is drawn from is checked (not nearby
  objects, NPCs or grass), only diffuse and normal textures, and only texture sets not
  seen before. After the two follow-ups nothing runs until the map is opened again.

Every step logs its time in milliseconds, so the cost on any setup is visible in the log.

## Compatibility

| Mod or kind of mod | Status |
|---|---|
| Flat World Map Framework, FWMF Ultimate Patch, paper maps for FWMF (Skyrim, Solstheim, Blackreach, Soul Cairn, Sovngarde, Coldharbour, Bruma, Wyrmstooth, Falskaar…) | Supported; this is what Fix 1 is for |
| 3D map retextures (A Quality World Map, Paper World Map) | Compatible |
| Map marker and map UI mods (Flat Map Markers, Atlas Map Markers, Map Marker Framework, Compass Navigation Overhaul, SkyUI) | Compatible; the interface is never touched |
| DynDOLOD, xLODGen, seasonal LOD | Compatible; only missing textures are acted on |
| ENB, Community Shaders | Compatible. A map that looks wrong with nothing missing or reset is a shader setting, which this can't fix |
| Skyrim VR | Not supported |

Tested in game on AE 1.6.1170 (LoreRim). SE 1.5.97 and the GOG version are supported by the
build but not yet tested; reports welcome.

## Settings

`SKSE\Plugins\PurpleMapFix.ini`. The ones most likely to matter:

| Setting | Default | What it does |
|---|---|---|
| `bRestoreMapData` | 1 | Fix 1. Set to 0 if you deliberately load a plugin to reset a map to vanilla |
| `bWriteLoadOrderReport` | 1 | Write `PurpleMapFix-LoadOrder.txt` for Fix Load Order |
| `sMapStyle` | auto | `paper` for paper/flat maps, `stone` for vanilla-style, `auto` to detect |
| `sObjectLODMode` | auto | `hide` or `patch` broken distant objects |
| `sCustomMapTexture` | empty | Use a texture from your own map mod for patched spots |
| `bReplaceBrokenTextures` | 1 | 0 = only log, change nothing |
| `iFollowUpScans` | 2 | Extra checks shortly after the map opens (0-5) |
| `bScanWholeWorld` | 0 | 1 = check every mesh in the scene, for troubleshooting only (slow) |
| `bCheckDDSHeaders` | 0 | Also flag files that exist but aren't DDS (slower: may unpack textures from BSAs) |

## The log

```
Documents\My Games\Skyrim Special Edition\SKSE\PurpleMapFix.log
```

(`Skyrim Special Edition GOG` for the GOG version.) What to do with broken textures it lists:

- **terrain LOD, under `textures\terrain\`:** regenerate terrain LOD (xLODGen), or reinstall
  your terrain LOD or paper map mod.
- **object LOD, under `textures\lod\` or `textures\dyndolod\`:** regenerate with TexGen and
  DynDOLOD, using the SE versions of the DynDOLOD resources.
- **flat map:** the flat map's image is missing; reinstall that map mod and check its installer options.
- **anything else:** search the file name in MO2's Data tab to find which mod should provide it.

If the map is still wrong and the log shows nothing restored and nothing broken, the cause
is something else (usually a shader or lighting setting). Please open an issue with your log.

## Limits

- If a flat map's whole image file is missing, it becomes plain parchment: no purple,
  markers still work, but there's no map art. The log names the file to reinstall.
- A plugin that deliberately sets a map back to vanilla is treated as a stale copy. Use
  `bRestoreMapData = 0` if that's what you want.
- Flat-map meshes are recognized by size (`fFlatMapMinRadius`).

## Building from source

Windows, with Visual Studio 2022 (or its Build Tools) with the **Desktop development with
C++** workload, [Git](https://git-scm.com/download/win) and [xmake](https://xmake.io):

```
build.bat
```

It fetches [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) (pinned to a
tested commit) into `lib\`, builds, and writes `PurpleMapFix-mod.zip` and
`PurpleMapFix-FixLoadOrder.zip`.
`install-tools-and-build.bat` installs any missing tools with winget first. Output is also
saved to `build-log.txt`.

### Tests

The plugin-file reader and the resolver (`src/MapData.*`) don't depend on the game, so they
have tests that run anywhere with a C++20 compiler, zlib and Python 3:

```
tests/run_tests.sh
```

They build small synthetic plugin files (vanilla base, a map mod, stale copies loaded later,
a compressed record, a deliberate later change) and check that the right values win, and
check the load order planner (dependent plugins, master files, unfixable cases). CI runs
them on every push, and builds the Windows DLL.

### Releases

Actions tab → **CI** → **Run workflow**, enter the version (e.g. `1.0.1`). It runs the tests,
builds the mod and creates a **draft** release with the mod zip, the Fix Load Order zip and the notes from
`.github/release-notes.md`. Check it and click **Publish**; publishing creates the version tag.

## Project layout

| Path | What |
|---|---|
| `src/main.cpp` | SKSE plugin: map data restore, texture checks, settings, logging |
| `src/MapData.h/.cpp` | Reads worldspace records from plugin files and resolves them (no game code) |
| `src/LoadOrder.h/.cpp` | Plans the load order change for Fix Load Order (no game code) |
| `tools/fix-load-order/` | Fix Load Order scripts (the separate FixLoadOrder download) |
| `dist/` | The mod's non-code files: ini and fallback textures |
| `tests/` | Synthetic plugin fixtures and resolver tests |
| `tools/make_fallback_textures.py` | Regenerates the fallback textures |

## Credits

- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) (MIT): game reverse-engineering library
- [spdlog](https://github.com/gabime/spdlog) (MIT) and [zlib](https://zlib.net) (zlib license)
- [Flat World Map Framework](https://www.nexusmods.com/skyrimspecialedition/mods/29932) by Caites, whose documented
  load-order requirement this works around. Purple Map Fix contains no FWMF files.

## License

[MIT](LICENSE)
