#!/usr/bin/env python3
"""Optional Linux/Wayland first-click check against an independent application.

Requires python-evdev and write access to /dev/uinput. Opens disposable test
windows, moves the pointer, and clicks exactly two known test controls. It does
not open Moji Nook's user profile. Run from an idle desktop; exit 2 is inconclusive.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary", type=Path,
        default=Path(__file__).resolve().parents[1] / "build/moji_nook_desktop_check",
    )
    parser.add_argument("--press-ms", type=int, default=40)
    args = parser.parse_args()
    if not 1 <= args.press_ms <= 300:
        parser.error("--press-ms must be between 1 and 300")
    if not os.environ.get("WAYLAND_DISPLAY"):
        parser.error("run this optional check in a native Wayland desktop session")
    if not os.access("/dev/uinput", os.W_OK):
        parser.error("/dev/uinput is not writable for this user")
    try:
        from evdev import UInput, AbsInfo, ecodes as e
    except ImportError:
        parser.error("python-evdev is required for this optional desktop check")

    capabilities = {
        e.EV_KEY: [e.BTN_LEFT, e.BTN_RIGHT],
        e.EV_ABS: [
            (e.ABS_X, AbsInfo(0, 0, 65535, 0, 0, 0)),
            (e.ABS_Y, AbsInfo(0, 0, 65535, 0, 0, 0)),
        ],
    }
    with UInput(capabilities, name="Moji Nook isolated first-click check") as pointer:
        process = subprocess.Popen(
            [str(args.binary), "--external-buttons"],
            stdout=subprocess.PIPE, text=True, bufsize=1,
        )
        desktop = None
        clicks = 0
        pressed = set()
        activated = set()
        try:
            for line in process.stdout:
                print(line.rstrip(), flush=True)
                fields = line.split()
                if fields[:2] == ["Native", "pressed"]:
                    pressed.add(fields[2])
                elif fields[:2] == ["Native", "clicked"]:
                    activated.add(fields[2])
                elif fields[:1] == ["NATIVE_DESKTOP"]:
                    desktop = tuple(map(int, fields[1:]))
                elif fields[:1] == ["NATIVE_TARGET"]:
                    if not desktop or clicks >= 2 or fields[1] != (
                        "revealAnswer" if clicks == 0 else "markRemembered"
                    ):
                        raise RuntimeError("unexpected native test target")
                    x, y = map(int, fields[2:])
                    left, top, width, height = desktop
                    if not (left <= x < left + width and top <= y < top + height):
                        raise RuntimeError("native target lies outside test desktop")
                    # Qt reports logical desktop coordinates. Normalize across
                    # the entire logical workspace for libinput's absolute mouse.
                    pointer.write(e.EV_ABS, e.ABS_X,
                                  round((x - left) * 65535 / max(1, width - 1)))
                    pointer.write(e.EV_ABS, e.ABS_Y,
                                  round((y - top) * 65535 / max(1, height - 1)))
                    pointer.syn()
                    time.sleep(.4)
                    pointer.write(e.EV_KEY, e.BTN_LEFT, 1)
                    pointer.syn()
                    time.sleep(args.press_ms / 1000)
                    pointer.write(e.EV_KEY, e.BTN_LEFT, 0)
                    pointer.syn()
                    clicks += 1
            result = process.wait()
            expected = {"revealAnswer", "markRemembered"}
            confirmed = clicks == 2 and pressed == expected and activated == expected
            return result if result else (0 if confirmed else 2)
        finally:
            pointer.write(e.EV_KEY, e.BTN_LEFT, 0)
            pointer.syn()
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=3)


if __name__ == "__main__":
    sys.exit(main())
