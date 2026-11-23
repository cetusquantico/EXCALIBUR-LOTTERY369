"""End-to-end CLI tests using published or deterministic known-key fixtures."""
import csv
import hashlib
import itertools
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

EXE = str(Path(sys.argv[1]).resolve())
FIXTURES = Path(__file__).resolve().parents[1] / "fixtures"
H39 = "b774167d31d2a2f0886ecfb67719968f64e17e95"
H160 = "75a598aae086b74feadf44c306e3a66526d5659c"
P160 = ["--puzzle", "160", "--first-allow", "80", "--allow", "00,01",
        "--fixture", "synthetic-160", "--hash160", H160]


def cli_output(*args, ok=True, env=None):
    p = subprocess.run([EXE, *map(str, args)], capture_output=True, text=True, timeout=300, env=env)
    if p.returncode != (0 if ok else 2):
        raise AssertionError((args, p.returncode, p.stderr, p.stdout))
    return p.stdout


def run(*args, ok=True, env=None):
    stdout = cli_output("--output", "kv", *args, ok=ok, env=env)
    return dict(line.split("=", 1) for line in stdout.splitlines() if "=" in line)


def checked(row, total, hits):
    assert int(row["total"]) == int(row["completed"]) == total, row
    assert int(row["fixture_hits"]) == hits, row


def dry_run_all():
    for n in range(1, 161):
        row = run("--puzzle", n, "--dry-run", "--rate", 17)
        length = (n+7)//8
        size = 1 << (n-1)
        assert row["low"] == f"{size:064x}" and row["high"] == f"{2*size-1:064x}"
        assert int(row["total"]) == int(row["original_space"]) == size
        assert float(row["percent_approx"]) == 100
        assert int(row["active_bytes"]) == length
        lo = size >> (8*(length-1))
        assert row["legal_first"] == f"{lo:02x}..{2*lo-1:02x}"
        assert int(row["eta_seconds_ceiling"]) == (size + 16)//17
        assert "completed" not in row
    with tempfile.TemporaryDirectory() as td:
        row = run("--puzzle", 160, "--first-draw-size", 1, "--draw-size", 3,
                  "--draw-seed", 369, "--dry-run", "--checkpoint", Path(td)/"x.json")
        assert not list(Path(td).iterdir())
        assert (row["first_allow"], row["shared_allow"]) == ("87", "63,70,aa")
        assert int(row["total"]) == 3**19
        assert row["ticket_id"] == hashlib.sha256(b"exlottery-ticket-v1\n160\n87\n63,70,aa\n").hexdigest()


def puzzle55_output():
    known = "6abe1f9b67e114"
    address = "1LzhS3k3e9Ub8i2W1V8xQFdB8n2MYCHPCa"
    base = ["--puzzle", 55, "--first-allow", "6a", "--known-key", known,
            "--address", address, "--threads", 3]
    allow = ["--allow", "14,1f,67,9b,be,e1"]
    expected = [
        known.zfill(64),
        "KwDiBf89QgGbjEhKnhXJuH7LrciVrZi3qYjidyYKE5NcJQZYvknA",
        "0385a30d8413af4f8f9e6312400f2d194fe14f02e719b24c3f83bf1fd233a8f963",
        "db53d9bbd1f3a83b094eeca7dd970bd85b492fa2",
        address,
    ]
    with tempfile.TemporaryDirectory() as td:
        checkpoint = Path(td) / "p55.json"
        default = cli_output(*base, *allow, "--checkpoint", checkpoint)
        assert checkpoint.stat().st_mode & 0o777 == 0o600
        assert "private :" not in default and "wif     :" not in default and "bytes   :" not in default
        assert expected[0] not in default and expected[1] not in default and "6a be 1f 9b 67 e1 14" not in default.lower()
        quiet = cli_output(*base, *allow, "--output", "human", "--quiet", "--show-private")
        for result in (default, quiet):
            assert result.count("*** FOUND") == 1, result
            for field in expected[2:]:
                assert field in result, (field, result)
            assert "fixture_hits=" not in result, result
        assert all(field in quiet for field in expected[:2])
        assert "6a be 1f 9b 67 e1 14" in quiet.lower()
        assert len(default.split("*** FOUND", 1)[0].strip()) > 0
        assert not quiet.split("*** FOUND", 1)[0].strip()
        resumed = cli_output("--resume", checkpoint)
        negative = cli_output(*base, "--allow", "14,1f,67,9b,be,e0")
        dry = cli_output(*base, *allow, "--dry-run", "--quiet")
        for result in (resumed, negative, dry):
            assert "*** FOUND" not in result and expected[1] not in result, result
        assert "=== Volante do puzzle 55 ===" in dry, dry
        assert "já confirmada no checkpoint" in resumed, resumed
        assert "=== run ===" in negative, negative
        checked(run(*base, *allow, "--quiet"), 6**6, 1)
        checked(run(*base, "--allow", "14,1f,67,9b,be,e0"), 6**6, 0)
        resumed_kv = run("--resume", checkpoint)
        checked(resumed_kv, 6**6, 1)
        assert resumed_kv["session_completed"] == "0"


