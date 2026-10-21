Purple Map Fix - Fix Load Order
===============================

Purple Map Fix already fixes the purple world map in game, every session, without
changing anything. This optional tool makes the fix permanent in your load order, so
the game loads the right map settings by itself.

It moves the plugins that reset the map (the ones listed in PurpleMapFix.log) up to
just before the map plugin (for example "FWMF - Ultimate Patch.esp"), keeping their
order. A plugin can't load before its own masters, so a master they need moves up with
them. Nothing else moves. Your plugin list is backed up first.

HOW TO USE

1. Install Purple Map Fix (the main download) and start the game once.
   Reaching the main menu is enough: that writes the report this tool reads.
2. Close the game and your mod manager.
3. Extract this zip anywhere (your Downloads folder is fine).
4. Run "Preview Fix Load Order.bat" to see what would change. It changes nothing.
5. Run "Fix Load Order.bat" to make the change. It asks before changing anything.

Then start the game: PurpleMapFix.log should no longer list plugins resetting the map.

MOD ORGANIZER 2
It finds your profile by matching it to the load order the game last used, including
Wabbajack lists (LoreRim, Nolvus...), portable installs and normal installs. It edits
plugins.txt and loadorder.txt in that profile. It won't run while MO2 or the game is
open (MO2 would overwrite the change when it closes) or if a plugin to move is locked.

VORTEX
Vortex keeps its own plugin order, so the tool doesn't edit files for you. It prints
the "Load after" rules to add in Vortex instead, which is the lasting way to do it there.

UNDO
Each changed file gets a backup next to it ending in .before-purplemapfix-<date>.
Replace the file with its backup to undo.

NOTES
- Moving a plugin up can change which plugin wins its other conflicts. Check your game.
- If you sort with LOOT later, it may move them back. Purple Map Fix still fixes the
  map in game either way.
- If it can't find your plugin list, it asks for the path to plugins.txt
  (MO2: <MO2 folder>\profiles\<profile name>\plugins.txt).

Source and help: https://github.com/RahmelJackson/PurpleMapFix
