# Puzzle families (fixed top-byte range)

Each Bitcoin puzzle `N` has its private key in the interval `[2^(N-1), 2^N)`. Written as
bytes, the **top byte is structurally constrained** to a fixed range that depends only on
`(N-1) mod 8`. Puzzles whose numbers differ by 8 therefore share the same top-byte range —
a **family**. This is a deterministic consequence of the puzzle definition, not a
statistical hint.

| family (smallest N) | top byte | values | members (N) |
|---|---|---|---|
| 1  | `01`      | 1   | 1, 9, 17, 25, … |
| 2  | `02–03`   | 2   | 2, 10, 18, 26, … |
| 3  | `04–07`   | 4   | 3, 11, 19, 27, … |
| 4  | `08–0F`   | 8   | 4, 12, 20, 28, … |
| 5  | `10–1F`   | 16  | 5, 13, 21, 29, … |
| 6  | `20–3F`   | 32  | 6, 14, 22, 30, … |
| 7  | `40–7F`   | 64  | 7, 15, 23, 31, 39, 47, 55, 63, 71, … |
| 8  | `80–FF`   | 128 | 8, 16, 24, 32, … |

For a key of `N` bits, `ceil(N/8)` bytes: the top byte is pinned to its family's range and
every other byte is a free `00–FF`.

## What this does and does not buy you

- It **does** pin the top byte: the panel's *Primeiro byte / First byte* tab only lets you
  pick values in the family's range, and the engine searches exactly `[2^(N-1), 2^N)`. The
  search space is already `2^(N-1)`, i.e. the top-byte constraint is baked in.
- It **does not** make an unknown key findable. The other bytes are uniform, so there is no
  "hot" value; brute force above ~70 bits stays infeasible on any hardware. The panel's
  coverage readout is the honest probability.

## In the panel

Turn on **Frequência histórica / Historical frequency** to tint the grid by how often each
byte appears across the solved keys (it comes out as flat noise — a visual proof of
uniformity). The family line under the grid shows the current puzzle's family: its fixed
top-byte range, its members, and which of them are solved. **Só desta família / This family
only** scopes the frequency to that family's solved keys.