def saved_ticket_cli():
    name = "Seleção.55"
    ticket = {"puzzle": 55, "first_allow": "6a", "shared_allow": "14,1f,67,9b,be,e1"}
    catalog = {"version": 1, "volantes": {name: ticket}}
    with tempfile.TemporaryDirectory() as td:
        env = dict(os.environ, XDG_CONFIG_HOME=td)
        data_path = Path(td) / "exlottery" / "volantes.json"
        data_path.parent.mkdir()
        data_path.write_text(json.dumps(catalog, ensure_ascii=False), encoding="utf-8")
        by_name = run("--volante", name, "--dry-run", env=env)
        manual = run("--puzzle", 55, "--first-allow", ticket["first_allow"],
                     "--allow", ticket["shared_allow"], "--dry-run", env=env)
        assert by_name == manual, (by_name, manual)
        checked(run("--volante", name, "--puzzle", 55, "--known-key", "6abe1f9b67e114",
                    "--threads", 2, env=env), 6**6, 1)
        for extra in [("--puzzle", 56), ("--first-allow", "6a"), ("--allow", "01"),
                      ("--draw-size", 3, "--draw-seed", 369)]:
            run("--volante", name, *extra, "--dry-run", env=env, ok=False)
        run("--volante", "Ausente", "--dry-run", env=env, ok=False)
        data_path.write_text("[]", encoding="utf-8")
        run("--volante", name, "--dry-run", env=env, ok=False)


def fixture_diagnostics():
    base = [EXE, "--puzzle", "55", "--first-allow", "40", "--allow", "00"]
    address = ["--address", "1111111111111111111114oLvT2"]
    for extra, expected in (
        (["--known-key", *address], "--known-key exige a HEX"),
        (["--known-key", "", *address], "--known-key exige a HEX"),
        (address, "catálogo público"),
    ):
        p = subprocess.run([*base, *extra], capture_output=True, text=True, timeout=3)
        assert p.returncode == 2 and not p.stdout and expected in p.stderr, p


