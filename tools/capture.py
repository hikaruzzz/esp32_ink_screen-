#!/usr/bin/env python3
"""有限时长串口采集。

用法：
    python tools/capture.py --port COM4 --seconds 10 --out evidence/boot.log

结束条件：达到 --seconds 或匹配到所有 --wait 事件或 Ctrl+C。
无论何种方式结束都会关闭串口。

依赖 pyserial（ESP-IDF 自带 Python 环境已包含）。
"""

import argparse
import re
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture serial output for a bounded time")
    parser.add_argument("--port", required=True, help="serial port, e.g. COM4")
    parser.add_argument("--baud", type=int, default=115200, help="baud rate (ignored by USB-Serial-JTAG)")
    parser.add_argument("--seconds", type=float, default=10.0, help="max capture duration in seconds")
    parser.add_argument("--out", required=True, help="output log file path")
    parser.add_argument("--wait", action="append", default=[],
                        help="regex for an event; all must appear to stop early (repeatable)")
    parser.add_argument("--reset", action="store_true",
                        help="pulse RTS/DTR after opening the port to reset the board (native USB-Serial/JTAG)")
    args = parser.parse_args()

    patterns = [re.compile(p) for p in args.wait]
    seen = [False] * len(patterns)
    started = time.monotonic()
    deadline = started + args.seconds
    lines = []

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.5)
    except serial.SerialException as exc:
        print(f"ERROR: cannot open {args.port}: {exc}", file=sys.stderr)
        return 2

    try:
        if args.reset:
            # 与 esptool 相同的自动复位时序：RTS 拉 EN 低，DTR 保持 GPIO9 高
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
            # 复位后重新计时，避免把复位前的等待算进采集窗口
            started = time.monotonic()
            deadline = started + args.seconds

        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            lines.append(text)
            for i, pat in enumerate(patterns):
                if not seen[i] and pat.search(text):
                    seen[i] = True
            if patterns and all(seen):
                break
    except KeyboardInterrupt:
        print("\n[stopped by Ctrl+C]", file=sys.stderr)
    finally:
        ser.close()

    elapsed = time.monotonic() - started
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.writelines(lines)

    print(f"\n[capture] port={args.port} elapsed={elapsed:.1f}s lines={len(lines)} out={args.out}",
          file=sys.stderr)
    missing = [p for p, ok in zip(args.wait, seen) if not ok]
    if missing:
        print(f"[capture] missing events: {missing}", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
