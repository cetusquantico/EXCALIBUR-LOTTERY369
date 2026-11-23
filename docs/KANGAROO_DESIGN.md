# EXCALIBUR-LOTTERY369 — Design: Volante + Kangaroo

> Documento de design / estudo. Objetivo: entender o algoritmo Kangaroo (via
> RCKangaroo), a matemática EC envolvida, e responder com rigor se o método
> "volante" (`--first-allow` + `--allow`) pode ser combinado com Kangaroo dentro
> do EXCALIBUR. Era um passo de entendimento anterior à implementação.

Autor: cetusquantico · Data: 2026-11-22 · Status: rascunho de arquitetura

> **Atualização (2026-11-23) — já implementado.** O Kangaroo deixou de ser proposta e
> virou o projeto **EXCALIBUR-KANGAROO369** (repositório próprio, público, licença MIT,
> CI verde). O que este documento previa está construído — dispatcher, intervalo exato do
> puzzle, pontos distinguidos, checkpoint/resume, ranges de 2 a 160 bits — e validado em
> dado real: **puzzle 68 resolvido a partir da chave pública em ~50 min** no i5-3470.
>
> Correção importante à seção 1.4: a simetria `±P` **não** entrega `K≈1,15` de graça.
> Escolher o sinal do salto pela paridade de `y` torna o passeio um **random walk 1-D
> recorrente**, que acima de ~52 bits trava em laços que re-batem os mesmos DPs. O conserto
> (a "Fase 6" do outro repo) foi reimplementar a simetria como **caminhadas Gaudry–Schost**
> (recomeço num ponto aleatório após cada DP). K medido ≈2 — hoje empata com o Kangaroo
> plano; chegar perto do 1,36 teórico é ajuste fino futuro.

---

## 0. TL;DR (a resposta curta)

- **Brute force (volante)** e **Kangaroo** resolvem problemas **diferentes** e têm
  custos que dependem de coisas **diferentes**:
  - Brute: custo = **nº de candidatos** que você testa. O volante reduz isso.
  - Kangaroo: custo = **`K·√(largura do intervalo)`**, `K≈1,15`. **Independe** de
    quantos candidatos existem no intervalo.
- Por isso `--allow` (um filtro de candidatos) **não tem onde se encaixar** no
  Kangaroo para os bytes do meio. É uma otimização de *enumeração*; Kangaroo não
  enumera, ele **resolve o log discreto** direto.
- **Porém**, parte da sua ideia é real e aproveitável:
  - `--first-allow` (byte alto) = **bits altos conhecidos** → encolhe o intervalo
    → o Kangaroo **aproveita** (é o `-start`/`-range` do RCKangaroo). ✅
  - `--allow` no **byte baixo** = resíduo `k mod 256 ∈ S` → dá pra reduzir via
    substituição e **aproveita** se `|S|` for pequeno. ✅ (parcial)
  - `--allow` nos **bytes do meio** = conjunto esparso → **não aproveita**. ❌
- **Conclusão de arquitetura:** a junção certa é um **dispatcher**
  (pubkey exposta → Kangaroo; sem pubkey → brute+volante), com um **front-end de
  volante unificado** onde `--first-allow` significa "bits altos conhecidos" para
  *os dois* motores. Isso é limpo, honesto e — até onde sabemos — ninguém montou
  exatamente assim.

---

## 1. Como o Kangaroo funciona

### 1.1 O problema (ECDLP num intervalo)
Dado o ponto público `P = k·G` na secp256k1, e sabendo que `k ∈ [a, a+W)` (um
intervalo de largura `W`), achar `k`. É o **log discreto com intervalo**. Só é
possível quando a **pubkey está exposta** (ao contrário do brute, que só precisa
do endereço/hash160).

### 1.2 A ideia dos cangurus (tame & wild)
- Solta-se uma manada de **cangurus domados (tame)** com posição inicial
  **conhecida** (a partir de `a` ou do meio do intervalo) e **cangurus selvagens
  (wild)** partindo de `P` (posição = `k` + offset desconhecido).
- Cada canguru dá "saltos" pseudo-aleatórios: do ponto atual, escolhe um salto de
  uma **tabela de saltos** em função do próprio `x` (ex.: `x[0] % JMP_CNT`), soma
  esse ponto, e **acumula a distância** (escalar) percorrida.
