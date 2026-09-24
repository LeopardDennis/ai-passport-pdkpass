#!/usr/bin/env python3
"""Capture PDKPASS USB serial logs on macOS using only Python's standard library."""

import argparse
import errno
import glob
import os
import re
import select
import sys
import termios
import time
import tty
from collections import Counter
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path


EVENTS = {
    "boot": re.compile(r"PDKPASS starting"),
    "ready": re.compile(r"PDKPASS ready"),
    "ui_ready": re.compile(r"UI objects ready"),
    "wifi_ip": re.compile(r"got ip:|WIFI OK", re.IGNORECASE),
    "season_loaded": re.compile(r"Loaded \d+ season:"),
    "season_adopted": re.compile(r"Adopted \d+ season:"),
    "result_cached": re.compile(r"R\d+ .* podium cached"),
    "http_issue": re.compile(r"pdk_http.*GET stage="),
    "sound_issue": re.compile(r"Button sound .*failed|Button sound unavailable"),
    "panic": re.compile(r"Guru Meditation|assert failed|Brownout detector|panic'ed|abort\(\)"),
    "error": re.compile(r"(?:^|\s)E \(\d+\)"),
    "warning": re.compile(r"(?:^|\s)W \(\d+\)"),
}


def device_port(explicit):
    if explicit:
        return explicit
    matches = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(matches) == 1:
        return matches[0]
    available = ", ".join(sorted(glob.glob("/dev/cu.*")))
    raise RuntimeError(
        f"Expected one USB modem port, found {len(matches)}. "
        f"Available: {available or 'none'}. Pass --port explicitly if needed."
    )


@contextmanager
def open_serial(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        tty.setraw(fd, termios.TCSANOW)
        attributes = termios.tcgetattr(fd)
        attributes[2] |= termios.CLOCAL | termios.CREAD
        attributes[4] = termios.B115200
        attributes[5] = termios.B115200
        termios.tcsetattr(fd, termios.TCSANOW, attributes)
        yield fd
    finally:
        os.close(fd)


def capture_line(line, counts):
    clean = re.sub(r"\x1b\[[0-9;]*m", "", line.decode("utf-8", "replace"))
    for event, pattern in EVENTS.items():
        if pattern.search(clean):
            counts[event] += 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list-ports", action="store_true", help="List available macOS serial ports")
    parser.add_argument("--port", help="USB serial port; one /dev/cu.usbmodem* port is auto-detected")
    parser.add_argument("--seconds", type=int, default=180, help="Capture duration (default: 180)")
    parser.add_argument("--output", type=Path, help="Raw log path; defaults to a timestamped file")
    args = parser.parse_args()
    if args.list_ports:
        print("\n".join(sorted(glob.glob("/dev/cu.*"))) or "No serial ports found")
        return 0
    if args.seconds < 1:
        parser.error("--seconds must be positive")

    try:
        port = device_port(args.port)
    except RuntimeError as error:
        parser.error(str(error))
    output = args.output or Path("/tmp/pdkpass-device-logs") / (
        "pdkpass-serial-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".log"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    counts = Counter()
    size = 0
    pending = b""
    started = time.monotonic()
    disconnected = False
    try:
        with os.fdopen(fd, "wb") as log, open_serial(port) as serial_fd:
            while time.monotonic() - started < args.seconds:
                ready, _, _ = select.select([serial_fd], [], [], 0.5)
                if not ready:
                    continue
                try:
                    chunk = os.read(serial_fd, 4096)
                except OSError as error:
                    if error.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                        continue
                    raise
                if not chunk:
                    disconnected = True
                    break
                log.write(chunk)
                log.flush()
                size += len(chunk)
                pending += chunk
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    capture_line(line, counts)
            if pending:
                capture_line(pending, counts)
    except KeyboardInterrupt:
        if pending:
            capture_line(pending, counts)
    except (OSError, termios.error) as error:
        print(f"Capture failed: {error}", file=sys.stderr)
        return 1

    print(f"Port: {port}")
    print(f"Raw log: {output}")
    print(f"Captured: {size} bytes")
    for event in EVENTS:
        print(f"{event}: {counts[event]}")
    if disconnected:
        print("USB serial disconnected during capture.")
    print("Counts report observed events only; absence is not proof of failure.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
