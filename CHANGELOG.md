# Changelog

## 1.1.0 (2026-10-21)

- The log now lists every plugin that resets the map settings, not just the last one.
- New load order report, `PurpleMapFix-LoadOrder.txt` (next to the log), written at each
  start: the plugins that reset the map and the load order change that fixes it for good.
- New `PurpleMapFix Tools\Fix Load Order.bat`: applies that change to your Mod Organizer 2
  profile (or plugins.txt), with a backup. It moves the map plugins to just after the last
  plugin that resets them, and nothing else. `Preview Fix Load Order.bat` shows the change
  without making it.
- `build.bat` packages the mod zip without debug symbols and with standard zip paths.

## 1.0.1 (2026-10-20)

- A broken texture set shared by many meshes is now patched once and the patched copy
  reused, instead of building a new one per mesh.

## 1.0.0 (2026-10-20)

First public release.

- Restores world map settings (map camera/bounds `MNAM`, map offset/scale `ONAM`) that a
  later plugin's stale worldspace copy reset, for every worldspace, in any load order.
  Fixes the blank purple/pink map on FWMF paper and flat maps.
- Checks the map's LOD textures when the map opens; patches missing ones with a parchment
  or stone texture, or hides broken distant objects; logs every broken file once.
- Plugin files are read in parallel at startup (128 ms for 3,646 plugins); the map check
  costs about 4 ms when the map opens; nothing runs during normal play.
- Settings in `PurpleMapFix.ini`; timings for every step in `PurpleMapFix.log`.
