"""Headless test of the live-search plumbing (no X11): build_search_cmd / build_resume_cmd
/ SearchRun. Drives the real engine exactly as the panel's RunDialog does, but without Tk."""
import json
from pathlib import Path
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "panel"))
import app

exe = Path(sys.argv[1]).resolve()
ADDRESS = "1HsMJxNiV7TLxmoF6uJNkydxPFDog4NQum"   # puzzle 20


def drive(cmd, timeout=90):
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


def full_wheel(puzzle):
    low, high = app.first_bounds(puzzle)
    return app.record(puzzle, app.parse_list(app.format_bytes(range(low, high + 1))),
                      app.parse_list(app.format_bytes(range(256))))


def hit_and_miss():
    with tempfile.TemporaryDirectory() as td:
        found = Path(td) / "hit.txt"
        found.write_text("")
        cmd = app.build_search_cmd(exe, full_wheel(20), ADDRESS, 2, True, found)
        assert cmd[0] == str(exe) and "--search" in cmd and cmd[cmd.index("--output") + 1] == "kv"
        assert "--stop-on-hit" in cmd
        run = drive(cmd)
        assert run.result.get("matches") == "1", run.result
        assert run.result.get("found_private", "").endswith("d2c55"), run.result
        assert run.result.get("found_address") == ADDRESS and run.returncode == 0

        miss = app.record(20, app.parse_list("08"), app.parse_list("00,01"))
        cmd2 = app.build_search_cmd(exe, miss, ADDRESS, 1, False, found)
        run2 = drive(cmd2)
        assert run2.result.get("matches") == "0", run2.result
        assert int(run2.result["completed"]) == int(run2.result["total"]) == 4


def checkpoint_and_resume():
    # A wheel without the key, big enough to persist a checkpoint before we interrupt it.
    low, high = app.first_bounds(36)
    wheel = app.record(36, app.parse_list(app.format_bytes(range(low, high + 1))),
                       app.parse_list(app.format_bytes(range(40))))
    with tempfile.TemporaryDirectory() as td:
        ck = Path(td) / "state.json"
        found = Path(td) / "hit.txt"
        found.write_text("")
        cmd = app.build_search_cmd(exe, wheel, ADDRESS, 2, False, found, checkpoint=ck, interval=1)
        assert "--checkpoint" in cmd and "--checkpoint-interval" in cmd
        run = app.SearchRun(cmd)
        deadline = time.monotonic() + 60
        while True:                               # wait for a real periodic save, then interrupt
            run.drain()
            if run.done:
                raise AssertionError("finished before a checkpoint save: " + str(run.result))
            saved = app.checkpoint_progress(ck)
            if saved and saved[0] > 0:
                break
            if time.monotonic() > deadline:
                run.kill()
                raise AssertionError("no checkpoint was saved")
            time.sleep(0.03)
        run.stop()                                # SIGINT -> flush + exit 130
        while not run.done:
            run.drain()
            time.sleep(0.02)
        run.drain()
        assert run.returncode == 130, run.returncode
        done, total = app.checkpoint_progress(ck)
        assert 0 < done < total, (done, total)

        resume = app.build_resume_cmd(exe, ck, 4, False, Path(td) / "hit2.txt")
        assert resume[1] == "--resume"
        run2 = drive(resume)
        assert run2.result.get("matches") == "0", run2.result
        assert int(run2.result["completed"]) == total, run2.result
        assert int(run2.result["session_completed"]) == total - done, (run2.result, done)


def main():
    hit_and_miss()
    checkpoint_and_resume()
    print("panel_run OK")


if __name__ == "__main__":
    main()
