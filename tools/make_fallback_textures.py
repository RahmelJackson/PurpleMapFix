"""Writes the two fallback textures shipped with Purple Map Fix.

fallback_d.dds  - muted stone-gray diffuse with light noise, so patched LOD reads
                  as terrain on the map instead of a flat block
fallback_paper_d.dds - parchment diffuse for paper and flat maps
fallback_n.dds  - flat tangent-space normal map; alpha 0 so it adds no shine

Both are uncompressed 32-bit BGRA DDS files with a full mip chain, which every
version of Skyrim reads. Run: python make_fallback_textures.py <output folder>
"""
import random
import struct
import sys
from pathlib import Path

SIZE = 128


def dds_header(size: int, mips: int) -> bytes:
    DDSD = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000  # caps|height|width|pitch|pixelformat|mipmapcount
    pixel_format = struct.pack(
        "<II4sIIIII",
        32,                 # struct size
        0x1 | 0x40,         # DDPF_ALPHAPIXELS | DDPF_RGB
        b"\0\0\0\0",        # no FourCC
        32,                 # bits per pixel
        0x00FF0000,         # R mask
        0x0000FF00,         # G mask
        0x000000FF,         # B mask
        0xFF000000,         # A mask
    )
    caps = 0x8 | 0x1000 | 0x400000  # complex | texture | mipmap
    header = struct.pack("<7I", 124, DDSD, size, size, size * 4, 0, mips)
    header += b"\0" * 44  # reserved
    header += pixel_format
    header += struct.pack("<5I", caps, 0, 0, 0, 0)
    assert len(header) == 124
    return b"DDS " + header


def build(pixel_fn, size: int) -> bytes:
    # Base level, then box-filtered mips down to 1x1.
    level = [[pixel_fn(x, y) for x in range(size)] for y in range(size)]
    levels = [level]
    while len(level) > 1:
        half = len(level) // 2
        nxt = []
        for y in range(half):
            row = []
            for x in range(half):
                px = [level[2 * y + dy][2 * x + dx] for dy in (0, 1) for dx in (0, 1)]
                row.append(tuple(sum(c[i] for c in px) // 4 for i in range(4)))
            nxt.append(row)
        level = nxt
        levels.append(level)

    data = bytearray(dds_header(size, len(levels)))
    for lvl in levels:
        for row in lvl:
            for r, g, b, a in row:
                data += bytes((b, g, r, a))  # BGRA in memory
    return bytes(data)


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    out.mkdir(parents=True, exist_ok=True)

    rng = random.Random(1234)
    noise = [[rng.randint(-10, 10) for _ in range(SIZE)] for _ in range(SIZE)]

    def diffuse(x, y):
        n = noise[y][x]
        return (118 + n, 114 + n, 106 + n, 255)

    def normal(x, y):
        return (128, 128, 255, 0)

    # Parchment: warm tan with soft, tileable blotches plus fine fiber grain, so a
    # patched block reads as part of a paper map rather than a hole in it.
    grid = 8
    blotch = [[rng.uniform(-1.0, 1.0) for _ in range(grid)] for _ in range(grid)]

    def smooth(x, y):
        gx, gy = x * grid / SIZE, y * grid / SIZE
        x0, y0 = int(gx), int(gy)
        fx, fy = gx - x0, gy - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        x1, y1 = (x0 + 1) % grid, (y0 + 1) % grid
        top = blotch[y0][x0] * (1 - fx) + blotch[y0][x1] * fx
        bottom = blotch[y1][x0] * (1 - fx) + blotch[y1][x1] * fx
        return top * (1 - fy) + bottom * fy

    grain = [[rng.randint(-6, 6) for _ in range(SIZE)] for _ in range(SIZE)]

    def paper(x, y):
        v = int(smooth(x, y) * 14) + grain[y][x]
        clamp = lambda c: max(0, min(255, c))
        return (clamp(198 + v), clamp(178 + v), clamp(138 + v), 255)

    (out / "fallback_paper_d.dds").write_bytes(build(paper, SIZE))
    (out / "fallback_d.dds").write_bytes(build(diffuse, SIZE))
    (out / "fallback_n.dds").write_bytes(build(normal, SIZE))
    print("wrote", out / "fallback_d.dds", "and", out / "fallback_n.dds")


if __name__ == "__main__":
    main()
