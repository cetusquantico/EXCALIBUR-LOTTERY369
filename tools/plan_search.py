#!/usr/bin/env python3
"""Planeja uma busca a partir da chave de teste conhecida.

Dada a HEX da chave privada (e o volante), informa se ela cai no volante,
o rank dentro dele, a direcao otima (sequential/reverse) e imprime o comando
pronto para rodar. Util para os testes em que voce mesmo gera a chave.

O puzzle e o volante vem de variaveis de ambiente (com os padroes abaixo),
entao o mesmo script serve para qualquer puzzle:

    python3 tools/plan_search.py <PRIV_HEX> [ENDERECO]
    PUZZLE=63 python3 tools/plan_search.py <PRIV_HEX> [ENDERECO]
    PUZZLE=63 FIRST="4a,5e" ALLOW="00,01,.." python3 tools/plan_search.py <HEX>
"""
import os
import sys

EXE = "/root/EXCALIBUR-LOTTERY369/build-opt/exlottery"
PUZZLE = int(os.environ.get("PUZZLE", "55"))
FIRST = os.environ.get("FIRST", "4a,5e,67,6a,6f,7a")
ALLOW = os.environ.get("ALLOW",
                       "ff,9a,4a,ab,de,e1,9b,a1,14,fe,f1,1f,df,67,01,23,00,be,"
                       "ac,ce,7a,33,dd,aa,bb,ad,0b,5e,6f,6a")
RATE = int(os.environ.get("RATE", "4500000"))  # candidatos/s (4 threads, i5)


def main():
    if len(sys.argv) < 2:
        sys.exit("uso: plan_search.py <PRIV_HEX> [ENDERECO]  "
                 "(PUZZLE/FIRST/ALLOW via variaveis de ambiente)")
    length = (PUZZLE + 7) // 8
    k = int(sys.argv[1], 16)
    lo, hi = 1 << (PUZZLE - 1), (1 << PUZZLE) - 1
    if not (lo <= k <= hi):
        sys.exit(f"chave fora da faixa do puzzle {PUZZLE} "
                 f"[{lo:x}..{hi:x}]")
    kb = k.to_bytes(length, "big")
    first = sorted(int(x, 16) for x in FIRST.split(","))
    shared = sorted({int(x, 16) for x in ALLOW.split(",")})
    if kb[0] not in first:
        sys.exit(f"1o byte {kb[0]:02x} NAO esta em FIRST -> fora do volante")
    for b in kb[1:]:
        if b not in shared:
            sys.exit(f"byte {b:02x} NAO esta em ALLOW -> fora do volante")
    rank = first.index(kb[0])
    for b in kb[1:]:
        rank = rank * len(shared) + shared.index(b)
    total = len(first) * len(shared) ** (length - 1)
    seq, rev = rank + 1, total - rank
    mode = "sequential" if seq <= rev else "reverse"
    scan = min(seq, rev)
    print(f"chave     : {kb.hex()}  (dentro do volante)")
    print(f"rank      : {rank:,} de {total:,}  ({100 * rank / total:.1f}%)")
    print(f"direcao   : {mode}  -> varre ~{scan:,} candidatos "
          f"(~{scan / RATE:.0f}s a {RATE/1e6:.1f}M/s)")
    addr = sys.argv[2] if len(sys.argv) > 2 else "<ENDERECO>"
    print("\ncomando:")
    print(f'{EXE} \\\n'
          f'  --puzzle {PUZZLE} --first-allow "{FIRST}" \\\n'
          f'  --allow "{ALLOW}" \\\n'
          f'  --search --address {addr} --mode {mode} '
          f'--stop-on-hit --threads 4 --affinity on \\\n'
          f'  --status 15 --found achado.txt')


if __name__ == "__main__":
    main()
