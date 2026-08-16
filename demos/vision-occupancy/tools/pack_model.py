#!/usr/bin/env python3
"""Pack a .tflite vision model into an IOTV blob (+ platform upload zip).

The IOTV envelope is the vision-scale sibling of the ml-model-update demo's
IOTM format: a 32-byte header (magic, format version, model version, display
name, payload length, CRC32) followed by the raw .tflite flatbuffer. The
firmware validates the envelope before trial-loading the model, and rejects
anything that fails CRC or does not parse as an int8 96x96x1 2-class model.

Outputs, next to the input (or under --out-dir):
  <name>_v<ver>.iotv   the enveloped model (also served raw by model-fetch)
  <name>_v<ver>.zip    STORED (uncompressed) zip of the .iotv, for the
                       platform's Devices -> AI Models upload

Example (stock person-detect model from the tflite-micro module):
  python pack_model.py \
      <workspace>/modules/lib/tflite-micro/tensorflow/lite/micro/models/person_detect.tflite \
      --version 2 --name person-v2
"""

import argparse
import pathlib
import struct
import sys
import zipfile
import zlib

IOTV_MAGIC = b"IOTV"
IOTV_FMT_VER = 1
IOTV_HDR_LEN = 32
NAME_LEN = 16
MODEL_MAX = 512 * 1024 - IOTV_HDR_LEN


def build_iotv(payload: bytes, model_ver: int, name: str) -> bytes:
    if len(payload) > MODEL_MAX:
        sys.exit(f"error: model is {len(payload)} B; firmware cap is {MODEL_MAX} B")
    if payload[4:8] != b"TFL3":
        sys.exit("error: input is not a .tflite flatbuffer (no TFL3 magic)")
    name_b = name.encode("ascii", errors="replace")[: NAME_LEN - 1]
    hdr = struct.pack(
        "<4sHHII16s",
        IOTV_MAGIC,
        IOTV_FMT_VER,
        model_ver,
        len(payload),
        zlib.crc32(payload) & 0xFFFFFFFF,
        name_b.ljust(NAME_LEN, b"\0"),
    )
    assert len(hdr) == IOTV_HDR_LEN
    return hdr + payload


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("tflite", type=pathlib.Path, help="input .tflite model")
    ap.add_argument("--version", type=int, required=True, metavar="N",
                    help="model version stamped into the header (1..65535)")
    ap.add_argument("--name", required=True,
                    help=f"display name (max {NAME_LEN - 1} ASCII chars)")
    ap.add_argument("--out-dir", type=pathlib.Path, default=None)
    args = ap.parse_args()

    if not 1 <= args.version <= 0xFFFF:
        sys.exit("error: --version must be 1..65535")

    payload = args.tflite.read_bytes()
    blob = build_iotv(payload, args.version, args.name)

    out_dir = args.out_dir or args.tflite.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = f"{args.name}_v{args.version}"

    iotv_path = out_dir / f"{stem}.iotv"
    iotv_path.write_bytes(blob)

    # The platform's AI Models upload takes a zip; the firmware only accepts
    # STORED entries (it walks the zip in place, no inflate on-device).
    zip_path = out_dir / f"{stem}.zip"
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_STORED) as zf:
        zf.writestr(f"{stem}.iotv", blob)

    crc = zlib.crc32(payload) & 0xFFFFFFFF
    print(f"packed  : {args.tflite} ({len(payload)} B)")
    print(f"header  : v{args.version} \"{args.name}\" crc={crc:08x}")
    print(f"wrote   : {iotv_path} ({len(blob)} B)")
    print(f"wrote   : {zip_path} (STORED zip for AI Models upload)")


if __name__ == "__main__":
    main()
