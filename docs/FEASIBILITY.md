# What's feasible: brute force, wheels, and kangaroo

There are three ways to attack a puzzle, and each has a ceiling set by math, not by code.
This note is honest about where each one actually lands — including on a GPU.

## The public-key divide (the single most important fact)

A puzzle address publishes a **hash160**. The **public key** behind it is revealed only if
that address has ever *spent* (sent a transaction). That splits the puzzles in two:

- **Address-only (pubkey hidden)** → you can only **brute force**: make a key, hash to
  hash160, compare. Cost = the whole interval. The **lowest unsolved** puzzle, **#71**, is here.
- **Public-key exposed** → **Pollard's kangaroo** applies, costing ~√(interval) instead of the
  whole interval. The high puzzles the creator exposed (…, 120, 125, 130, 135, 140, 145, 150,
  155, 160) are here.

Kangaroo needs the public key because it walks on the elliptic curve (points); brute force
works from the hash. **No pubkey → no kangaroo.**

## 1. Brute force (address-only)

Cost = the full interval, `2^(N-1)` keys. On one RTX 4070 (~1.5 billion keys/s, approx):

| puzzle | interval | 1×4070 |
|---|---|---|
| 63 | 2^62 | ~97 years |
| **71** (lowest unsolved) | 2^70 | ~25,000 years |

#71 is the wall for address-only puzzles — even optimistic community estimates put it in the
hundreds of years on a single rig. This is what the LOTTERY369 engine does, and it is exactly
why the tool is honest that it will not find an unknown key much above ~70 bits.

## 2. The byte wheel (restricting allowed byte values)

Tempting idea: the solved keys seem to use only ~80 of the 256 byte values, so restrict each
byte to 80 values and the search shrinks. It does shrink — but so does the chance the key is
even inside it:

- coverage = `(80/256)^(free bytes)`. For puzzle 63 (7 free bytes): **0.029%**.
- That 80-value wheel for #63 **runs** in ~**10 days** on a 4070 — but has a **0.029%** chance
  of **containing** the key.

You trade a smaller search for a proportionally smaller hit chance; the product — your real
odds — does not move. The bytes are uniform, so no value is "hot." Same lesson from two other
angles in [FAMILIES.md](FAMILIES.md) and [FUNDING.md](FUNDING.md). **Running fast is not the
same as finding.**

## 3. Kangaroo (public-key exposed)

Cost ≈ `2·√(2^(N-1))`. A massive speedup — but √ of an astronomical number is still
astronomical. On one RTX 4070 (~2 billion jumps/s, generous):

| puzzle (pubkey exposed) | work ≈ | 1×4070 | 4070s for 1 year |
|---|---|---|---|
| 140 | 2^70 | ~26,500 years | ~26,000 |
| 145 | 2^73 | ~150,000 years | ~150,000 |
| 150 | 2^76 | ~850,000 years | ~850,000 |
| 155 | 2^78 | ~4.8M years | ~4.8M |
| 160 | 2^80 | ~27M years | ~27M |

### What one 4070 actually reaches (pubkey known)

| budget | up to ~puzzle |
|---|---|
| 1 hour | ~84 |
| 1 day | ~94 |
| 1 month | ~103 |
| 1 year | ~111 |

A single 4070's realistic kangaroo frontier is **~puzzle 100–110** — not 140+.

## The real-world scoreboard (the honest proof)

The author of **RCKangaroo** (RetiredCoder) — a fast, state-of-the-art GPU kangaroo (a
3-kangaroo method reaching K≈1.15 vs the classic ~2.1) — reported solving the pubkey-exposed
puzzles **#120, #125, #130 and #135** (Bitcointalk, Nov 2024). **#135 reportedly took on the
order of 200 GPUs for ~5 months.**

That is the edge of the state of the art. #140 is ~6× more work than #135 (`2^70.5` vs
`2^68`), which is precisely why it is **still unsolved even though its public key has been
exposed for years**. The scoreboard is the proof: if a single GPU could crack 140–160, it
would have happened already.

## Honest bottom line

- **Pubkey exposed ≠ feasible.** The range dominates; √(huge) is still huge.
- **One 4070's real reach:** address-only brute into the mid-60-bit puzzles (with patience);
  kangaroo up to ~puzzle 100–110 when the pubkey is known.
- **140–160 need planet-scale pools** and remain beyond even the #135 record.
- **No byte pattern, funding coincidence, or numeric trick changes any of this** — the keys
  are uniformly random.

## Credits & further reading

- **RCKangaroo** by RetiredCoder — SOTA GPU Pollard kangaroo; the reference for the kangaroo
  numbers here.
- Puzzle status trackers: *Bitcoin-Puzzle-Info* (`github.com/roadhero/Bitcoin-Puzzle-Info`).
- Announcement thread: Bitcointalk topic `5517607`.

> GPU times assume ~1.5e9 keys/s (brute) and ~2e9 jumps/s (kangaroo) on a single RTX 4070 —
> order-of-magnitude figures to compare approaches, not benchmarks.
