#!/usr/bin/env python3
"""有限时长串口采集。

用法：
    python tools/capture.py --port COM4 --seconds 10 --out evidence/boot.log

结束条件：达到 --seconds 或匹配到所有 --wait 事件或 Ctrl+C。
日志边读边写；USB CDC 短暂断连（复位/重枚举）时自动重连，直到截止时间。
无论何种方式结束都会关闭串口并保留已采集内容。

依赖 pyserial（ESP-IDF 自带 Python 环境已包含）。
"""

import argparse
import os
import re
import sys
import time

import serial


def open_port(port: str, baud: int) -> serial.Serial:
    return serial.Serial(port, baud, timeout=0.5)


def pulse_reset(ser: serial.Serial) -> None:
    """esptool 同款自动复位时序：RTS 拉 EN 低，DTR 保持 GPIO9 高。"""
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture serial output for a bounded time")
    parser.add_argument("--port", required=True, help="serial port, e.g. COM4")
    parser.add_argument("--baud", type=int, default=115200, help="baud rate (ignored by USB-Serial-JTAG)")
    parser.add_argument("--seconds", type=float, default=10.0, help="max capture duration in seconds")
    parser.add_argument("--out", required=True, help="output log file path")
    parser.add_argument("--wait", action="append", default=[],
                        help="regex for an event; all must appear to stop early (repeatable)")
    parser.add_argument("--reset", action="store_true",
                        help="pulse RTS/DTR after opening the port to reset the board (native USB-Serial-JTAG)")
    args = parser.parse_args()

    patterns = [re.compile(p) for p in args.wait]
    seen = [False] * len(patterns)
    line_count = 0
    serial_errors = 0

    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)

    started = time.monotonic()
    deadline = started + args.seconds
    ser = None

    with open(args.out, "w", encoding="utf-8") as fh:
        try:
            ser = open_port(args.port, args.baud)
        except serial.SerialException as exc:
            print(f"ERROR: cannot open {args.port}: {exc}", file=sys.stderr)
            fh.write(f"[capture] cannot open {args.port}: {exc}\n")
            return 2

        if args.reset:
            try:
                pulse_reset(ser)
            except serial.SerialException as exc:
                print(f"[capture] reset pulse failed: {exc}", file=sys.stderr)
            started = time.monotonic()
            deadline = started + args.seconds

        try:
            while time.monotonic() < deadline:
                if ser is None:
                    try:
                        ser = open_port(args.port, args.baud)
                        print(f"[capture] reconnected {args.port}", file=sys.stderr)
                    except serial.SerialException:
                        time.sleep(0.5)
                        continue

                try:
                    raw = ser.readline()
                except serial.SerialException as exc:
                    serial_errors += 1
                    print(f"[capture] serial error ({serial_errors}): {exc}; will retry",
                          file=sys.stderr)
                    try:
                        ser.close()
                    except Exception:
                        pass
                    ser = None
                    time.sleep(0.5)
                    continue

                if not raw:
                    continue
                text = raw.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                fh.write(text)
                fh.flush()
                line_count += 1
                for i, pat in enumerate(patterns):
                    if not seen[i] and pat.search(text):
                        seen[i] = True
                if patterns and all(seen):
                    break
        except KeyboardInterrupt:
            print("\n[stopped by Ctrl+C]", file=sys.stderr)
        finally:
            if ser is not None:
                try:
                    ser.close()
                except Exception:
                    pass

    elapsed = time.monotonic() - started
    print(f"\n[capture] port={args.port} elapsed={elapsed:.1f}s lines={line_count} "
          f"serial_errors={serial_errors} out={args.out}", file=sys.stderr)

    if line_count == 0 and serial_errors == 0:
        print("[capture] no data received", file=sys.stderr)
        return 3
    missing = [p for p, ok in zip(args.wait, seen) if not ok]
    if missing:
        print(f"[capture] missing events: {missing}", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
