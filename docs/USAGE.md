# Uso

Execute os exemplos a partir da raiz do projeto. O executável exibe um resumo com `--help`.

## Selecionar um volante no painel

No ambiente gráfico, execute `./build/exlottery --panel`. Se o Python da distribuição estiver sem Tkinter, instale `python3-tk`. A CLI também funciona em terminal sem ambiente gráfico.

1. Para repetir o puzzle 55, use o exemplo que aparece ao abrir o painel. Para outro volante, use **Novo** e escolha o puzzle.
2. Em **Primeiro byte**, marque um ou mais valores legais. Em **Bytes compartilhados**, marque os valores que cada uma das demais posições pode assumir. Também é possível colar os bytes no campo de texto e usar **Aplicar lista**.
3. Confira o número de candidatos e os extremos. **Validar volante** pede um dry-run à CLI. **Copiar argumentos** copia as opções de puzzle e listas para o terminal.
4. O vínculo do primeiro byte aplica a interseção entre os valores compartilhados e os limites legais. Pode ser desmarcado para editar uma lista inicial independente. O menu **Lista .txt** importa e exporta a lista da aba atual.
5. Dê um nome e clique em **Salvar**. Para reutilizar uma seleção, clique no nome em **Volantes salvos**. **Excluir** remove o volante escolhido após confirmação.

O nome identifica o puzzle e as duas listas. A opção `--volante` carrega esses três valores no terminal. Se informar também `--puzzle`, seu valor deve coincidir com o puzzle salvo. As opções de lista e sorteio pertencem ao modo de montagem manual. Para retomar um trabalho salvo, use `--resume` com o checkpoint daquela execução. O catálogo do painel fica em `$XDG_CONFIG_HOME/exlottery/volantes.json` quando `XDG_CONFIG_HOME` aponta para um diretório absoluto. O caminho padrão é `~/.config/exlottery/volantes.json`.

O volante inicial **Puzzle 55 · 32 bytes** usa sete valores para o primeiro byte e 32 para as seis posições restantes. Para inspecionar os arquivos fornecidos:

```sh
./build/exlottery --puzzle 55 --first-allow-file grupos/primeiro.txt \
  --allow-file grupos/compartilhada.txt --dry-run
```

Mantendo somente `--allow-file grupos/compartilhada.txt`, o dry-run calcula o volante com todos os 64 primeiros bytes legais de `40` a `7f` e as mesmas 32 opções nas seis posições seguintes. O total é `64 × 32⁶ = 68.719.476.736`. Com os dois arquivos do exemplo anterior, o total é `7 × 32⁶ = 7.516.192.768`.

## Montar um volante

Escolha um puzzle de 1 a 160. A primeira lista controla o byte ativo mais significativo. A segunda se aplica igualmente às demais posições. Cada token hexadecimal tem exatamente dois dígitos. Vírgulas, espaços e quebras de linha podem separar tokens. Duplicatas, posições vazias e primeiros bytes fora da faixa causam erro. Uma lista omitida assume todos os valores legais daquela posição.

```sh
./build/exlottery --puzzle 39 --first-allow 40 \
  --allow-file fixtures/allowlist.txt --fixture synthetic-39 --threads 4
./build/exlottery --puzzle 160 --first-draw-size 1 --draw-size 3 \
  --draw-seed 369 --dry-run --rate 2000000
```

Use `--first-allow-file` e `--allow-file` para carregar arquivos independentes. Cada lista pode ter uma fonte diferente. Um sorteio exige seed inteira entre 0 e `2^64−1`. Valores e tamanhos fora dos limites causam erro.

No exemplo de sorteio, a primeira lista contém `87` e a compartilhada contém `63,70,aa`. Há exatamente 1.162.261.467 candidatos. No formato `--output kv`, o campo `percent_approx` é a aproximação decimal de `100 × total/original_space`. A ETA arredonda para cima em segundos. A taxa assumida padrão é 1.000.000 cand/s, configurável com `--rate`. O dry-run funciona sem chave de teste e sem gravar checkpoint.

## Validação criptográfica

Para conferir o endereço público e o tamanho do volante do puzzle 71 com os dois arquivos distribuídos, execute o planejamento offline.

```sh
./build/exlottery --puzzle 71 --address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
  --first-allow-file grupos/primeiro.txt --allow-file grupos/compartilhada.txt --dry-run
```

O catálogo vincula o puzzle ao endereço da saída correspondente na transação de financiamento. Um par incompatível ou um checksum inválido encerra o comando com código 2. O plano com um par válido contém `public_address` em `--output kv`. A execução criptográfica de uma fixture contém `execution_kind=known_fixture`. O caso abaixo do puzzle 39 resolve internamente uma fixture que inclui a chave conhecida.

```sh
./build/exlottery --puzzle 39 --first-allow 4b --allow 03,5f,83,e9 \
  --address 122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8 --threads 2 --mode reverse

./build/exlottery --puzzle 160 --first-allow 80 --allow 00,01 \
  --hash160 75a598aae086b74feadf44c306e3a66526d5659c --threads 4
```

