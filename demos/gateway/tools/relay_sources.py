#!/usr/bin/env python3
"""Relay real uart-telemetry-source boards into the gateway's ingest UART.

On the FRDM-IMX93 the A55's UART1 (gateway ingest link 0) is routed ONLY to
the CH342F debug bridge -- it does not appear on the P11 expansion header
(hardware-observed; P11's "GPIO_IO04" net is the GPIO2 pad driving the green
user LED, not the UART1_RXD pad). The bridge, however, exposes UART1 as the
board's second debug COM port, so the PC can stand in for the missing wire:
this script reads the LIVE telemetry lines from one or more real source
boards' console COM ports and forwards them into the gateway.

Bonus over a physical wire: multiple real boards can feed the single ingest
UART at once -- the gateway routes each line to its own child device by the
line's "board" field.

    python relay_sources.py COM20 COM42 COM44
                            ^gateway ^MCXE31B ^MCXW72 (any number of sources)
"""
import argparse
import sys
import threading

import serial

PREFIX = b"IOTC-TELEMETRY: "


def pump(src_port, gw, lock):
    with serial.Serial(src_port, 115200, timeout=1) as src:
        buf = b""
        while True:
            data = src.read(4096)
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip(b"\r")
                if line.startswith(PREFIX):
                    with lock:
                        gw.write(line + b"\n")
                    print(f"[{src_port}] {line[:90].decode(errors='replace')}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gateway_port", help="COM port of the gateway ingest UART "
                    "(FRDM-IMX93: the second debug COM port)")
    ap.add_argument("source_ports", nargs="+",
                    help="console COM port(s) of real source boards")
    args = ap.parse_args()

    gw = serial.Serial(args.gateway_port, 115200, timeout=1)
    lock = threading.Lock()
    threads = [
        threading.Thread(target=pump, args=(p, gw, lock), daemon=True)
        for p in args.source_ports
    ]
    for t in threads:
        t.start()
    print(f"relaying {', '.join(args.source_ports)} -> {args.gateway_port} "
          "(Ctrl-C to stop)")
    try:
        for t in threads:
            t.join()
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
