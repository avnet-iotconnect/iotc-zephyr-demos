#!/usr/bin/env python3
"""Build IOTM model blobs for the ml-model-update demo.

The firmware ships a fixed tiny-MLP inference engine (2 inputs -> ReLU hidden
-> linear scores -> argmax); a *model* is pure data: a small header + float32
weights. This tool constructs the two demo models deterministically (no
training framework needed), brute-force VERIFIES their class bands, and emits:

  ../src/model_builtin.h          v1 "ambient" baked into the firmware
  ../models/model_v2_comfort.b64  v2 "comfort" -- push it from IOTCONNECT
  ../models/model_v2_comfort.cmd  ready-to-paste C2D command line

Blob layout (little-endian, must match src/main.c):
  0   4  magic "IOTM"
  4   2  fmt_ver = 1
  6   2  model_ver
  8   1  n_in  (<= 4)
  9   1  n_hid (<= 16)
  10  1  n_out (<= 4)
  11  1  led_mask (bit i: LED on while class i is active)
  12 48  labels[4][12], NUL-padded
  60  4  crc32 (IEEE) over the weights section
  64  .. float32 weights: W1[n_hid][n_in], b1[n_hid], W2[n_out][n_hid], b2[n_out]

Features fed by the firmware: x0 = temp_c / 50.0, x1 = light_pct / 100.0.
"""

import base64
import binascii
import struct
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def pack_model(model_ver, labels, led_mask, w1, b1, w2, b2):
    n_hid = len(b1)
    n_in = len(w1[0])
    n_out = len(b2)
    assert n_in <= 4 and n_hid <= 16 and n_out <= 4
    assert len(labels) == n_out and all(len(s) < 12 for s in labels)

    weights = []
    for row in w1:
        weights += row
    weights += b1
    for row in w2:
        weights += row
    weights += b2
    wbytes = struct.pack("<%df" % len(weights), *weights)

    lab = b"".join(s.encode().ljust(12, b"\0") for s in labels).ljust(48, b"\0")
    hdr = struct.pack("<4sHHBBBB", b"IOTM", 1, model_ver, n_in, n_hid, n_out,
                      led_mask) + lab + struct.pack("<I", binascii.crc32(wbytes))
    return hdr + wbytes


def infer(blob, x):
    """Reference inference -- mirrors the C engine bit-for-bit in spirit."""
    n_in, n_hid, n_out = blob[8], blob[9], blob[10]
    off = 64
    n = n_hid * n_in + n_hid + n_out * n_hid + n_out
    w = struct.unpack_from("<%df" % n, blob, off)
    W1 = [w[i * n_in:(i + 1) * n_in] for i in range(n_hid)]
    b1 = w[n_hid * n_in:n_hid * n_in + n_hid]
    o2 = n_hid * n_in + n_hid
    W2 = [w[o2 + i * n_hid:o2 + (i + 1) * n_hid] for i in range(n_out)]
    b2 = w[o2 + n_out * n_hid:]
    h = [max(0.0, sum(W1[i][j] * x[j] for j in range(n_in)) + b1[i])
         for i in range(n_hid)]
    o = [sum(W2[i][j] * h[j] for j in range(n_hid)) + b2[i]
         for i in range(n_out)]
    return o.index(max(o))


def banded_model(model_ver, labels, led_mask, w_feat, lo, hi):
    """3-class banded classifier on the projection w_feat . x:
    class0 below `lo`, class1 in [lo,hi), class2 above `hi`.
    Hidden pair h1=relu(w.x-lo), h2=relu(w.x-hi); steep output weights make
    the argmax flip exactly at the thresholds. w_feat is a 2-vector, e.g.
    [1,0] = temperature only, [0,1] = light only, [0.5,0.5] = fused index."""
    w1 = [list(w_feat), list(w_feat)]
    b1 = [-lo, -hi]
    # K makes class1 overtake class0 within ~0.25e-3 of `lo`; H >> K makes
    # class2 overtake class1 within ~0.2e-3 of `hi` (else the crossover drifts
    # past the threshold by K*(hi-lo)/H).
    K, H = 1000.0, 1000000.0
    w2 = [
        [-K,   0.0],   # class0: wins below lo
        [+K,    -H],   # class1: rises after lo, collapses after hi
        [0.0,   +H],   # class2: wins after hi
    ]
    b2 = [0.5, 0.0, -0.5]
    return pack_model(model_ver, labels, led_mask, w1, b1, w2, b2)