O primeiro exemplo valida o puzzle 39 resolvido em 256 candidatos. O segundo usa a chave conhecida `8000010001000100010001000100010001000100`. Para reproduzir o teste negativo no segundo exemplo, use `--first-allow 81`. A chave fica fora do volante, que continua com 524.288 candidatos.

A opção `--fixture` aceita `solved-1`, `solved-39`, `synthetic-39` e `synthetic-160`. Outra chave de teste pode ser fornecida com `--known-key` e 1 a 64 dígitos hexadecimais sem prefixo. Ela deve pertencer à faixa do puzzle. A opção `--hash160` ou `--address` confere o digest derivado independentemente dela. Se apenas o identificador for fornecido, a CLI procura a fixture correspondente no catálogo embutido. O endereço aceito é P2PKH mainnet em Base58Check.

O bloco `FOUND` confirma que a enumeração visitou a chave de teste e que a derivação independente coincidiu. Para `--known-key`, é obrigatório fornecer a HEX. Ao receber somente um endereço ou HASH160 fora das fixtures do puzzle, a CLI termina com código 2 antes de processar candidatos.

### Conferir a saída do puzzle 55

Para conferir a apresentação com a solução pública do puzzle 55, use o volante pequeno de seis valores compartilhados.

```sh
./build/exlottery --puzzle 55 --first-allow 6a --allow 14,1f,67,9b,be,e1 \
  --known-key 6abe1f9b67e114 --address 1LzhS3k3e9Ub8i2W1V8xQFdB8n2MYCHPCa --threads 4 --quiet
```

A chave tem sete bytes ativos, em ordem big-endian, `6a be 1f 9b 67 e1 14`. O primeiro byte do volante é `6a`. A lista compartilhada ordenada é `14,1f,67,9b,be,e1`. Cada uma das seis posições seguintes pode conter qualquer um desses seis valores, somando 46.656 candidatos.

O bloco `FOUND` aparece ao concluir a sessão que confirmou a chave. O campo `private` preserva os 64 dígitos hexadecimais do escalar e o campo `bytes` separa os sete bytes ativos na ordem da chave. O endereço e o WIF usam Base58Check. O resumo `=== run ===` informa os candidatos concluídos e a taxa da sessão. Ao retomar uma execução com um acerto anterior, o resumo identifica a confirmação já registrada no checkpoint.

O comando abaixo exibe um cabeçalho antes da execução e as estatísticas ao final. Para integrações que leem linhas `campo=valor`, substitua `human` por `kv`.

```sh
./build/exlottery --puzzle 55 --first-allow 6a --allow 14,1f,67,9b,be,e1 \
  --known-key 6abe1f9b67e114 --output human
```

### Encerrar ao confirmar a chave

```sh
./build/exlottery --puzzle 39 --first-allow 40 --allow-file fixtures/allowlist.txt \
  --fixture synthetic-39 --threads 4 --mode sequential --stop-on-hit
```

A varredura começa no primeiro ordinal pendente. Ao confirmar a chave conhecida, o agendador encerra a distribuição de trabalho. Cada worker conclui o bloco que já recebeu. Neste modo, a reserva tem até `min(batch, 256)` candidatos. Em uma execução nova e sequencial que termina com acerto, o conjunto confirmado vai do ordinal zero até o último bloco processado. A quantidade concluída pode ultrapassar a posição da chave, especialmente com vários workers.

Um acerto confirmado retorna código 0. Se a chave estiver fora do volante, o programa percorre todos os ordinais e termina com zero acertos. O formato `--output kv` inclui `stop_reason` com os valores `confirmed_hit`, `exhausted` ou `interrupted` quando a opção está ativa.

Para manter a parada no acerto ao retomar um checkpoint, use `--resume ARQ --stop-on-hit`. Se o arquivo já tiver um acerto, os novos candidatos processados serão zero. Use `--resume ARQ` sozinho para completar o restante do volante.

## Busca real (chave desconhecida)

Diferente da validação de fixture, o modo `--search` **não recebe a chave privada**. Ele recebe apenas o alvo — um endereço P2PKH ou um HASH160 — e varre o volante calculando o HASH160 de cada candidato. Quando um candidato reproduz o alvo, a chave é recuperada, re-derivada de forma independente pela libsecp256k1 e gravada. É o modo para procurar a HEX desconhecida de endereços de teste gerados no bitaddress dentro da faixa e dos bytes escolhidos.

```sh
# Alvo único por endereço, parando no primeiro acerto, 4 threads
./build/exlottery --puzzle 39 --first-allow 4b --allow 03,5f,83,e9 \
  --search --address 122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8 --stop-on-hit --threads 4

# Alvo por HASH160, gravando os acertos em um arquivo próprio
./build/exlottery --puzzle 39 --first-allow 4b --allow 03,5f,83,e9 \
  --search --hash160 0b304f2a79a027270276533fe1ed4eff30910876 --found achados.txt

# Vários alvos de uma vez (um por linha; endereço ou HASH160; `#` é comentário)
./build/exlottery --puzzle 39 --allow-file fixtures/allowlist.txt \
  --search --targets alvos.txt --threads 8
