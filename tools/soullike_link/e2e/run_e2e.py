#!/usr/bin/env python3
"""End-to-end link test: two headless mGBAs running the real ROM, joined through the relay.

Usage: run_e2e.py HARNESS ROM [--frames N]
Passes when both games report being connected to each other, with different
player numbers, the same Daily Run seed, and pings answered by the other ROM.
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LINK_DIR = os.path.dirname(HERE)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("harness")
    parser.add_argument("rom")
    parser.add_argument("--frames", type=int, default=1200)
    args = parser.parse_args()

    port = free_port()
    relay = subprocess.Popen([sys.executable, os.path.join(LINK_DIR, "relay.py"), "--host", "127.0.0.1", "--port", str(port)])
    time.sleep(0.5)
    players = []
    try:
        with tempfile.TemporaryDirectory() as tmp:
            config = os.path.join(tmp, "config.lua")
            with open(config, "w") as f:
                f.write(f'RELAY_HOST = "127.0.0.1"\nRELAY_PORT = {port}\nROOM = "e2e"\n')
            bridge = os.path.join(LINK_DIR, "bridge.lua")
            for i in range(2):
                # Player 1 loads the bridge twice, like loading the script again in
                # mGBA; the second copy must replace the first, not take a second slot.
                scripts = [bridge, bridge] if i == 0 else [bridge]
                players.append(subprocess.Popen(
                    [args.harness, args.rom, str(args.frames), config, *scripts, os.path.join(HERE, "report.lua")],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
            outputs = [p.communicate(timeout=args.frames / 10 + 60)[0] for p in players]
    finally:
        relay.terminate()
        relay.wait()

    finals = []
    for i, out in enumerate(outputs):
        print(f"--- emulator {i + 1} ---")
        print(out)
        reports = re.findall(r"E2E frame=\d+ status=(\d+) player=(\d+) date=(\d+) seed=(\d+) pongs=(\d+) rtt=(\d+)", out)
        if not reports:
            print(f"FAIL: emulator {i + 1} never found the game's mailbox")
            return 1
        reports = [tuple(int(x) for x in r) for r in reports]
        # Use the last report from while both were running; one emulator can finish a moment before the other.
        connected = [r for r in reports if r[0] == 2]
        finals.append(connected[-1] if connected else reports[-1])

    (s1, p1, d1, seed1, pongs1, _), (s2, p2, d2, seed2, pongs2, _) = finals
    checks = [
        (s1 == 2 and s2 == 2, "both games saw their partner as connected"),
        ({p1, p2} == {0, 1}, "players got different numbers"),
        (d1 == d2 and d1 > 20000000 and seed1 == seed2, "both games got the same Daily Run date and seed"),
        (pongs1 > 0 and pongs2 > 0, "each game's pings were answered by the other game"),
    ]
    ok = True
    for passed, what in checks:
        print(("PASS: " if passed else "FAIL: ") + what)
        ok &= passed
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
