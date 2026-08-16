#!/usr/bin/env python3
"""Decode a `vision snap` console dump into a PNG.

Usage: decode_console_snap.py <console-capture.txt> [out.png]

Finds the last SNAP-BEGIN <w> <h> <b64len> ... SNAP-END block in the capture,
base64-decodes the grayscale payload, and writes an 8-bit grayscale PNG
(pure stdlib -- no PIL needed). Scales 4x nearest-neighbor so the 160x90
frame is comfortably viewable.
"""
import base64
import re
import struct
import sys
import zlib


def write_png(path, w, h, gray, scale=4):
    sw, sh = w * scale, h * scale
    raw = bytearray()
    for y in range(sh):
        raw.append(0)  # filter: none
        row = gray[(y // scale) * w:(y // scale + 1) * w]
        for x in range(sw):
            raw.append(row[x // scale])

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", sw, sh, 8, 0, 0, 0, 0)  # 8-bit grayscale
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main():
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    text = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)  # strip ANSI

    blocks = re.findall(
        r"SNAP-BEGIN (\d+) (\d+) (\d+)\s*\n(.*?)SNAP-END", text, re.S)
    if not blocks:
        sys.exit("no SNAP-BEGIN/SNAP-END block found")
    w, h, b64len, body = blocks[-1]
    w, h = int(w), int(h)

    b64 = re.sub(r"[^A-Za-z0-9+/=]", "", body)  # join lines, drop prompts
    if len(b64) != int(b64len):
        print(f"note: got {len(b64)} b64 chars, device said {b64len}")
    gray = base64.b64decode(b64[:int(b64len)])
    if len(gray) < w * h:
        sys.exit(f"short payload: {len(gray)} < {w*h}")

    out = sys.argv[2] if len(sys.argv) > 2 else "snap.png"
    write_png(out, w, h, gray)
    print(f"wrote {out}: {w}x{h} (shown {w*4}x{h*4})")


if __name__ == "__main__":
    main()
