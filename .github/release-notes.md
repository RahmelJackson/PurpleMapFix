**Purple Map Fix** keeps the Skyrim world map readable on big mod lists like **LoreRim** and **Nolvus**. It's an SKSE plugin with no .esp, so it doesn't use a plugin slot and load order doesn't matter.

## New in 1.1.0

- **Fix Load Order:** the log now lists every plugin that resets the map, and the mod writes a load order report each time the game starts. The new optional **Fix Load Order** download makes the fix permanent: extract it anywhere, close MO2 and run **Fix Load Order.bat**. It finds your MO2 profile itself, moves those plugins up to just before the map plugin (with any master they need), backs up your plugin list first, and changes nothing else. **Preview Fix Load Order.bat** shows the change first. Vortex users get the "Load after" rules to add instead.
- Faster startup on big lists: only worldspaces that are actually wrong get looked up.
- A broken texture set shared by many meshes is patched once and reused.

## The problem

Opening the world map shows a blank purple, pink or blue sheet instead of the map. This mostly happens on lists that use flat or paper maps built on **Flat World Map Framework (FWMF)**.

FWMF works by moving the map camera high above a flat paper map. It stores that in each worldspace record. Any plugin that edits anything in a worldspace, even one placed object or NPC, carries a full copy of that record. If it loads after FWMF, it silently puts the camera back to vanilla: under the paper map. That's the purple sheet. The usual fix is "keep the map plugins last", and it breaks again every time you add a mod. Nolvus's own guide warns about exactly this.

## What this does

1. **Fixes the purple map, in any load order.** At startup it reads the worldspace records of every plugin. For each map setting it ignores plugins that just carry a stale vanilla copy, and keeps the value from the last plugin that changed it on purpose. Works for every worldspace, including Solstheim, DLC and new lands, and for any map mod.
2. **Patches missing map textures.** When the map opens it checks the textures the map is drawn from. Missing ones are patched with parchment (paper maps) or stone, or broken distant objects are hidden. Each broken file is logged so you can fix the real cause.

It never changes your load order or any file on disk. The log says exactly what it restored and which plugin had reset it:

```
Map data: Tamriel map bounds and camera (MNAM) restored from 'FWMF - Ultimate Patch.esp' (it was reset by 'ahzFleetKnight.esp', which loads later)
```

## Cost

Measured on LoreRim (3,646 plugins): about 130 ms once on the loading screen, about 4 ms the first time the map opens, about 0.2 ms each time after that, and nothing at all during normal play.

## Install

Requires SKSE64 and Address Library for SKSE Plugins. Skyrim SE 1.5.97 or AE 1.6.x (tested on 1.6.1170); not VR.

Download **PurpleMapFix-x.y.z.zip** below and install it with Mod Organizer 2 or Vortex, like any mod. That's all you need for a working map.

Optional: **PurpleMapFix-FixLoadOrder-x.y.z.zip** is the Fix Load Order tool. Don't install it as a mod: extract it anywhere and run it with MO2 and the game closed (see its README.txt).

The `-pdb` zip is debug symbols for crash logs; you don't need it to play.

Log: `Documents\My Games\Skyrim Special Edition\SKSE\PurpleMapFix.log`. Settings: `SKSE\Plugins\PurpleMapFix.ini`. Full details in the [README](https://github.com/RahmelJackson/PurpleMapFix#readme).
