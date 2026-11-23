# Relatório técnico · 21 de novembro de 2026

## Revisão 1.0.3 · Identidade pública e origem de FOUND

O executável aceita o número, o endereço e as listas sem HEX para planejar um volante. O processamento criptográfico ainda requer uma fixture conhecida internamente. O teste A solicitado, em que apenas o teste externo conhece o escalar, continua pendente.

| Etapa inspecionada | Comportamento observado |
|---|---|
| Inicialização | A função `known_key` lê a HEX explícita, recupera o campo do checkpoint ou procura uma das quatro chaves embutidas. O comando com o endereço público do puzzle 39 localiza a fixture resolvida por esse último caminho. [CLI](../src/main.cpp) |
| Geração | O agendador reserva ordinais. Os workers usam o volante para converter o primeiro ordinal e avançar em cada segmento. A auditoria do checkpoint consulta separadamente a posição da fixture para conferir a coerência dos acertos gravados. [Motor](../src/engine.cpp) |
| Verificação | Cada candidato passa por secp256k1, compressão da pubkey e HASH160. O verificador de fixture exige igualdade com o escalar conhecido antes de conferir os 20 bytes do digest e repetir a derivação pela referência. A decodificação Base58Check da entrada ocorre na CLI antes dos workers. [Criptografia](../src/crypto.cpp) |
| Resultado | O resumo identifica a validação de fixture. A saída KV rotula a execução com `execution_kind=known_fixture`. [Apresentação](../src/presentation.cpp) |

| Aceitação | Resultado desta revisão |
|---|---|
| A · Positivo com chave conhecida só pelo teste | Pendente. O puzzle 39 obteve um acerto em 256 candidatos sem a opção `--known-key`, porém o binário contém sua solução. |
| B · Volante esgotado | Na execução com a fixture do puzzle 39, a troca de `e9` por `e8` produziu `completed=256`, `session_completed=256`, `fixture_hits=0`, `stop_reason=exhausted` e código 0. O cenário comprova o controle negativo da fixture. |
| C · Validação do catálogo | O teste offline reconstruiu os 160 endereços e aceitou todos. Rejeitou 160 trocas entre puzzles. No caso 71 verificou ainda checksum inválido e rejeição antes de criar um checkpoint novo ou seu lock. [Teste](../tests/public_catalog.py) |
| D · Regressões | Passaram as nove suítes anteriores e a nova suíte `public_catalog`. Foram exercitados campos 5×52 e 4×64, backends de referência e saltos, HASH160 escalar e SSSE3, interrupção e retomada. Mudar um bit em cada uma das 20 posições do HASH160 fornecido à fixture causou rejeição nas 20 tentativas de inicialização. |