- Como tame e wild andam no mesmo "terreno", cedo ou tarde um wild **cai na
  trilha** de um tame (colisão). Nesse ponto:
  `k = dist_tame − dist_wild` (as distâncias que cada um anotou).
- Probabilisticamente isso acontece em **`≈ K·√W`** saltos. É o paradoxo do
  aniversário aplicado a passeios aleatórios.

### 1.3 Distinguished Points (DP) — como detectar colisão sem guardar tudo
Guardar cada ponto visitado é inviável. Truque: só se registra um ponto quando ele
é **"distinto"** — no RCKangaroo, quando os bits baixos da palavra alta de `x`
são zero: `((u32*)x)[7] & dp_mask == 0`, com `dp_mask = (1<<DP)−1`, `DP ∈ 14..32`.
- Em média, 1 a cada `2^DP` pontos é DP.
- Guarda-se só os DPs numa tabela (hash). Colisão = dois DPs com mesmo `x`.
- Trade-off: `DP` alto → tabela menor, mas "overhead" de passos até cada DP maior.
  Memória ≈ `(32+4+4)·ops / 2^DP`.

### 1.4 Simetria (o "SOTA v2" do RCKangaroo)
RCKangaroo usa **Kangaroo com simetria** (mapa `±P`, negação): como `−P` é trivial
de calcular, o espaço efetivo cai pela metade. Resultado: **`K = 1,15`**, contra
`K = 2,1` do método clássico (≈1,8× menos operações); e melhor que os "3-way
kangaroo" de `K≈1,6`. Manadas: ~1/3 tame, ~2/3 wild.
`ops ≈ K·2^(range/2)`.

> ⚠️ Esse `K=1,15` é o **alvo teórico ideal**, não o que sai de graça. Ver a nota de
> atualização no topo: a implementação real (EXCALIBUR-KANGAROO369, Fase 6) mede **K≈2**
> com caminhadas Gaudry–Schost; a versão ingênua da simetria travava acima de ~52 bits.

### 1.5 Como os bits altos conhecidos entram
No RCKangaroo você passa `-range B` (largura em bits) e `-start OFFSET` (hex).
A busca é sobre `[start, start + 2^range)`. **Bits altos conhecidos viram parte do
`start`**, então só os `range` bits baixos são varridos → trabalho `K·2^(range/2)`.
**Guarde esta frase — é a chave da seção 5.**

### 1.6 Hardware
RCKangaroo é **CUDA puro, sem caminho de CPU para resolver** (`if (!GpuCnt) exit`).
A CPU só faz bookkeeping de DP, colisão e verificação. ~14,5 GH/s numa RTX 4090.

---

## 2. A matemática EC (de `Ec.cpp`) — e o que dá pra reaproveitar

Boa notícia: **`Ec.cpp`/`Ec.h` do RCKangaroo é código de host (CPU) em C++**, GPLv3.
As primitivas abaixo rodam em CPU e são exatamente o que um Kangaroo de CPU precisa.

| Primitiva | Nome no RCKangaroo | Nota |
|---|---|---|
| Inteiro 256-bit | `EcInt` = `u64 data[5]` | 5 limbs de 64 bits (320b; topo p/ sinal/carry) |
| Primo do corpo | `g_P = FFFF…FEFFFFFC2F` | secp256k1 |
| Mult. modular | `MulModP` | schoolbook + redução rápida com `P_REV = 2^256 mod p` |
| Inversão modular | `InvModP` | **Bernstein–Yang / divsteps** (não Fermat) — rápido |
| Raiz quadrada | `SqrtModP` | `a^((p+1)/4)` (p ≡ 3 mod 4) |
| Soma de pontos | `Ec::AddPoints` | afim: `λ=(y2−y1)/(x2−x1)`, `x3=λ²−x1−x2` |
| Dobro de ponto | `Ec::DoublePoint` | afim: `λ=3x²/2y` |
| `k·G` | `Ec::MultiplyG` | double-and-add MSB-first |
| Descompressão | `CalcY`, `IsValidPoint` | pra ler a pubkey 02/03 |