def cli():
    fixture_diagnostics()
    saved_ticket_cli()
    puzzle55_output()
    checked(run("--puzzle", 1, "--fixture", "solved-1"), 1, 1)
    for mode, threads in [("reverse", 2), ("sequential", 4), ("reverse", 1)]:
        row = run("--puzzle", 39, "--first-allow", "4b", "--allow", "e9,03,83,5f",
                  "--address", "122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8",
                  "--mode", mode, "--threads", threads, "--batch", 7, "--affinity", "on")
        checked(row, 256, 1)
        assert row["execution_kind"] == "known_fixture"
    # Independently construct this product, then plant every member in turn.
    # Exactly one verified occurrence per member catches duplicates and holes
    # in the end-to-end worker loop, including short and partial segments.
    for mode, threads in (("sequential", 1), ("reverse", 2), ("sequential", 4)):
        for first, low in itertools.product(("02", "03"), ("00", "05", "ff")):
            row = run("--puzzle", 10, "--first-allow", "02,03", "--allow", "00,05,ff",
                      "--known-key", first+low, "--mode", mode, "--threads", threads,
                      "--batch", 4)
            checked(row, 6, 1)

    # This fixture is resolved internally from the public address.
    # This checks exhaustion and exit status, not unknown-key recovery.
    excluded = run("--puzzle", 39, "--first-allow", "4b", "--allow", "03,5f,83,e8",
                   "--address", "122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8", "--stop-on-hit")
    checked(excluded, 256, 0)
    assert excluded["stop_reason"] == "exhausted"
    expected_hash = bytes.fromhex(H39)
    for i in range(len(expected_hash)):
        mismatch = bytearray(expected_hash)
        mismatch[i] ^= 1
        run("--puzzle", 39, "--fixture", "synthetic-39", "--hash160", mismatch.hex(), "--dry-run", ok=False)

    with (FIXTURES/"known.tsv").open() as f:
        for fixture in csv.DictReader(f, delimiter="\t"):
            compressed = bytes.fromhex(fixture["compressed_pubkey"])
            assert hashlib.new("ripemd160", hashlib.sha256(compressed).digest()).hexdigest() == fixture["hash160"]
            for option, value in [("--fixture", fixture["name"]), ("--known-key", fixture["scalar_hex"])]:
                run("--puzzle", fixture["puzzle"], option, value, "--dry-run",
                    "--hash160", fixture["hash160"])
    base = ["--puzzle", 39, "--first-allow", "40"]
    row = run(*base, "--allow-file", FIXTURES/"allowlist.txt", "--hash160", H39)
    checked(row, 4096, 1)
    checked(run(*base, "--allow", "00,01,02,03", "--fixture", "synthetic-39"), 256, 0)
    checked(run("--puzzle", 17, "--first-allow", "01", "--allow", "ff,00", "--known-key", "100ff"), 4, 1)
    for extra in [
        ["--allow", "00,00"], ["--allow", "GG"], ["--threads", "-1"],
        ["--draw-size", 257, "--draw-seed", 8], ["--draw-size", 3],
        ["--allow", "00", "--draw-size", 1], ["--draw-seed", 1], ["--mode", "random"],
        ["--batch", 0], ["--simd", "bogus"], ["--allow-file", FIXTURES/"absent"],
        ["--hash160", "0"*40], ["--address", "122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT9"],
        ["--known-key", "0100"], ["--threads", 1, "--threads", 2],
    ]:
        run(*base, *extra, "--dry-run", ok=False)
    run("--puzzle", 39, "--first-allow", "80", "--dry-run", ok=False)
    run("--puzzle", 0, "--dry-run", ok=False)
    run("--puzzle", 161, "--dry-run", ok=False)


def fixture160():
    with tempfile.TemporaryDirectory() as td:
        path = Path(td)/"v.json"
        row = run(*P160, "--threads", 4, "--checkpoint", path)
        checked(row, 524288, 1)
        assert row["fixture_hash160"] == H160
        raw = json.loads(path.read_text())
        assert raw["ranges"] == [["0", "524288"]] and raw["completed"] == "524288"
        resumed = run("--resume", path)
        checked(resumed, 524288, 1)
        assert resumed["session_completed"] == "0"
        run("--resume", path, "--allow", "00", ok=False)
        run("--resume", path, "--mode", "reverse", ok=False)
        run("--resume", path, "--known-key", "80" + "01"*19, ok=False)
        run(*P160, "--checkpoint", path, ok=False)
        data = path.read_bytes().replace(b"524288", b"524289", 1)
        path.write_bytes(data)
        run("--resume", path, ok=False)
    checked(run("--puzzle", 160, "--first-allow", "81", "--allow", "00,01",
                "--fixture", "synthetic-160", "--threads", 4, "--mode", "reverse"), 524288, 0)


