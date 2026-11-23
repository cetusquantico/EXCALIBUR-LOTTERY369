#!/usr/bin/env python3
"""Measure the complete pipeline on the built-in synthetic puzzle 39 fixture."""
import argparse
import csv
import hashlib
import math
import os
import platform
import re
import statistics
import subprocess
import time
from datetime import datetime, timezone
from itertools import product
from pathlib import Path


def nonzero(text):
    value = int(text)
    if value < 1:
        raise argparse.ArgumentTypeError("expected positive integer")
    return value


def scenarios():
    for size, backend in ((8, "reference"), (8, "jump"), (16, "jump")):
        for simd, threads in product(("scalar", "sse"), (1, 2, 4)):
            yield size, backend, simd, threads
    for size in (32, 64):
        yield size, "jump", "sse", 4


def measure(binary, scenario):
    size, backend, simd, threads = scenario
    batch = 4096 if size >= 32 else 256
    allowed = ",".join(f"{byte:02X}" for byte in range(size))
    args = [str(binary), "--output", "kv", "--puzzle", "39", "--first-allow", "40", "--allow", allowed,
            "--fixture", "synthetic-39", "--threads", str(threads), "--affinity", "on",
            "--batch", str(batch), "--backend", backend, "--simd", simd]
    start = time.monotonic()
    p = subprocess.run(args, text=True, capture_output=True, timeout=300)
    wall = time.monotonic() - start
    if p.returncode != 0:
        raise ValueError(f"CLI returned {p.returncode} for {args!r}\n{p.stderr}")
    values = dict(line.split("=", 1) for line in p.stdout.splitlines() if "=" in line)
    total = size ** 4
    if any(int(values[field]) != total for field in ("completed", "session_completed")):
        raise ValueError(f"incomplete scenario {scenario}: {values}")
    if int(values["fixture_hits"]) != 1:
        raise ValueError(f"fixture missing or repeated for {scenario}")
    rate = float(values["candidates_per_second"])
    seconds = float(values["seconds"])
    if seconds <= 0 or rate <= 0 or not math.isfinite(rate):
        raise ValueError(f"invalid reported time or rate for {scenario}")
    if not math.isclose(rate * seconds, total, rel_tol=0.001):
        raise ValueError(f"reported rate inconsistent with confirmed coverage for {scenario}")
    return dict(size=size, backend=backend, simd=simd, threads=threads, batch=batch,
                completed=total, fixture_hits=1, seconds=seconds,
                candidates_per_second=rate, process_wall_seconds=round(wall, 6))


def build_details(binary):
    details = {}
    config = binary.parent / "CMakeCache.txt"
    if config.exists():
        entries = re.findall(r"^(\w+):[A-Z]+=(.*)$", config.read_text(), flags=re.MULTILINE)
        details.update(entries)
    compiler = details.get("CMAKE_CXX_COMPILER")
    if compiler:
        details["compiler_version"] = subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]
    return details


def report(args, binary, rows, started):
    groups = {}
    for row in rows:
        group = tuple(row[name] for name in ("size", "backend", "simd", "threads"))
        groups.setdefault(group, []).append(row["candidates_per_second"])
    cpu = re.search(r"^model name\s*:\s*(.*)$", Path("/proc/cpuinfo").read_text(), re.MULTILINE)
    info = build_details(binary)
    raw = os.path.relpath(args.output.resolve(), args.report.resolve().parent)
    lines = ["# Benchmark da fixture sintética do puzzle 39", "",
             f"Medição iniciada em {started}. Foram executadas {args.repetitions} repetições de cada um dos "
             f"{len(groups)} cenários no processador {cpu.group(1) if cpu else platform.machine()}.",
             "", f"Linux {platform.release()}. Compilador {info.get('compiler_version', 'desconhecido')}. "
             f"Build {info.get('CMAKE_BUILD_TYPE', 'desconhecido')}. "
             f"Corpo finito {info.get('EXLOTTERY_FIELD', 'desconhecido')}. "
             f"SHA-256 do executável `{hashlib.sha256(binary.read_bytes()).hexdigest()}`.",
             "", "A fixture tem primeiro byte 40 e bytes compartilhados consecutivos iniciando em 00. "
             "Todos os cenários validaram uma ocorrência conhecida e a conclusão dos "
             "4.096, 65.536, 1.048.576 ou 16.777.216 candidatos. A taxa divide essa contagem pelo tempo de execução "
             "reportado pelo motor, incluindo início e encerramento dos workers, preparação de tabelas, "
             "enumeração, multiplicações ou saltos EC, serialização comprimida, SHA-256, RIPEMD-160 "
             "e verificação da fixture. Cada repetição inicia um processo novo. O tempo de parede "
             f"do subprocesso também consta nos [dados brutos]({raw}).",
             "", "As repetições alternam a ordem dos cenários a cada rodada. Cada worker usa "
             "afinidade de CPU. Os segmentos têm 256 candidatos para os tamanhos 8 e 16, e 4.096 para "
             "os tamanhos 32 e 64. Os volantes curtos tornam o custo "
             "de inicialização relevante. A taxa histórica de 7,095 milhões de candidatos/s do "
             "EXCALIBUR369 foi obtida com outra carga de trabalho.",
             "", "| Bytes compartilhados | EC | HASH160 | Threads | Mediana (cand/s) | Mínimo | Máximo |",
             "|---:|---|---|---:|---:|---:|---:|"]
    for (size, backend, simd, threads), rates in sorted(groups.items()):
        lines.append(f"| {size} | {backend} | {simd} | {threads} | {statistics.median(rates):.0f} | "
                     f"{min(rates):.0f} | {max(rates):.0f} |")
    lines.extend(["", "Para repetir a matriz após a compilação Release, na raiz do projeto, execute "
                  "`python3 bench/run.py --binary build/exlottery`. O cenário SSE exige suporte SSSE3.",
                  ""])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/exlottery"))
    parser.add_argument("--output", type=Path, default=Path("bench/results.csv"))
    parser.add_argument("--report", type=Path, default=Path("docs/BENCHMARKS.md"))
    parser.add_argument("--repetitions", type=nonzero, default=9)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    started = datetime.now(timezone.utc).isoformat(timespec="seconds")
    cases = list(scenarios())
    rows = []
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fields = ("round", "size", "backend", "simd", "threads", "batch", "completed",
              "fixture_hits", "seconds", "candidates_per_second", "process_wall_seconds")
    with args.output.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for i in range(1, args.repetitions + 1):
            for scenario in cases if i % 2 else reversed(cases):
                row = dict(round=i, **measure(binary, scenario))
                rows.append(row)
                writer.writerow(row)
                f.flush()
            print(f"round {i}/{args.repetitions} complete", flush=True)
    report(args, binary, rows, started)


if __name__ == "__main__":
    main()
