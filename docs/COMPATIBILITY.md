# Compatibility

## Platforms

- **OS:** Linux, x86-64. Developed on Debian/Kali; CI builds and tests on Ubuntu 24.04.
  Other modern distributions work with the equivalent packages.
- **Compilers:** g++ and clang++ (both exercised in CI, for the 5x52 and 4x64 field
  representations).
- **Python panel:** Python 3.8+ with Tk (`python3-tk`). The GUI tests also need `xvfb`.

## Build dependencies

```bash
sudo apt-get install build-essential cmake pkg-config libboost-dev libssl-dev \
                     libsecp256k1-dev python3 python3-tk
```

## CPU portability

The default build is **portable**: it does **not** pass `-march=native`, so the binary
targets the generic x86-64 baseline and runs on any x86-64 CPU.

- **SIMD is chosen at runtime.** Multi-buffer SHA-256/RIPEMD-160 uses an SSSE3 backend
  when the CPU reports SSSE3, and falls back to a scalar path otherwise — so the same
  binary runs on old and new CPUs without crashing. Toggle the SSSE3 backend with
  `-DEXLOTTERY_SSE=ON|OFF` (default ON, runtime-detected).
- **Host tuning (optional, non-portable):** `-DEXLOTTERY_NATIVE=ON` compiles with
  `-march=native` for a few percent more throughput on the build machine, at the cost of
  a binary that may not run on older CPUs. Use it only for a binary you run locally.

```bash
# portable (default) — ship this
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
# tuned for THIS machine only
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEXLOTTERY_NATIVE=ON
```

## Modern CPUs (AVX2) — status

An AVX2 multi-buffer hashing backend exists in the base project
[excalibur369](https://github.com/cetusquantico/excalibur369) (`backend_avx2.cpp`) and
roughly doubles hashing throughput on Haswell (2013) and newer. It has **not yet** been
vendored into this project, whose crypto copy (`vendor/ex369`) currently ships the SSSE3
backend only. Vendoring it with runtime dispatch (AVX2 → SSSE3 → scalar) is planned.

Note: the primary development machine is an Intel i5-3470 (Ivy Bridge), which has AVX but
**not** AVX2, so the AVX2 path is validated on the base project rather than here.

## Known-good matrix

| Component | Status |
|---|---|
| Ubuntu 24.04 · g++ / clang++ · 5x52 & 4x64 | ✅ CI |
| Debian / Kali (dev) | ✅ |
| x86-64 without SSSE3 | ✅ scalar fallback |
| AVX2 (Haswell+) | ⏳ backend exists in excalibur369; vendoring planned |
| ARM / non-x86 | ❌ not targeted |
