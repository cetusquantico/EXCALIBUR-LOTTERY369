# Puzzle funding amounts (what they encode — and what they don't)

The amount of bitcoin in each puzzle is not random: it encodes the puzzle number. These
are **economic facts** about how the challenge was funded. They say **nothing** about the
private keys, which were generated at random.

## The two facts

- **Original funding (2015-01-15).** A single transaction with 256 outputs funded puzzles
  1–256. Puzzle `N` received `N × 0.001 BTC` — the balance *is* the puzzle number
  (e.g. puzzle 63 → 0.063 BTC, puzzle 71 → 0.071 BTC).
- **Top-up (2017-07-11).** A second transaction added `N × 9 × 0.001 BTC` to each puzzle —
  9× the original — raising every balance to `N × 0.01 BTC`, a 10× prize increase
  (e.g. puzzle 63 → +0.567, puzzle 71 → +0.639).

So for any puzzle `N`: first amount `= N × 0.001`, second amount `= 9 × (N × 0.001)`.

## What this is NOT

The amounts were a human choice (encode the index, then scale the prize). The private keys
were drawn at random. **There is no mathematical relationship between a puzzle's balance and
its key**, and knowing the amount tells you nothing about the key.

Arithmetic on the amounts can *look* like it lands on a key's bytes for a cherry-picked case,
but it does not generalize. Example seen in the wild: `N × 9 × 100 / 2 = N × 450` compared to
the top two bytes of the previous family member's key. It matches for `N = 63` and `N = 71`
(offsets 1024 and 0), but testing the same formula across the whole family 7 the required
offset swings from **−16517 to +6674** — i.e. the two "hits" were the cases that happened to
fall near zero, not a rule. The reason is mundane: `N × 450` and a family's top‑two‑bytes
occupy the same numeric range (~16k–33k) only for `N ≈ 55–71`, so near‑misses there are
inevitable; outside that band it fails badly.

More generally, any value `mod 256` yields a byte, any division leaves a remainder, and with a
free additive offset you can hit any 16‑bit target after the fact. A pattern that only works
on already‑public keys, needs an adjustable fudge factor, and can't be validated on an unknown
key is a coincidence, not a predictor. The keys are uniform; there is no "hot" value.

## What *is* real and useful

- **Top‑byte families** — a deterministic structural fact (see [FAMILIES.md](FAMILIES.md)).
- **Coverage / odds** — the panel shows the honest probability that a wheel contains the key.

Neither makes an unknown key findable; they just keep the search honest about its (tiny) odds.
