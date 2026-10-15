#!/usr/bin/env python3
"""End-to-end co-op battle test: two headless mGBAs fight one double battle together.

Usage: run_coop_e2e.py HARNESS ROM [--frames N]
Both games skip the intro, swap teams over the relay and start the co-op
battle, then press A until it ends. Passes when both games finish the same
battle with the same result, and every turn's battle-state checksum matched
the partner's.
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

COOP_FINISHED = 3


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("harness")
    parser.add_argument("rom")
    parser.add_argument("--frames", type=int, default=16000)
    args = parser.parse_args()

    port = free_port()
    relay = subprocess.Popen([sys.executable, os.path.join(LINK_DIR, "relay.py"), "--host", "127.0.0.1", "--port", str(port)])
    time.sleep(0.5)
    players = []
    try:
        with tempfile.TemporaryDirectory() as tmp:
            config = os.path.join(tmp, "config.lua")
            with open(config, "w") as f:
                f.write(f'RELAY_HOST = "127.0.0.1"\nRELAY_PORT = {port}\nROOM = "coop-e2e"\n')
            scripts = [os.path.join(LINK_DIR, "bridge.lua"), os.path.join(HERE, "coop_test.lua")]
            for _ in range(2):
                players.append(subprocess.Popen(
                    [args.harness, args.rom, str(args.frames), config, *scripts],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
            outputs = [p.communicate(timeout=args.frames / 10 + 120)[0] for p in players]
    finally:
        relay.terminate()
        relay.wait()

    finals = []
    for i, out in enumerate(outputs):
        reports = re.findall(r"COOP frame=\d+ test=\d+ state=(\d+) outcome=(\d+) turn=(\d+) hash=(\d+) matched=(\d+) desyncs=(\d+) battles=(\d+)", out)
        print(f"--- emulator {i + 1} ---")
        # The status repeats every 120 frames; print everything else and the last status.
        status_lines = [line for line in out.splitlines() if "COOP frame=" in line]
        for line in out.splitlines():
            if "COOP frame=" not in line:
                print(line)
        if status_lines:
            print(status_lines[-1])
        if not reports:
            print(f"FAIL: emulator {i + 1} never reported co-op status")
            return 1
        finals.append(tuple(int(x) for x in reports[-1]))

    (st1, out1, turn1, hash1, ok1, bad1, n1), (st2, out2, turn2, hash2, ok2, bad2, n2) = finals
    checks = [
        (n1 == 1 and n2 == 1, "both games started one co-op battle"),
        (st1 == COOP_FINISHED and st2 == COOP_FINISHED, "both games finished the battle"),
        (out1 == out2 and out1 != 0, f"both games got the same result (outcomes {out1} and {out2})"),
        (turn1 == turn2 and hash1 == hash2, f"both games ended on the same turn and state (turns {turn1} and {turn2})"),
        (bad1 == 0 and bad2 == 0, f"no turn's battle state differed between the games ({bad1} and {bad2} mismatches)"),
        (ok1 >= 3 and ok2 >= 3, f"turn checksums were compared ({ok1} and {ok2} matched)"),
    ]
    ok = True
    for passed, what in checks:
        print(("PASS: " if passed else "FAIL: ") + what)
        ok &= passed
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
