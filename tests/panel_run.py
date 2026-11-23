"""Headless test of the live-search plumbing (no X11): build_search_cmd + SearchRun.

Drives the real engine exactly as the panel's RunDialog does, but without any Tk,
and checks the parsed progress/result on both a wheel that contains the key and one
that does not.
"""
import os
from pathlib import Path
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "panel"))
import app

exe = Path(sys.argv[1]).resolve()
ADDRESS = "1HsMJxNiV7TLxmoF6uJNkydxPFDog4NQum"   # puzzle 20


def run_to_end(ticket, address, threads, stop_on_hit, timeout=90):
    handle = tempfile.NamedTemporaryFile(delete=False)
    handle.close()
    try:
        cmd = app.build_search_cmd(exe, ticket, address, threads, stop_on_hit, handle.name)
        assert cmd[0] == str(exe) and "--search" in cmd and "--output" in cmd
        assert cmd[cmd.index("--output") + 1] == "kv"
        assert ("--stop-on-hit" in cmd) == stop_on_hit
        run = app.SearchRun(cmd)
        deadline = time.monotonic() + timeout
        while not run.done:
            run.drain()
            if time.monotonic() > deadline:
                run.kill()
                raise AssertionError("search did not finish in time")
            time.sleep(0.02)
        run.drain()
        return run
    finally:
        os.unlink(handle.name)


def main():
    # 1) Full puzzle-20 wheel contains the key -> must confirm it.
    low, high = app.first_bounds(20)
    full = app.record(20, app.parse_list(app.format_bytes(range(low, high + 1))),
                      app.parse_list(app.format_bytes(range(256))))
    hit = run_to_end(full, ADDRESS, 2, True)
    assert hit.result.get("matches") == "1", hit.result
    assert hit.result.get("found_private", "").endswith("d2c55"), hit.result
    assert hit.result.get("found_address") == ADDRESS, hit.result
    assert hit.returncode == 0, hit.returncode

    # 2) A tiny wheel that cannot contain the key -> exhausts with no match.
    miss = app.record(20, app.parse_list("08"), app.parse_list("00,01"))
    done = run_to_end(miss, ADDRESS, 1, False)
    assert done.result.get("matches") == "0", done.result
    assert int(done.result["completed"]) == int(done.result["total"]) == 4, done.result

    print("panel_run OK")


if __name__ == "__main__":
    main()
