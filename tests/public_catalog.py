"""Verify offline public-puzzle planning against the original Bitcoin transaction."""
import hashlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile

TXID = "08389f34c98c606322740c0be6a7125d9860bb8d5cb182c02f98461e5fa6cd15"
ROOT = Path(__file__).resolve().parents[1]


def sha256d(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def funding_addresses():
    raw = bytes.fromhex((ROOT / "fixtures/puzzle_funding_tx.hex").read_text())
    assert sha256d(raw)[::-1].hex() == TXID
    stream = io.BytesIO(raw)

    def take(n):
        data = stream.read(n)
        assert len(data) == n
        return data

    def integer(n):
        return int.from_bytes(take(n), "little")

    def compact_size():
        prefix = integer(1)
        return prefix if prefix < 253 else integer({253: 2, 254: 4, 255: 8}[prefix])

    assert integer(4) == 1
    for _ in range(compact_size()):
        take(36)
        take(compact_size())
        take(4)
    assert compact_size() == 256
    addresses = []
    for puzzle in range(1, 257):
        assert integer(8) == 100_000 * puzzle
        script = take(compact_size())
        assert len(script) == 25 and script[:3] == bytes.fromhex("76a914") and script[-2:] == bytes.fromhex("88ac")
        if puzzle <= 160:
            payload = bytes(1) + script[3:-2]
            wire = payload + sha256d(payload)[:4]
            n = int.from_bytes(wire, "big")
            address = ""
            while n:
                n, digit = divmod(n, 58)
                address = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"[digit] + address
            leading_zeroes = len(wire) - len(wire.lstrip(bytes(1)))
            addresses.append("1" * leading_zeroes + address)
    assert take(4) == bytes(4) and not stream.read()
    assert len(set(addresses)) == 160
    return addresses


def check(binary):
    addresses = funding_addresses()
    assert addresses[70] == "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU"
    for n, address in enumerate(addresses, 1):
        args = [binary, "--puzzle", str(n), "--dry-run", "--output", "kv", "--address"]
        ok = subprocess.run(args+[address], text=True, capture_output=True, check=True)
        fields = dict(line.split("=", 1) for line in ok.stdout.splitlines())
        assert fields["public_address"] == address and "fixture_hash160" not in fields
        assert "completed" not in fields and "FOUND" not in ok.stdout
        wrong = subprocess.run(args+[addresses[n % 160]], text=True, capture_output=True)
        assert wrong.returncode == 2 and not wrong.stdout and "catálogo público" in wrong.stderr
    with tempfile.TemporaryDirectory() as directory:
        for address in (addresses[0], addresses[70], addresses[70][:-1] + "V"):
            refused = subprocess.run([binary, "--puzzle", "71", "--first-allow", "40", "--allow", "00",
                                      "--checkpoint", directory + "/run.json", "--address", address],
                                     text=True, capture_output=True, timeout=5)
            assert refused.returncode == 2 and not refused.stdout and not list(Path(directory).iterdir())
    print("160 identities and 160 mismatches verified from the pinned transaction")


if __name__ == "__main__":
    check(sys.argv[1])