```

Cada acerto imprime `private`, `wif`, `pubkey`, `hash160` e `address` e é acrescentado ao arquivo de `--found` (padrão `found.txt`) no momento em que ocorre, de modo que uma interrupção não o perde. No formato `--output kv`, os campos são `found_private`, `found_wif`, `found_pubkey`, `found_hash160`, `found_address` e `matches`. Alvos repetidos (por exemplo, um endereço e seu HASH160) são deduplicados.

A busca aceita as mesmas opções de execução das fixtures: `--threads`, `--mode sequential|reverse`, `--backend`, `--simd`, `--affinity`, `--status` e `--stop-on-hit`. Com `--checkpoint ARQ` o progresso e os próprios alvos ficam gravados no checkpoint; `--resume ARQ` retoma a busca sem precisar reinformar os alvos. Fornecer na retomada um alvo diferente do salvo encerra com código 2 (`checkpoint configuration mismatch`). Um acerto confirmado retorna código 0; se nenhum alvo estiver no volante, a varredura termina completa com `matches=0`, também em código 0.

## Opções de execução

| Opção | Efeito e padrão |
|---|---|
| `--output human\|kv` | Blocos legíveis por padrão. Use `kv` para scripts que leem linhas `campo=valor`. |
| `--show-private` | Inclui HEX, WIF e bytes ativos da fixture no bloco FOUND quando a saída humana vai para arquivo ou pipe. Esses campos aparecem por padrão em terminal interativo. |
| `--stop-on-hit` | Encerra as reservas ao confirmar a chave de teste. Padrão: varredura completa. |
| `--quiet` | Na execução humana, oculta o plano inicial e o progresso periódico. Mantém o resultado e o resumo. O dry-run sempre exibe o plano. Em `kv`, oculta apenas o progresso periódico. |
| `--volante NOME` | Carrega o puzzle e as duas listas do catálogo do painel. |
| `--threads N` | Workers, de 1 a 1024. Padrão 1. |
| `--batch N` | Máximo de ordinais por reserva. Padrão 4096. Os buffers criptográficos têm tamanho fixo. |
| `--mode sequential\|reverse` | Ordem das reservas e da enumeração dentro de cada segmento. Padrão sequencial. Com vários workers a ordem global de conclusão pode variar. |
| `--backend reference\|jump` | Multiplicação independente ou atualização por deltas. Padrão `jump`. |
| `--simd scalar\|sse\|auto` | Implementação de HASH160. Padrão `auto`. `sse` exige SSSE3. |
| `--affinity on\|off` | Distribui os workers pelos CPUs disponíveis ao processo. Padrão `off`. |
| `--status S` | Progresso confirmado em stderr a cada S segundos. Padrão 0, desativado. |
| `--checkpoint ARQ` | Cria um arquivo novo. Recusa sobrescrever uma execução existente. |
| `--resume ARQ` | Carrega a configuração e continua no mesmo arquivo. |
| `--checkpoint-interval S` | Intervalo positivo entre snapshots, em segundos. Padrão 30. |

## Interromper e retomar

```sh
./build/exlottery --puzzle 160 --first-allow 80 --allow 00,01 \
  --fixture synthetic-160 --checkpoint volante.json --checkpoint-interval 1
./build/exlottery --resume volante.json --threads 4
```

No primeiro comando, Ctrl+C solicita a gravação dos segmentos inteiramente confirmados. Uma retomada permite alterar opções de desempenho, incluindo threads, backend e tamanho de segmento. Requer o mesmo puzzle, volante, origem do sorteio, sentido e fixture. O checkpoint contém a chave conhecida de teste e os parâmetros necessários para reconstruir o volante, inclusive quando a lista inicial veio de arquivo.

O checkpoint guarda o escalar em texto claro e o sistema cria o arquivo com permissão `0600`. Para mantê-lo fora do controle de versão, crie o diretório `checkpoints/` e use um caminho dentro dele. Uma sessão de terminal gravada também pode conter os campos privados.

Código de saída 0 indica cobertura completa, acerto com `--stop-on-hit` ou dry-run bem-sucedido. Um teste negativo concluído também retorna 0 e mostra zero acertos. No formato `--output kv`, o campo correspondente é `fixture_hits=0`. Código 2 indica erro. Uma interrupção tratada com trabalho pendente retorna 130. A taxa usa os candidatos confirmados na sessão corrente e o tempo do motor, incluindo criação e encerramento de workers. A escrita final do checkpoint ocorre fora desse tempo.

## Repetir medições e testes instrumentados

```sh
python3 tests/cli.py build/exlottery large39
python3 bench/run.py --binary build/exlottery
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ -DEXLOTTERY_SANITIZE=ON -DEXLOTTERY_FIELD=4x64
cmake --build build-san -j"$(nproc)"
ctest --test-dir build-san --output-on-failure
```

A matriz de benchmark usa SSSE3. Para uma build escalar, configure `-DEXLOTTERY_SSE=OFF` e execute os testes CTest. Os [resultados das medições](BENCHMARKS.md) identificam a configuração e o binário utilizados.