def restart():
    # Wait for an actual periodic commit before interrupting. Reference mode keeps
    # the experiment long enough on fast CPUs; resume finishes it with jumps.
    for sig, mode in [(signal.SIGTERM, "reverse"), (signal.SIGKILL, "sequential")]:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)/"state.json"
            cmd = [EXE, "--output", "kv", *P160, "--backend", "reference", "--batch", "64", "--checkpoint",
                   str(path), "--checkpoint-interval", "1", "--mode", mode, "--status", "1"]
            with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as proc:
                try:
                    deadline = time.monotonic() + 90
                    while time.monotonic() < deadline:
                        if proc.poll() is not None:
                            raise AssertionError(proc.communicate())
                        if path.exists() and int(json.loads(path.read_text())["completed"]) > 0:
                            break
                        time.sleep(0.03)
                    else:
                        raise TimeoutError("no periodic progress")
                    run("--resume", path, ok=False)
                    proc.send_signal(sig)
                    proc.communicate(timeout=15)
                    assert proc.returncode == (130 if sig == signal.SIGTERM else -sig)
                finally:
                    if proc.poll() is None:
                        proc.kill()
            old = json.loads(path.read_text())
            done = int(old["completed"])
            assert 0 < done < 524288
            assert sum(int(b)-int(a) for a,b in old["ranges"]) == done
            row = run("--resume", path, "--threads", 4, "--batch", 2037)
            checked(row, 524288, 1)
            assert int(row["session_completed"]) == 524288-done


