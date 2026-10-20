"""Builds small synthetic plugin files for the MapData tests.

No game files are needed or included. Each fixture is a valid TES4-format plugin with
just enough records to exercise the reader and the resolver:

  Base.esm              defines Tamriel (with a >64 KB subrecord, so XXXX is exercised)
                        and a child world; a non-world group comes first
  MapMod.esp            a map mod: changes MNAM/ONAM of both worlds
  StaleCopy.esp         copies Tamriel from Base.esm (vanilla values), loads later
  StaleCopyZ.esp        same, but the record is zlib-compressed
  DeletedCopy.esp       a deleted Tamriel override (must be ignored)
  LaterMapMod.esp       a deliberate later MNAM change (must win over MapMod)
  NewLand.esp           defines its own worldspace
  NewLandMap.esp        a map mod for NewLand's worldspace
  LandPatch.esp         stale copy of NewLand's worldspace (masters: Base, NewLand)
  Broken.esp            truncated file (reading must fail cleanly)

Usage: python make_fixtures.py <output folder>
"""
import struct
import sys
import zlib
from pathlib import Path

TAMRIEL = 0x3C
CHILD = 0x1691D
NEWLAND_LOCAL = 0x801

V_MNAM = bytes.fromhex("0000000000000000e2ff0f002800d8ff0050434700409c4700004842")  # vanilla
V_ONAM = bytes.fromhex("0000803f000000000000000000000000")
F_MNAM = bytes.fromhex("0000000000000000ccff32003400ceff00d683480067d4480000b442")  # map mod
F_ONAM = bytes.fromhex("0000000000000000000000003ec23348")
G_MNAM = bytes.fromhex("0000000000000000ccff32003400ceff00007a480000fa480000b442")  # later, deliberate
N_MNAM = bytes.fromhex("00000000000000000a00f6ff1400ecff00504347004058470000a042")  # new land


def sub(sig, data):
    if len(data) > 0xFFFF:  # big subrecord: XXXX carries the real size
        return b"XXXX" + struct.pack("<HI", 4, len(data)) + sig.encode() + struct.pack("<H", 0) + data
    return sig.encode() + struct.pack("<H", len(data)) + data


def record(sig, form_id, body, flags=0, compress=False):
    if compress:
        body = struct.pack("<I", len(body)) + zlib.compress(body)
        flags |= 0x40000
    return sig.encode() + struct.pack("<IIIIHH", len(body), flags, form_id, 0, 44, 0) + body


def group(label, gtype, content):
    return b"GRUP" + struct.pack("<I4siHHHH", 24 + len(content), label, gtype, 0, 0, 0, 0) + content


def world(form_id, edid, mnam=None, onam=None, extra=b"", flags=0, compress=False):
    body = sub("EDID", edid.encode() + b"\0")
    if mnam is not None:
        body += sub("MNAM", mnam)
    if onam is not None:
        body += sub("ONAM", onam)
    body += extra
    rec = record("WRLD", form_id, body, flags, compress)
    # every world is followed by its children group (cells); the reader must skip it
    children = group(struct.pack("<I", form_id), 1, b"\xAB" * 40)
    return rec + children


def plugin(path, masters, worlds, esm=False):
    hdr = sub("HEDR", struct.pack("<fII", 1.71, 1, 0x800))
    for m in masters:
        hdr += sub("MAST", m.encode() + b"\0") + sub("DATA", b"\0" * 8)
    tes4 = record("TES4", 0, hdr, flags=1 if esm else 0)
    glob = group(b"GLOB", 0, record("GLOB", 0x800, sub("EDID", b"Test\0")))
    path.write_bytes(tes4 + glob + group(b"WRLD", 0, b"".join(worlds)))


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "fixtures")
    out.mkdir(parents=True, exist_ok=True)

    big = sub("OFST", b"\x01" * 70000)
    plugin(out / "Base.esm", [], [
        world(TAMRIEL, "Tamriel", V_MNAM, V_ONAM, extra=big),
        world(CHILD, "ChildWorld", None, V_ONAM),
    ], esm=True)
    plugin(out / "MapMod.esp", ["Base.esm"], [
        world(TAMRIEL, "Tamriel", F_MNAM, F_ONAM),
        world(CHILD, "ChildWorld", None, F_ONAM),
    ])
    plugin(out / "StaleCopy.esp", ["Base.esm"], [world(TAMRIEL, "Tamriel", V_MNAM, V_ONAM)])
    plugin(out / "StaleCopyZ.esp", ["Base.esm"], [world(TAMRIEL, "Tamriel", V_MNAM, V_ONAM, compress=True)])
    plugin(out / "DeletedCopy.esp", ["Base.esm"], [world(TAMRIEL, "Tamriel", V_MNAM, V_ONAM, flags=0x20)])
    plugin(out / "LaterMapMod.esp", ["Base.esm", "MapMod.esp"], [world(TAMRIEL, "Tamriel", G_MNAM, F_ONAM)])
    # NewLand's own world: FormID index 1 = past its one master, i.e. the plugin itself
    plugin(out / "NewLand.esp", ["Base.esm"], [world(0x01000000 | NEWLAND_LOCAL, "NewLandWorld", N_MNAM, V_ONAM)])
    # a paper map for the new land, then a stale copy of the new land's own values
    plugin(out / "NewLandMap.esp", ["Base.esm", "NewLand.esp"], [world(0x01000000 | NEWLAND_LOCAL, "NewLandWorld", F_MNAM, F_ONAM)])
    plugin(out / "LandPatch.esp", ["Base.esm", "NewLand.esp"], [world(0x01000000 | NEWLAND_LOCAL, "NewLandWorld", N_MNAM, V_ONAM)])
    data = (out / "MapMod.esp").read_bytes()
    (out / "Broken.esp").write_bytes(data[: len(data) - 30])
    print("fixtures written to", out)


if __name__ == "__main__":
    main()
