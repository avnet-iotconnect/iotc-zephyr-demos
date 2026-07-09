#!/usr/bin/env python3
"""Convert an eiq-pdm-vibration capture log into eIQ Time Series Studio files.

The capture firmware prints one line per sample:

    VIB,<t_ms>,<state>,<ax_g>,<ay_g>,<az_g>

eIQ Time Series Studio import wants SPACE-delimited files of numeric channels
only (no marker, timestamp, or label column), ONE FILE PER CLASS (you pick the
class + sample rate in the TSS import UI, channel count = 3). This splits the log
by the <state> label into <outdir>/<state>.csv, each space-delimited "ax ay az".

Usage:  python tss_convert.py <capture.log> [outdir]
"""
import os
import sys
import collections


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    src = sys.argv[1]
    outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(src)), "tss")
    os.makedirs(outdir, exist_ok=True)

    by_state = collections.defaultdict(list)
    with open(src) as f:
        for line in f:
            line = line.strip()
            if not line.startswith("VIB,"):
                continue                    # skip banners / "# state ->" comments
            p = line.split(",")
            if len(p) < 6:
                continue
            by_state[p[2]].append((p[3], p[4], p[5]))   # (ax, ay, az)

    if not by_state:
        print("No 'VIB,...' data lines found in", src)
        sys.exit(2)

    for state, rows in sorted(by_state.items()):
        path = os.path.join(outdir, f"{state}.csv")
        with open(path, "w", newline="") as o:
            for ax, ay, az in rows:
                o.write(f"{ax} {ay} {az}\n")
        distinct = len({r for r in rows})
        print(f"  {state:<11} {len(rows):>6} rows, {distinct:>6} distinct -> {path}")
    print("Done. In TSS: import each file, delimiter = Space, channels = 3, set "
          "the class per file (balanced=healthy, unbalanced=fault).")


if __name__ == "__main__":
    main()
