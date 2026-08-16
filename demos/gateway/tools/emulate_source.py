#!/usr/bin/env python3
"""Emulate a uart-telemetry-source MCU into the gateway's ingest UART.

On the FRDM-IMX93 the debug USB's second CH342 channel is wired to UART1 --
the same UART the gateway uses as ingest link 0 -- so a PC can stand in for
the source MCU with no jumper wires (hardware-verified):

    python emulate_source.py COM20            # 5 s cadence, endless
    python emulate_source.py COM20 -n 10 -i 2 # 10 messages, 2 s apart

Lines use the exact uart-telemetry-source format; the gateway forwards each
as its link-0 child device (default id: frdmmcxe31b01).
"""
import argparse
import json
import random
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="COM port wired to the gateway ingest UART")
    ap.add_argument("-n", "--count", type=int, default=0,
                    help="messages to send (0 = endless)")
    ap.add_argument("-i", "--interval", type=float, default=5.0,
                    help="seconds between messages (default 5, like the real source)")
    ap.add_argument("--board", default="frdm_mcxe31b")
    args = ap.parse_args()

    t0 = time.time()
    seq = 0
    temp = 33.0
    with serial.Serial(args.port, 115200, timeout=0.5) as s:
        while args.count == 0 or seq < args.count:
            temp += random.uniform(-0.3, 0.35)
            record = {
                "sequence": seq,
                "uptime_s": round(time.time() - t0, 3),
                "cpu_temp_c": round(temp, 1),
                "board": args.board,
                "bearer": "uart-source",
                "status": "Ready",
                "safe_state": True,
            }
            line = "IOTC-TELEMETRY: " + json.dumps(
                {"d": [{"d": record}]}, separators=(",", ":")) + "\n"
            s.write(line.encode())
            print(line.strip()[:100])
            seq += 1
            time.sleep(args.interval)


if __name__ == "__main__":
    main()