def verify(blob, w_feat, lo, hi, name):
    """Sweep the 2-D feature grid; assert the class matches the band that
    the projection w_feat . x falls in (skipping a hair around thresholds)."""
    for i in range(0, 101):
        for j in range(0, 101):
            x = [i / 100.0, j / 100.0]
            p = w_feat[0] * x[0] + w_feat[1] * x[1]
            if abs(p - lo) < 5e-3 or abs(p - hi) < 5e-3:
                continue
            want = 0 if p < lo else (1 if p < hi else 2)
            got = infer(blob, x)
            assert got == want, f"{name}: x={x} want {want} got {got}"
    print(f"  {name}: verified {len(blob)} bytes "
          f"(b64 {len(base64.b64encode(blob))} chars)")


# The model catalog. Feature scaling (firmware): x0 = temp_c/50, x1 = light/100.
# Each entry pushes a NOTICEABLY different behavior onto the same firmware.

# v1 "ambient" (BUILTIN): light bands  dark <20% <= dim < 60% <= bright.
# LED on while "bright".
v1 = banded_model(1, ["dark", "dim", "bright"], 0b100, [0, 1], 0.20, 0.60)
verify(v1, [0, 1], 0.20, 0.60, "v1 ambient (light)")

# v2 "comfort": temperature bands  cool <22C <= comfy < 27C <= warm.
# LED on while "warm" -- the device stops caring about light entirely.
v2 = banded_model(2, ["cool", "comfy", "warm"], 0b100, [1, 0], 0.44, 0.54)
verify(v2, [1, 0], 0.44, 0.54, "v2 comfort (temp)")

# v3 "nightlight": same light bands as v1 but the LED POLICY IS INVERTED --
# LED on while "night" (class 0). Cover the sensor and the LED turns ON.
v3 = banded_model(3, ["night", "dusk", "day"], 0b001, [0, 1], 0.20, 0.60)
verify(v3, [0, 1], 0.20, 0.60, "v3 nightlight (light, LED-on-dark)")

# v4 "hot-alarm": tight temperature bands around room temp so a fingertip
# trips it in seconds -- normal <26C <= warm < 29C <= hot. LED on while warm
# OR hot (alarm-style mask covering two classes).
v4 = banded_model(4, ["normal", "warm", "hot"], 0b110, [1, 0], 0.52, 0.58)
verify(v4, [1, 0], 0.52, 0.58, "v4 hot-alarm (temp, 2-class LED)")

# v5 "fusion": classifies on the MEAN of both features -- the first model
# that uses temperature AND light together. gloomy < 0.32 <= normal < 0.55
# <= sunny (e.g. 25C + dark room = gloomy; warm + bright light = sunny).
# LED on while "sunny".
v5 = banded_model(5, ["gloomy", "normal", "sunny"], 0b100, [0.5, 0.5],
                  0.32, 0.55)
verify(v5, [0.5, 0.5], 0.32, 0.55, "v5 fusion (temp+light)")

# --- emit ------------------------------------------------------------------
src = HERE.parent / "src"
models = HERE.parent / "models"
src.mkdir(exist_ok=True)
models.mkdir(exist_ok=True)

lines = [f"0x{b:02x}," for b in v1]
rows = ["\t" + " ".join(lines[i:i + 12]) for i in range(0, len(lines), 12)]
(src / "model_builtin.h").write_text(
    "/* Generated by tools/build_model.py -- v1 \"ambient\" model blob.\n"
    " * Regenerate with:  python tools/build_model.py  */\n"
    "#ifndef MODEL_BUILTIN_H\n#define MODEL_BUILTIN_H\n\n"
    "#include <stdint.h>\n\n"
    "static const uint8_t model_builtin[] = {\n" + "\n".join(rows) +
    "\n};\n\n#endif /* MODEL_BUILTIN_H */\n")

# Per model: .zip = STORED (uncompressed) single-entry archive for the
# IOTCONNECT AI Models upload (the firmware unwraps stored zips); .bin = the
# raw blob; .b64 = paste as the model-push command parameter; .cmd = the full
# one-liner.
for blob, fname in ((v2, "model_v2_comfort"), (v3, "model_v3_nightlight"),
                    (v4, "model_v4_hotalarm"), (v5, "model_v5_fusion")):
    b64 = base64.b64encode(blob).decode()
    (models / f"{fname}.bin").write_bytes(blob)
    (models / f"{fname}.b64").write_text(b64 + "\n")
    (models / f"{fname}.cmd").write_text("model-push " + b64 + "\n")
    with zipfile.ZipFile(models / f"{fname}.zip", "w",
                         zipfile.ZIP_STORED) as z:
        # fixed date so regeneration is byte-identical
        z.writestr(zipfile.ZipInfo(f"{fname}.bin", (2026, 1, 1, 0, 0, 0)),
                   blob)
    zsize = (models / f"{fname}.zip").stat().st_size
    print(f"  wrote models/{fname}.zip/.bin/.b64/.cmd "
          f"({len(blob)} B blob, {zsize} B zip)")
print("  wrote src/model_builtin.h (v1)")