O catálogo embarcado usa a saída de índice `N−1` da transação de financiamento do desafio. O teste reconstrói as identidades a partir dos bytes da transação, verifica seu TXID, o script P2PKH e os valores das saídas. A verificação funciona offline e independe da lista de endereços em C++. O catálogo identifica endereços e não acompanha o saldo ou a situação de resolução de cada puzzle. [Transação de financiamento](https://blockstream.info/tx/08389f34c98c606322740c0be6a7125d9860bb8d5cb182c02f98461e5fa6cd15) · [Vetor bruto](../fixtures/puzzle_funding_tx.hex)

No i5-3470, o GCC 16.2.0 em Release completou 10/10 em 8,96 s. O Clang 21.1.8 com ASan e UBSan completou 10/10 em 21,47 s. São tempos de suíte. As taxas da versão 1.0.2 abaixo pertencem ao ensaio histórico. [Log GCC](../tests/results/1.0.3-gcc-release.log) · [Log Clang](../tests/results/1.0.3-clang-sanitizers.log)

### Reprodução local

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ -DEXLOTTERY_SANITIZE=ON -DEXLOTTERY_FIELD=4x64
cmake --build build-san -j4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-san --output-on-failure
(cd vendor/ex369 && sha256sum -c SHA256SUMS)
```

### Preparação para distribuição

Os 19 arquivos de código copiados de `EXCALIBUR369/src` e o arquivo de licença conferem byte a byte com o original local. O manifesto verificou os 20 itens. A cópia inclui o aviso MIT do original. O projeto liga a libsecp256k1 de referência, estática nesta build, cujo aviso do pacote acompanha os fontes. O pacote local do OpenSSL 3 identifica Apache-2.0, e o do Boost identifica BSL-1.0 para o código principal. O projeto LOTTERY369 ainda precisa de uma licença própria para o código novo antes da publicação como open source. [Licença do núcleo](../vendor/ex369/LICENSE) · [Avisos da libsecp256k1](../vendor/LICENSE-libsecp256k1.txt)

O workflow de CI fixa a imagem Ubuntu 24.04 amd64 por digest, a ação de checkout por commit e os pacotes pelo snapshot assinado do Ubuntu. Há quatro combinações de compilador, campo, otimização e sanitizers. Em um contêiner Podman com a imagem e o snapshot da CI, a instalação das dependências, a build GCC Release e as dez suítes passaram. O CTest levou 6,41 s. A matriz hospedada no GitHub ainda está pendente. [Workflow](../.github/workflows/ci.yml) · [Log do contêiner](../tests/results/1.0.3-ubuntu-snapshot.log)

Arquivos desta revisão incluem a CLI e apresentação (`src/main.cpp`, `src/presentation.cpp`, `include/exlottery/engine.hpp`), catálogo e referência (`src/public_puzzles.cpp`, `include/exlottery/catalog.hpp`, `fixtures/puzzle_funding_tx.hex`), testes (`tests/cli.py`, `tests/public_catalog.py` e os logs 1.0.3), build e CI (`CMakeLists.txt`, `.github/workflows/ci.yml`, `.gitignore`) e documentação (`README.md`, `CHANGELOG.md`, `docs/USAGE.md` e este relatório).

## Revisão 1.0.2 · Varredura até a confirmação da chave conhecida

No puzzle 55, a execução Release utilizou os dois arquivos em `grupos/`, quatro workers com afinidade, modo sequencial, backend de saltos, SSSE3 e a opção `--stop-on-hit`. Começou no ordinal zero, com checkpoint novo, e confirmou a fixture que o usuário havia fornecido. O volante tinha 7.516.192.768 combinações.

| Medição desta execução | Resultado |
|---|---:|
| Ocorrências verificadas | 1 |
| Posição da fixture (primeira posição = 1) | 855.144.526 |
| Candidatos processados e confirmados | 855.145.216 |
| Tempo do motor | 282,163 s |
| Taxa observada | 3,031 milhões de candidatos/s |

O checkpoint final continha um único intervalo semiaberto `[0, 855145216)`. O total inclui os blocos já atribuídos aos workers no momento da confirmação. A saída foi comparada com os valores esperados para HEX, WIF comprimido, pubkey comprimida, HASH160 e endereço. Os testes de regressão também rodaram nesta máquina durante a varredura. A taxa acima corresponde a esta execução específica.

O dry-run com todos os 64 primeiros bytes legais e a mesma lista compartilhada retornou 68.719.476.736 candidatos. As nove suítes incluem bordas, controle negativo e retomada da varredura após parada por acerto. [GCC Release](../tests/results/1.0.2-gcc-release.log) · [Clang ASan e UBSan](../tests/results/1.0.2-clang-sanitizers.log)

## Revisão 1.0.1 · Painel e apresentação da chave conhecida

O painel foi exercitado em X11 virtual. O teste abriu a grade de 256 valores, validou o volante de 32 bytes pelo executável, alternou entre seleção vinculada e manual, ampliou a lista até 256 bytes, exportou e importou arquivos e reabriu uma seleção nomeada. O dry-run com os arquivos da entrega retornou `7 × 32⁶ = 7.516.192.768` combinações. O identificador do volante foi `face2df889c0a3a1ba7f8c70f5541d2f1d7ab8d60bb833a8aded9e0c03e52a9e`.

O teste de saída comparou a chave, WIF comprimido, pubkey, HASH160, endereço e bytes ativos da **solução pública já conhecida** do puzzle 55 com valores esperados, usando um volante separado de 46.656 combinações. Também verificou saída negativa, retomada e compatibilidade do formato KV. As oito suítes passaram em [GCC Release](../tests/results/1.0.1-gcc-release.log) e em [Clang com ASan e UBSan](../tests/results/1.0.1-clang-sanitizers.log). As medições de desempenho abaixo correspondem ao executável 1.0.0 identificado no relatório de benchmarks.

A comparação com o manifesto histórico da versão 1.0.0 encontrou diferença apenas em `/root/excalibur369/p71.json`, um checkpoint de execução da árvore original. As alterações de implementação desta revisão foram feitas em `EXCALIBUR-LOTTERY369`.

## Resultado da versão 1.0.0

Sete suítes passaram na build Release do GCC. As mesmas sete passaram sob AddressSanitizer e UndefinedBehaviorSanitizer com Clang e o campo 4×64. O motor completou os quatro volantes solicitados para o puzzle 39 e os casos positivo e negativo de 524.288 candidatos para o puzzle 160. O domínio matemático completo de `2^159` foi validado por limites, contagem, conversões e acesso ordinal.

A entrega implementa o planejamento geral e a validação de chaves conhecidas. A recuperação por HASH160 ou endereço arbitrário de chave desconhecida permanece fora da implementação. O resultado positivo do puzzle 160 pertence à fixture sintética distribuída.

## Ambiente

Linux x86-64, kernel 7.1.5+kali-amd64, Intel Core i5-3470 a 3,20 GHz, quatro núcleos e quatro threads de hardware, com SSSE3. GCC 16.2.0, Clang 21.1.8, CMake 4.3.4, Boost 1.92, libsecp256k1 0.7.1 e OpenSSL 3.6.4. O pacote de desenvolvimento Boost foi instalado para a compilação. A libsecp256k1 foi vinculada estaticamente nesta máquina. OpenSSL usa a biblioteca compartilhada do sistema.

| Build local | Configuração | Resultado |
|---|---|---|
| GCC Release | 5×52, SSSE3 e escalar | 7/7 em 5,39 s na última rodada. [Log](../tests/results/gcc-release.log) |
| Clang RelWithDebInfo | 4×64, SSSE3 e escalar, ASan+UBSan | 7/7 em 11,08 s. [Log](../tests/results/clang-sanitizers.log) |
| GCC Release a partir do pacote extraído | 5×52, somente escalar com `-DEXLOTTERY_SSE=OFF` | 7/7 em 4,49 s. [Log](../tests/results/gcc-package-scalar.log) |

O executável pré-compilado, extraído em diretório temporário, também percorreu 4.096 candidatos e verificou a fixture do puzzle 39. A compilação escalar descrita acima foi feita a partir dos fontes do pacote em diretório de build vazio.

A matriz de CI inclui GCC e Clang com as duas representações de campo e uma build exclusivamente escalar. Esses jobs estão definidos no [workflow](../.github/workflows/ci.yml). A execução remota depende do repositório que receber a distribuição.

## Evidência por componente

| Suíte | Verificação executada |
|---|---|
| `math` | Todos os puzzles, 256 tamanhos de lista compartilhada, extremos e ordinais intermediários, contagem por inteiro de precisão arbitrária, overflow e máximo de 256 bits, 1.024 conversões com entradas pseudoaleatórias. Um produto cartesiano independente verifica a ordem completa, o carry e o retrocesso. |
| `crypto` | HASH160 e pubkeys derivadas por libsecp256k1/OpenSSL como oráculo. Cobertura de referência e saltos, scalar/SSSE3, caudas de lote, carry de 18 bytes, reversão e renovação da tabela de transições. Checksums diferenciais por lote e comparação integral dos digests das fixtures. Validação positiva e negativa de Base58Check. |
| `scheduler` | Retirada e confirmação de segmentos, extremos de 256 bits, 1/2/4 threads, lacunas, snapshots, exclusão de gravadores e arquivos inválidos. |
| `cli` | Fixtures resolvidas 1 e 39, chave sintética customizada, arquivos de lista, testes negativos e entradas rejeitadas. Cada membro de um pequeno volante é plantado individualmente em três configurações para verificar a cobertura de ponta a ponta. |
| `dry_run_all` | Os 160 puzzles foram chamados pela CLI. Limites, quantidade de bytes, primeiro byte legal, contagens e ETA foram confrontados com inteiros do Python. O dry-run com checkpoint não criou arquivos. |
| `fixture160` | 524.288 candidatos confirmados no caso positivo, exatamente uma ocorrência, e zero ocorrências no negativo de igual tamanho. Após o último segmento, o checkpoint continha `[0,524288)`. Retomar o arquivo completo processou zero candidatos adicionais. |
| `restart` | O teste aguardou um checkpoint periódico com progresso, interrompeu com SIGTERM em reverso e com SIGKILL em sequencial e concluiu ambos com outra quantidade de workers e outro tamanho de lote. Verificou o total confirmado e a ocorrência única. |

Também derivei as quatro pubkeys comprimidas e seus digests fora do executável, com `cryptography` para secp256k1 e `hashlib` para SHA-256/RIPEMD-160. Os resultados coincidiram com todos os registros da [tabela de fixtures](../fixtures/known.tsv). O puzzle 39 usa a chave e o endereço registrados no teste público do projeto original. O caso sintético de 160 bits usa o HASH160 `75a598aae086b74feadf44c306e3a66526d5659c`.

## Quatro volantes do puzzle 39

Cada execução usou a fixture sintética `4001020304`, a primeira lista `40` e a lista compartilhada começando em `00`. O total confirmado coincidiu com `m^4` em todas as execuções. Os dados desta tabela são de **uma execução funcional** por tamanho. As nove repetições e suas medianas estão no [relatório de benchmarks](BENCHMARKS.md).

| Tamanho m | Confirmados | Ocorrências | Segundos |
|---:|---:|---:|---:|
| 8 | 4.096 | 1 | 0,010914 |
| 16 | 65.536 | 1 | 0,023237 |
| 32 | 1.048.576 | 1 | 0,250115 |
| 64 | 16.777.216 | 1 | 3,804619 |

A matriz de desempenho completou 180 processos, nove repetições para cada um dos 20 cenários. Ao todo, o CSV registra 164.413.440 candidatos confirmados, com uma ocorrência da fixture por processo. Cada linha inclui tamanho, backends, threads, segmento, segundos do motor e segundos de parede. [Dados brutos](../bench/results.csv)

## Reproduzir

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
python3 tests/cli.py build/exlottery large39
python3 bench/run.py --binary build/exlottery
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ -DEXLOTTERY_FIELD=4x64 -DEXLOTTERY_SANITIZE=ON
cmake --build build-san -j"$(nproc)"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-san --output-on-failure
```

## Integridade e limites da entrega

Antes e depois da implementação, foram comparados o conjunto de caminhos de arquivos regulares do original e seu conteúdo por SHA-256. Os manifestos dos 1.669 arquivos em `/root/excalibur369` coincidiram. O SHA-256 do manifesto ordenado foi `8601478faf0906b338ead25b0f8af734f28e5e5a62c67130ef744a0a81fa630c`. Os arquivos criptográficos copiados têm um [manifesto próprio](../vendor/ex369/SHA256SUMS), relativo ao diretório da cópia.

A distribuição mede o pipeline de validação de fixtures e conserva o custo de inicialização dos workers nas taxas. A ETA de dry-run usa a taxa configurada. Uma máquina diferente, outras listas ou outro tamanho de lote podem produzir resultados diferentes. O projeto implementa agendamento local sequencial e reverso. A partição entre máquinas está descrita na arquitetura para desenvolvimento futuro.