Observações de performance:
- O gargalo de um salto afim é a **inversão modular**. A técnica padrão é
  **inversão em lote (Montgomery's trick)**: inverter `n` pontos com **1 inversão
  + 3(n−1) multiplicações**. O RCKangaroo faz isso no `KernelA` (pass forward/
  backward por grupos). **Nosso Kangaroo de CPU precisa do mesmo** pra ser decente.
- Sem GLV/endomorfismo no RCKangaroo (procurei `λ/β`: não há). A "simetria" é só a
  negação `±P`. Então não precisamos implementar GLV para paridade com ele.
- Tabela de saltos: `EcJumps1/2/3`, 512 entradas, distância base
  `minjump = 2^(range/2 + 3)`, forçada par; índice do salto = `x[0] % JMP_CNT`.

---

## 3. Brute (volante) vs Kangaroo — paradigmas

| | Brute force (volante atual) | Kangaroo |
|---|---|---|
| Precisa de | endereço / hash160 | **pubkey exposta** |
| O que faz | **enumera** candidatos e testa hash160 | **resolve** o log discreto |
| Custo | `nº de candidatos testados` | `K·√(largura do intervalo)` |
| `--allow` ajuda? | **Sim** (menos candidatos) | depende do byte (seção 5) |
| Saída | o candidato que bateu | **a chave única**, direto |
| Paralelismo | trivial (dividir o volante) | manadas + DP table compartilhada |

O ponto que resolve 90% da confusão: **o volante é um otimizador de ENUMERAÇÃO.**
Ele faz sentido quando o algoritmo é "chuta-e-confere". O Kangaroo **não chuta** —
ele calcula a resposta. Não existe "conjunto de candidatos para filtrar".

---

## 4. Para chaves REAIS não existe estrutura de bytes (aviso honesto)

As chaves dos puzzles são **aleatórias dentro do intervalo**. Não há motivo para a
chave usar poucos bytes distintos. Nos nossos testes (71/75/85/95…) o volante
reduzido só funcionou porque **nós alimentamos os bytes da resposta conhecida**.

Logo: qualquer `--first-allow`/`--allow` apertado num puzzle **não resolvido** é uma
**aposta** sobre a estrutura da chave — e, para uma chave aleatória, a aposta tem
**valor esperado zero** (você ou acerta por sorte, ou exclui a chave e nunca acha).
Isto vale tanto para brute quanto para Kangaroo. Manter em mente na seção 5.

---

## 5. A pergunta central: dá pra juntar volante + Kangaroo?

Resposta rigorosa, **byte por byte**. Seja `N` o nº de bits do puzzle, intervalo de
largura `W = 2^(N−1)`, custo-base do Kangaroo `K·√W`.

### 5.1 `--first-allow` (byte ALTO) → **ajuda** ✅
Restringir o byte alto a `f` valores (de `F` legais; ex.: p95 tem `F=64`,
`legal_first=40..7f`) = a chave está numa **união de `f` sub-intervalos**, cada um
de largura `W/F`. Rodando Kangaroo em cada:
```
custo = f · K·√(W/F) = (f/√F) · K·√W
speedup vs full = √F / f
```
- `f = 1` (byte alto exato): speedup `√F` (p95: `√64 = 8×`).
- Ajuda enquanto **`f < √F`**. Se `f ≥ √F`, dividir é **pior** que um Kangaroo só
  (porque `√` é côncavo: `F·√(W/F) = √F·√W > √W`).
- **É exatamente o `-start`/`-range`**: bits altos conhecidos. O `--first-allow`
  é só uma forma byte-granular de dizer "conheço/chuto os bits altos".

### 5.2 `--allow` no byte BAIXO → **ajuda parcial** ✅
Restringir o byte baixo a `S` valores = saber `k mod 256 ∈ S`. Para cada resíduo
`r ∈ S`: escreve `k = 256k' + r`, e resolve o DLP de `(P − r·G)` na base `256·G`,
num intervalo de `k'` de largura `W/256`:
```
custo = |S| · K·√(W/256) = (|S|/16) · K·√W
```
Ajuda enquanto **`|S| < 16`**. (Generaliza Pohlig–Hellman para o fator 2^8.)

### 5.3 `--allow` nos bytes do MEIO → **não ajuda** ❌
"Byte 5 ∈ S" = bits 40–47 de `k` num conjunto. Isso **não** é um intervalo (bits
altos) nem um resíduo limpo (bits baixos) — é um **dígito do meio**. Não existe
redução algébrica (nem subgrupo, nem módulo, nem encolhimento de intervalo). Para o
Kangaroo aproveitar, o passeio teria que **permanecer dentro do conjunto esparso**,
mas os saltos são **aditivos** e saem do conjunto no primeiro passo. Incompatível.

### 5.4 Síntese
| Restrição | Brute | Kangaroo | Por quê |
|---|---|---|---|
| `--first-allow` (byte alto) | ✅ | ✅ (`√F/f`) | = bits altos → intervalo menor |
| `--allow` byte baixo | ✅ | ✅ parcial (`|S|/16`) | = resíduo mod 256 |
| `--allow` bytes do meio | ✅ | ❌ | dígito do meio, sem álgebra |
| `--allow` geral (todos) | ✅ | ❌ | é filtro de candidatos; Kangaroo não filtra |

**Portanto não existe "um `--search` único" que use o volante inteiro com Kangaroo
para os bytes do meio — é erro de categoria.** O que transfere (bits altos / resíduo
baixo) o Kangaroo **já suporta** como "bits conhecidos". E, para chave aleatória
(seção 4), mesmo esses são apostas de valor esperado zero.

---

## 6. Arquitetura proposta no EXCALIBUR

> ✅ **Construída** em EXCALIBUR-KANGAROO369 (ver nota no topo). O que segue é a proposta
> original; a implementação seguiu esta linha, com a simetria refeita à la Gaudry–Schost.

A junção certa **não** é no algoritmo, é no **despachante** — e é exatamente a sua
intuição ("kangaroo p/ pubkey exposta + brute p/ o resto"):

```
                 ┌───────────────────────────┐
   alvo  ───────▶│   Front-end de VOLANTE     │
 (--puzzle,      │  --first-allow / --allow   │
  --first-allow, │  (parsing, faixa, bytes)   │
  --allow,       └─────────────┬──────────────┘
  --address |                  │  tem pubkey?
  --pubkey) ─────────┬─────────┴─────────┐
                     │ não (só addr)     │ sim (pubkey)
                     ▼                   ▼
            ┌─────────────────┐  ┌───────────────────────────┐
            │  Motor BRUTE    │  │  Motor KANGAROO (CPU)      │
            │  (volante full) │  │  usa first-allow como      │
            │  first-allow +  │  │  bits-altos → -start/-range│
            │  allow + hash160│  │  allow geral = IGNORADO    │
            └─────────────────┘  │  (avisa o usuário)         │
                                 └───────────────────────────┘
```

**Front-end unificado (a parte "nova" e honesta):** um único volante onde
`--first-allow` tem significado nos **dois** motores:
- no **brute**, enumera os bytes altos permitidos;
- no **kangaroo**, vira **conjunto de sub-intervalos** (`-start` por valor de byte
  alto, `-range = N−8`), aproveitando a seção 5.1.

`--allow` (bytes do meio) continua sendo um recurso **exclusivo do brute**; quando o
caminho é Kangaroo, o motor **avisa** que vai ignorar e explica o porquê (seção 5.3)
— nada de iludir o usuário com um filtro que não faz efeito.

### 6.1 Módulos (espelhando o RCKangaroo, mas CPU)
| Módulo EXCALIBUR | Papel | Fonte reaproveitável |
|---|---|---|
| `ec/` (host math) | `EcInt`, `EcPoint`, add/double, `MulModP`, `InvModP`, `MultiplyG`, descompressão | portar `Ec.cpp`/`Ec.h` (GPLv3 — ver licença!) |
| `kangaroo/jumps` | tabela de 512 saltos, `minjump=2^(range/2+3)` | `RCKangaroo.cpp` (setup) |
| `kangaroo/herd` | tame/wild, acumulação de distância, simetria `±P` | `KernelA/B/C` (lógica) |
| `kangaroo/dp` | tabela de DP (hash), `dp_mask`, colisão, recuperação `k=t−w` | `TFastBase`, `Collision_SOTA` |
| `kangaroo/batchinv` | inversão em lote (Montgomery) — **essencial p/ CPU** | `KernelA` (ideia) |
| `dispatch` | escolhe motor por pubkey; traduz `--first-allow` → `-start/-range` | novo |
| `checkpoint` | reaproveita o `--checkpoint`/`--resume` já existente | existente |

### 6.2 Licença (atenção)
RCKangaroo é **GPL-3.0**. Reusar/portar `Ec.cpp` traz obrigações de GPL para o
EXCALIBUR. **Decidir antes de copiar código:** (a) portar e assumir GPL, ou
(b) reimplementar a matemática EC "limpa" a partir das fórmulas (afim add/double,
redução com `2^256 mod p`, inversão divsteps) sem copiar linhas. Recomendo (b) para
manter liberdade de licença — as fórmulas são públicas/padrão.

---

## 7. Viabilidade de hardware (realismo)

- Máquina atual: **i5-3470 (2012, 4 núcleos), GPU Intel integrada, sem CUDA.**
  RCKangaroo **não roda** aqui (exige NVIDIA).
- Um **Kangaroo de CPU nosso** serve para **validar correção em tamanho de
  brinquedo**, não para puzzles reais:

| Range sintético | `ops ≈ 1,15·2^(R/2)` | Tempo @ ~2 M saltos/s (4 threads) |
|---|---|---|
| 40 bits | ~1,2M | ~1 s |
| 48 bits | ~1,9e7 | ~10 s ✅ demo |
| 56 bits | ~3,0e8 | ~2,5 min ✅ |
| 64 bits | ~4,9e9 | ~40 min (teto prático) |

- Puzzles reais com pubkey (120/125/130/135/**140**) continuam **inviáveis** sem
  GPU: mesmo com Kangaroo, 140 ≈ `2^70` saltos ≈ **dezenas de milhares de
  anos-GPU** sozinho (ver tabela da conversa). Isso é problema de **pool + GPUs**,
  não de software nosso.

---

## 8. Roadmap de implementação (quando partir pro código)

> ✅ **Concluído** em EXCALIBUR-KANGAROO369: fases 0–6 feitas e testadas (ctest 8/8),
> puzzle 68 resolvido em dado real. O roadmap abaixo é o registro do plano original.

1. **Fase 0 — matemática EC em CPU (limpa):** `EcInt`, add/double afim, `MulModP`
   (redução `2^256 mod p`), `InvModP` (divsteps), `MultiplyG`, descompressão.
   Teste: bater `MultiplyG(k)` com os puzzles já validados (95…135) — já temos os
   vetores.
2. **Fase 1 — inversão em lote (Montgomery).** Medir saltos/s no i5.
3. **Fase 2 — Kangaroo 2-manadas + DP table (sem simetria ainda).** Validar num
   **pubkey sintético de 40–48 bits** (gerar `k` aleatório, `P=kG`, resolver, conferir).
   Esse é o **"known-answer test" do Kangaroo**.
4. **Fase 3 — simetria `±P`** (chegar perto de `K≈1,15`); medir ganho.
5. **Fase 4 — dispatcher + front-end de volante unificado** (seção 6); traduzir
   `--first-allow` → sub-intervalos (`-start/-range`); avisar que `--allow` do meio
   é ignorado no caminho Kangaroo.
6. **Fase 5 — checkpoint/resume** da tabela de DP (reusar o que já existe).

Critério de sucesso do projeto **nesta máquina**: resolver um pubkey **sintético**
de ~56 bits em poucos minutos, com a chave conferida por `MultiplyG`. Puzzle real =
só com GPU no futuro.

---

## 9. Referências
- RCKangaroo (RetiredC) — GPLv3, CUDA. Método "SOTA v2 Kangaroo with symmetry",
  `K=1,15`. Arquivos-chave: `Ec.cpp/Ec.h` (math host), `RCKangaroo.cpp` (host/CLI/
  colisão), `RCGpuCore.cu` (kernels), `GpuKang.cpp`.
- Inversão modular: Bernstein–Yang "divsteps" (TCHES) — usada no `InvModP`.
- Pollard's Kangaroo / lambda para intervalo; Distinguished Points (van Oorschot &
  Wiener) para colisão com memória sublinear.

---

### Apêndice — por que "ninguém fez igual"
Não é acaso: a parte do volante que é **genuinamente diferente** (o crivo de bytes
do **meio**, `--allow`) é **matematicamente incompatível** com Kangaroo. O que
*seria* a novidade some na análise. O que sobra — bits altos (`--first-allow`) e
resíduo baixo — o Kangaroo já faz como "bits conhecidos". A contribuição real e
defensável do EXCALIBUR é de **engenharia/UX**: **um front-end de volante único que
despacha para o motor certo** e traduz `--first-allow` em sub-intervalos de
Kangaroo automaticamente — unificando dois mundos que hoje vivem em ferramentas
separadas. Isso sim é incomum e útil.
