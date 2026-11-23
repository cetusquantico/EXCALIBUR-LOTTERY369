# Criptografia e verificação de fixtures

O binário mede a derivação de uma chave pública comprimida e de seu HASH160 para cada candidato confirmado. A fixture inclui obrigatoriamente o escalar conhecido. Ao passar pela posição dessa chave, o verificador confronta os 20 bytes do digest com o HASH160 previamente derivado e repete a derivação com libsecp256k1 e OpenSSL. Uma falha aborta o lote. A contagem de ocorrências usa a identidade do escalar conhecido.

## Componentes

`vendor/ex369` contém cópias dos módulos de corpo finito, grupo, identificação de CPU, SHA-256, RIPEMD-160 e SSSE3 de quatro vias de `/root/excalibur369/src`. A licença MIT acompanha esses arquivos. O diretório original não participa do build. Para conferir a integridade da cópia, execute `(cd vendor/ex369 && sha256sum -c SHA256SUMS)`.

O parâmetro de CMake `EXLOTTERY_FIELD` escolhe `5x52` (padrão) ou `4x64`. Os dois usam aritmética de 128 bits apenas para produtos e transporte entre limbs do corpo finito. Os ordinais do projeto têm sua própria representação de 256 bits.

`reference` usa a tabela de base fixa sobre G para calcular cada ponto a partir do escalar completo. `jump` calcula o primeiro ponto da sequência do worker com o mesmo método. Para os demais, subtrai os escalares como inteiros de quatro limbs e armazena os pontos correspondentes a diferenças frequentes em uma tabela local. O sinal é parte da chave da tabela. O cálculo abrange o escalar inteiro, inclusive carries entre bytes e transições que cruzam a fronteira de um segmento. Uma sequência que repete um escalar conserva o ponto atual. O cache é limpo ao precisar inserir a 513ª diferença distinta.

O backend incremental acumula os pontos em coordenadas Jacobianas e normaliza blocos de até 128 pontos usando inversão em lote. Serializa a paridade de Y seguida dos 32 bytes de X antes de aplicar SHA-256 e RIPEMD-160. O valor de `--batch` controla o lote externo. O código SSSE3 recebe quatro chaves públicas por chamada, e a cauda de um a três elementos passa pelo HASH160 escalar. Cada chamada opera somente sobre registros válidos, inclusive no último bloco.

`--simd auto` seleciona SSSE3 se compilado e disponível via CPUID. `scalar` força uma via e `sse` exige o backend de quatro vias. CMake isola o código SSSE3 em sua própria unidade de tradução. `-DEXLOTTERY_SSE=OFF` permite construir exclusivamente o backend escalar. Nenhum backend requer AVX2.

## Referência e entradas

A referência independente usa libsecp256k1 para multiplicação por G e serialização SEC comprimida, e a API EVP do OpenSSL para SHA-256 e RIPEMD-160. O binário requer ambos. O CMake prefere libsecp256k1 estática quando o pacote de desenvolvimento inclui o arquivo. Entradas nulas e escalares iguais ou superiores à ordem do grupo são inválidos. O cálculo EC do motor usa os módulos vendorizados.

O parser hexadecimal exige exatamente quarenta dígitos. O decodificador P2PKH aceita Base58Check mainnet com versão zero e valida o alfabeto, comprimento de 25 bytes incluindo checksum, quantidade de zeros iniciais, versão e quatro bytes de SHA-256 duplo. Essa operação ocorre fora dos lotes.

O vetor público do puzzle 39 vem de `tests/test_public_puzzle39.py` do projeto original. Seu escalar é `4b5f8303e9` e seu endereço comprimido é `122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8`. O teste C++ decodifica o endereço e o confronta com a derivação independente. O escalar 1 tem HASH160 comprimido `751e76e8199196d454941c45d1b3a323f1433bd6`.

## Testes diferenciais

`tests/crypto.cpp` percorre a mesma sequência com referência, saltos, hash escalar e seleção automática. Também exige explicitamente o backend SSE quando disponível. A sequência inclui duplicatas, carry, reversão, deltas de 160 bits, mais de 512 diferenças, caudas SIMD e limites de lote. Para cada lote, o teste compara o número de ocorrências da fixture e a soma de controle de todos os digests com derivações independentes. Uma soma módulo 2⁶⁴ permite comparação de cobertura entre execuções, mas tem colisões. Em testes adicionais cada uma das quatro posições SIMD atua como fixture e tem seu digest verificado integralmente. Um teste cruza o ordinal 2¹⁸ do volante binário do puzzle 160 nas duas direções. A passagem seguinte ao ordinal 2¹⁸−1 zera 18 bytes finais. O teste verifica integralmente cada digest em torno dessa transição. A obtenção do delta usa sempre quatro limbs, independentemente do comprimento do carry. A primeira ocorrência de um delta paga o custo de multiplicação por G e as subsequentes consultam a tabela local.

Este código processa escalares públicos de teste. Os campos, saltos e índices de tabela usam operações dependentes dos dados.
