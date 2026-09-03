#!/usr/bin/env python3
"""Turns the raw screens the tests write into the PNGs spec/screens/ keeps (D-101).

    ROTA_SCREENS=out/screens-raw ./build/tests
    python3 tools/screens.py out/screens-raw spec/screens

Every .raw in the source directory becomes <name>.png; a PNG with no .raw beside it
is deleted, so a screen that stops being drawn shows up as a deletion in the diff
rather than as a picture of something that no longer exists. CI runs both and fails
on any difference, exactly as it does for the kits' generated headers.
"""

import pathlib
import struct
import sys
import zlib

WIDTH = 320
HEIGHT = 240
CHANNELS = 3


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def png_of(pixels):
    """A truecolour PNG. Rows are filtered Up, which collapses the long runs of
    identical rows a device screen is mostly made of, then deflated at level 9."""
    if len(pixels) != WIDTH * HEIGHT * CHANNELS:
        raise SystemExit(f"expected {WIDTH * HEIGHT * CHANNELS} bytes, got {len(pixels)}")
    stride = WIDTH * CHANNELS
    raw = bytearray()
    previous = bytes(stride)
    for y in range(HEIGHT):
        row = pixels[y * stride:(y + 1) * stride]
        raw.append(2)  # filter: Up
        raw += bytes((row[i] - previous[i]) & 255 for i in range(stride))
        previous = row
    body = chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
    body += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    body += chunk(b"IEND", b"")
    return b"\x89PNG\r\n\x1a\n" + body


def pixels_of(path):
    """The pixels inside a PNG this script wrote, so a file is rewritten only when the
    picture changes. Comparing bytes would compare compressors: zlib's output is not
    promised to be identical across versions, and CI runs on a different machine."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    position, compressed = 8, b""
    while position < len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        kind = data[position + 4:position + 8]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", data[position + 8:position + 18])
            if (width, height, depth, colour) != (WIDTH, HEIGHT, 8, 2):
                return None
        elif kind == b"IDAT":
            compressed += data[position + 8:position + 8 + length]
        position += 12 + length
    raw = zlib.decompress(compressed)
    stride = WIDTH * CHANNELS
    out, previous, position = bytearray(), bytes(stride), 0
    for _ in range(HEIGHT):
        kind = raw[position]
        position += 1
        row = bytearray(raw[position:position + stride])
        position += stride
        if kind == 2:  # Up, the only filter this script writes
            for i in range(stride):
                row[i] = (row[i] + previous[i]) & 255
        elif kind != 0:
            return None
        out += row
        previous = bytes(row)
    return bytes(out)


def main(argv):
    if len(argv) != 3:
        raise SystemExit("usage: screens.py <raw directory> <png directory>")
    source = pathlib.Path(argv[1])
    target = pathlib.Path(argv[2])
    if not source.is_dir():
        raise SystemExit(f"{source}: no raw screens; run the tests with ROTA_SCREENS set")
    target.mkdir(parents=True, exist_ok=True)

    written = set()
    for raw in sorted(source.glob("*.raw")):
        png = target / (raw.stem + ".png")
        pixels = raw.read_bytes()
        if not png.exists() or pixels_of(png) != pixels:
            png.write_bytes(png_of(pixels))
        written.add(png.name)
    if not written:
        raise SystemExit(f"{source}: no .raw files, so nothing was drawn")
    for stale in sorted(target.glob("*.png")):
        if stale.name not in written:
            stale.unlink()
            print(f"removed {stale}, which nothing draws any more")
    print(f"{len(written)} screens in {target}")


if __name__ == "__main__":
    main(sys.argv)