def stop_on_hit():
    allowed = ",".join(f"{i:02x}" for i in range(16))
    args = ["--puzzle", 25, "--first-allow", "01", "--allow", allowed]
    known = ["--known-key", "01040702"]
    rank = 4 * 256 + 7 * 16 + 2
    for mode in ("sequential", "reverse"):
        for threads in (1, 4):
            with tempfile.TemporaryDirectory() as td:
                path = Path(td) / "coverage.json"
                result = run(*args, *known, "--stop-on-hit", "--threads", threads,
                             "--mode", mode, "--batch", 2**34, "--checkpoint", path)
                assert result["stop_reason"] == "confirmed_hit" and result["fixture_hits"] == "1"
                total = int(result["total"])
                done = int(result["completed"])
                first, end = (0, done) if mode == "sequential" else (total - done, total)
                assert first <= rank < end and total == 4096
                if threads == 1:
                    ahead = rank if mode == "sequential" else total - 1 - rank
                    assert done == (ahead // 256 + 1) * 256
                assert json.loads(path.read_text())["ranges"] == [[str(first), str(end)]]
                resumed = run("--resume", path, "--stop-on-hit")
                assert resumed["session_completed"] == "0" and resumed["stop_reason"] == "confirmed_hit"
                final = run("--resume", path)
                checked(final, total, 1)
                assert int(final["session_completed"]) == total - done
    for key, done in (("01000000", 1), ("010f0f0f", 4096)):
        row = run(*args, "--known-key", key, "--batch", 1, "--stop-on-hit")
        assert int(row["completed"]) == done and row["stop_reason"] == "confirmed_hit"
    negative = run(*args, "--known-key", "01041002", "--threads", 4, "--stop-on-hit")
    checked(negative, 4096, 0)
    assert negative["stop_reason"] == "exhausted"
    assert "stop_reason" not in run(*args, *known, "--stop-on-hit", "--dry-run")
    run(*args, "--hash160", "01"*20, "--stop-on-hit", ok=False)


def large39():
    for size in (8, 16, 32, 64):
        allow = ",".join(f"{n:02x}" for n in range(size))
        row = run("--puzzle", 39, "--first-allow", "40", "--allow", allow, "--hash160", H39,
                  "--threads", 4, "--affinity", "on")
        checked(row, size**4, 1)
        print(size, row["completed"], row["fixture_hits"], row["seconds"], row["candidates_per_second"], flush=True)


def search():
    # Unknown-key recovery: the engine only receives the target HASH160/address
    # (never the scalar) and must reproduce the private key by sweeping.
    addr = "122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8"
    h = "0b304f2a79a027270276533fe1ed4eff30910876"
    priv = "4b5f8303e9".zfill(64)
    wheel = ["--puzzle", 39, "--first-allow", "4b", "--allow", "03,5f,83,e9"]
    with tempfile.TemporaryDirectory() as td:
        found = Path(td) / "found.txt"
        row = run(*wheel, "--search", "--hash160", h, "--stop-on-hit",
                  "--threads", 4, "--found", found)
        assert row["execution_kind"] == "search" and row["mode_kind"] == "search"
        assert row["targets"] == "1" and row["matches"] == "1"
        assert row["stop_reason"] == "confirmed_hit"
        assert row["found_private"] == priv and row["found_address"] == addr
        assert row["found_hash160"] == h
        text = found.read_text()
        assert "=== HIT ===" in text and priv in text and addr in text

        # Target given as a P2PKH address; full sweep finds exactly one key.
        row = run(*wheel, "--search", "--address", addr, "--threads", 2,
                  "--found", Path(td) / "a.txt")
        assert row["matches"] == "1" and row["found_private"] == priv
        assert int(row["completed"]) == 256

        # A file listing the address and its HASH160 is a single deduped target.
        tfile = Path(td) / "targets.txt"
        tfile.write_text(f"# comentario\n{addr}\n{h}\n")
        row = run(*wheel, "--search", "--targets", tfile, "--found", Path(td) / "t.txt")
        assert row["targets"] == "1" and row["matches"] == "1"

        # Key absent from the wheel: exhausts, finds nothing, no hit recorded.
        neg = Path(td) / "neg.txt"
        row = run("--puzzle", 39, "--first-allow", "4b", "--allow", "00,01,02,03",
                  "--search", "--hash160", h, "--threads", 4, "--found", neg)
        assert row["matches"] == "0" and int(row["completed"]) == 256
        assert not neg.exists() or neg.read_text() == ""

    # Interrupt a long search and resume it from its own checkpoint.
    with tempfile.TemporaryDirectory() as td:
        ck = Path(td) / "state.json"
        allow = ",".join(f"{n:02x}" for n in range(32))  # 32^4, key absent
        cmd = [EXE, "--output", "kv", "--puzzle", "39", "--first-allow", "4b",
               "--allow", allow, "--search", "--hash160", h, "--backend", "reference",
               "--batch", "64", "--checkpoint", str(ck), "--checkpoint-interval", "1",
               "--found", str(Path(td) / "f.txt")]
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as proc:
            try:
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline:
                    if proc.poll() is not None:
                        raise AssertionError(proc.communicate())
                    if ck.exists() and int(json.loads(ck.read_text())["completed"]) > 0:
                        break
                    time.sleep(0.03)
                else:
                    raise TimeoutError("no search progress")
                proc.send_signal(signal.SIGTERM)
                proc.communicate(timeout=15)
                assert proc.returncode == 130
            finally:
                if proc.poll() is None:
                    proc.kill()
        saved = json.loads(ck.read_text())
        assert saved["metadata"]["mode_kind"] == "search" and saved["metadata"]["targets"] == h
        done = int(saved["completed"])
        assert 0 < done < 32**4
        run("--resume", ck, "--hash160", "11" * 20, ok=False)   # mismatched target
        row = run("--resume", ck)                               # self-contained resume
        checked_total = 32**4
        assert int(row["completed"]) == checked_total and row["matches"] == "0"
        assert int(row["session_completed"]) == checked_total - done


if __name__ == "__main__":
    {"cli":cli, "dry_run_all":dry_run_all, "fixture160":fixture160,
     "restart":restart, "stop_on_hit":stop_on_hit, "large39":large39,
     "search":search}[sys.argv[2]]()
