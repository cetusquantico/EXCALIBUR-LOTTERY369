# EXCALIBUR LOTTERY369 1.1.0

Projeto C++17 independente para planejar volantes nos Bitcoin Puzzles 1–160 e medir sua enumeração com criptografia real em fixtures conhecidas. Cada volante é um produto cartesiano de listas de bytes. O enumerador produz diretamente os escalares permitidos, em ordem crescente ou decrescente.

O planejamento cobre até o puzzle 160 inteiro, com `2^159` candidatos. A execução criptográfica recebe uma chave conhecida ou uma das quatro fixtures distribuídas. Endereços P2PKH e HASH160 servem para conferir a identidade dessas fixtures. Recuperação de segredos desconhecidos por endereço ou hash está fora do escopo desta versão.

Para validar a identidade pública de um dos 160 puzzles e calcular a cobertura do volante, use `--puzzle N --address ENDEREÇO --dry-run`. A CLI verifica o Base58Check e confere o par no catálogo offline. Esse comando termina após apresentar o plano.

## Compilar e testar

Em Linux x86-64 são necessários GCC ou Clang com C++17, CMake 3.16+, pkg-config, Boost headers, OpenSSL com RIPEMD-160 e libsecp256k1. Os testes da CLI usam Python 3. A build Release usa campo 5×52 e detecta SSSE3 em tempo de execução. A instrução AVX2 não é necessária.

```sh
# Pacotes de desenvolvimento em Debian, Kali ou Ubuntu
sudo apt-get install build-essential cmake pkg-config libboost-dev libssl-dev libsecp256k1-dev python3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Para versões do CTest anteriores a 3.20, execute `(cd build && ctest --output-on-failure)`.

Para um binário afinado para a própria máquina, adicione `-DEXLOTTERY_NATIVE=ON` ao configurar (`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEXLOTTERY_NATIVE=ON`). Isso compila com `-march=native` e rende alguns por cento a mais na varredura, mas o executável deixa de ser portável para CPUs mais antigas. O build padrão continua portável e detecta SSSE3 em tempo de execução. LTO foi medido mais lento no laço quente da EC e não é usado.

## Painel de volantes

Em uma sessão gráfica Linux, abra `./build/exlottery --panel`. O painel usa Python 3 com Tkinter (pacote `python3-tk` em Debian/Kali). Escolha o puzzle, marque os valores hexadecimais na grade de bytes e salve o volante com um nome. A seleção inicial do puzzle 55 contém os 32 valores de `grupos/compartilhada.txt`. Com o vínculo ativado, o painel atualiza o primeiro byte usando a interseção da seleção com a faixa legal. Salve o volante para usá-lo pelo nome.

```sh
./build/exlottery --volante "Puzzle 55 · 32 bytes" --dry-run
```

[Ver o painel com os 32 bytes marcados](docs/PAINEL.png)

## Experimentar

O primeiro comando calcula o tamanho e a identidade do volante. O segundo percorre 524.288 candidatos, inclusive a fixture sintética de 160 bits fornecida com a distribuição.

```sh
./build/exlottery --puzzle 160 --first-draw-size 1 --draw-size 3 --draw-seed 369 --dry-run
./build/exlottery --puzzle 160 --first-allow 80 --allow 00,01 \
  --hash160 75a598aae086b74feadf44c306e3a66526d5659c --threads 4
```

A saída humana identifica o tipo de execução no resumo. Quando a sessão confirma a fixture, o bloco `FOUND` mostra pubkey, HASH160 e endereço. Em um terminal interativo, também mostra a HEX, o WIF comprimido e os bytes ativos. Ao redirecionar essa saída, é preciso pedir os campos privados com `--show-private`. A opção `--quiet` mostra apenas o resultado e o resumo. Por padrão, o motor percorre o volante inteiro. Para encerrar ao confirmar a chave de teste, use `--stop-on-hit`.

Para scripts, acrescente `--output kv`. Nesse formato, `total`, `completed` e `session_completed` são contagens inteiras exatas e `fixture_hits` é o total acumulado de ocorrências confirmadas.

## Arquivos

| Conteúdo | Local |
|---|---|
| Opções, exemplos e retomada | [Manual da CLI](docs/USAGE.md) |
| Ordinais, sorteio e cobertura entre máquinas | [Arquitetura](docs/ARCHITECTURE.md) |
| Multiplicação EC, saltos e referência independente | [Núcleo criptográfico](docs/CRYPTO.md) |
| Formato no disco e recuperação após interrupção | [Checkpoints](docs/CHECKPOINT.md) |
| Testes executados e limites da validação | [Relatório de testes](docs/TEST_REPORT.md) |
| Hardware, mediana e variação por cenário | [Benchmarks](docs/BENCHMARKS.md) |
| Chaves de teste, pubkeys e digests | [Fixtures](fixtures/known.tsv) |
| Estrutura das famílias (byte do topo fixo por range) | [Famílias](docs/FAMILIES.md) |
| Valores das transações (prêmio = N e N×9; por que não prevê a chave) | [Funding](docs/FUNDING.md) |
| CPUs modernas, distros e AVX2 | [Compatibilidade](docs/COMPATIBILITY.md) |
| Viabilidade: brute vs volante vs kangaroo (e a régua da pubkey) | [Viabilidade](docs/FEASIBILITY.md) |
| Código | [CLI](src/main.cpp), [motor](src/engine.cpp), [interfaces](include/exlottery), [núcleo copiado e licença](vendor/ex369) |

A build usa a cópia em `vendor/ex369`. Todos os arquivos necessários para recompilar acompanham este projeto, exceto as dependências de sistema listadas acima.

## Créditos

O núcleo criptográfico (copiado do excalibur369, em `vendor/ex369`) deriva do
[keyhunt](https://github.com/albertobsd/keyhunt), de Luis Alberto (albertobsd), sob a
licença MIT — Copyright (c) 2020 Luis Alberto. O libsecp256k1 em `vendor/` é MIT (Bitcoin
Core), com a licença incluída.

## Apoie

Trabalho de tempo livre, bancado por mim. O próximo passo grande — uma versão GPU — precisa
de hardware. Se isto te for útil e quiser ajudar, doações em Bitcoin são bem-vindas, sem
promessa de retorno:

`bc1qhsq406jt6ghkzmhpnqe4r2s639t6mtrhj6zx03`
