# Benchmark da fixture sintética do puzzle 39

Medição iniciada em 2026-11-21T10:02:58+00:00. Foram executadas 9 repetições de cada um dos 20 cenários no processador Intel(R) Core(TM) i5-3470 CPU @ 3.20GHz.

Linux 7.1.5+kali-amd64. Compilador c++ (Debian 16.2.0-1) 16.2.0. Build Release. Corpo finito 5x52. SHA-256 do executável `9252537ed64864830f2030cf158a8d0c88f154af00dbd6653e9189496a408105`.

A fixture tem primeiro byte 40 e bytes compartilhados consecutivos iniciando em 00. Todos os cenários validaram uma ocorrência conhecida e a conclusão dos 4.096, 65.536, 1.048.576 ou 16.777.216 candidatos. A taxa divide essa contagem pelo tempo de execução reportado pelo motor, incluindo início e encerramento dos workers, preparação de tabelas, enumeração, multiplicações ou saltos EC, serialização comprimida, SHA-256, RIPEMD-160 e verificação da fixture. Cada repetição inicia um processo novo. O tempo de parede do subprocesso também consta nos [dados brutos](../bench/results.csv).

As repetições alternam a ordem dos cenários a cada rodada. Cada worker usa afinidade de CPU. Os segmentos têm 256 candidatos para os tamanhos 8 e 16, e 4.096 para os tamanhos 32 e 64. Os volantes curtos tornam o custo de inicialização relevante. A taxa histórica de 7,095 milhões de candidatos/s do EXCALIBUR369 foi obtida com outra carga de trabalho.

| Bytes compartilhados | EC | HASH160 | Threads | Mediana (cand/s) | Mínimo | Máximo |
|---:|---|---|---:|---:|---:|---:|
| 8 | jump | scalar | 1 | 310709 | 301112 | 314421 |
| 8 | jump | scalar | 2 | 394859 | 382504 | 397432 |
| 8 | jump | scalar | 4 | 438611 | 342513 | 448677 |
| 8 | jump | sse | 1 | 383508 | 375144 | 387610 |
| 8 | jump | sse | 2 | 451090 | 431275 | 456972 |
| 8 | jump | sse | 4 | 479661 | 432497 | 498053 |
| 8 | reference | scalar | 1 | 96757 | 87328 | 98299 |
| 8 | reference | scalar | 2 | 164628 | 145559 | 166165 |
| 8 | reference | scalar | 4 | 241711 | 216104 | 246341 |
| 8 | reference | sse | 1 | 102258 | 100800 | 104144 |
| 8 | reference | sse | 2 | 172299 | 168526 | 175675 |
| 8 | reference | sse | 4 | 253971 | 245617 | 258244 |
| 16 | jump | scalar | 1 | 651747 | 644237 | 669391 |
| 16 | jump | scalar | 2 | 1205819 | 1149396 | 1221065 |
| 16 | jump | scalar | 4 | 2022239 | 1628833 | 2049581 |
| 16 | jump | sse | 1 | 1083589 | 1072726 | 1089134 |
| 16 | jump | sse | 2 | 1926159 | 1732651 | 1939875 |
| 16 | jump | sse | 4 | 3026399 | 2619230 | 3062566 |
| 32 | jump | sse | 4 | 4460482 | 4428775 | 4543848 |
| 64 | jump | sse | 4 | 4608465 | 4578649 | 4621230 |

Para repetir a matriz após a compilação Release, na raiz do projeto, execute `python3 bench/run.py --binary build/exlottery`. O cenário SSE exige suporte SSSE3.
