# Changelog

## Unreleased

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
