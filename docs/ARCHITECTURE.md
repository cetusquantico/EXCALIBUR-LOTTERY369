# Arquitetura do EXCALIBUR LOTTERY369

## Representação e limites

O planejamento usa `boost::multiprecision::checked_uint256_t`. Uma operação aritmética cujo resultado extrapola o tipo sinaliza erro. A entrada decimal aceita somente dígitos de 0 a 9. O prefixo `0x` seleciona hexadecimal. A conversão para escalar produz exatamente 32 bytes em ordem big-endian. O teste matemático cobre inclusive o valor máximo do tipo, `2^256 − 1`. A validação de um escalar para uso na curva cabe ao módulo criptográfico.

Para um puzzle `N` de 1 a 160, a posição do bit mais significativo é `N − 1`, contando do zero. Assim, `lo = 2^(N−1)`, `hi = 2^N − 1` e o total do intervalo fechado é `2^(N−1)`. Em `L = ceil(N/8)` bytes ativos, o primeiro byte ocupa a posição `32 − L`. Seu intervalo inclusivo vai de `f = 1 << ((N−1) mod 8)` até `2f − 1`. Todos os bytes anteriores são zero. Para N = 160, o prefixo tem 12 bytes e os 20 bytes ativos começam com um valor de 80 a ff em hexadecimal.

## Produto cartesiano e ordinal

O construtor ordena as duas listas, verifica a faixa da primeira e rejeita listas vazias ou com repetições. Seja `a` o tamanho da primeira lista, `m` o tamanho da compartilhada e `d[i]` o índice do byte ativo `i` dentro da respectiva lista ordenada. A expressão de Horner abaixo produz um ordinal em `[0, a × m^(L−1))`.

```text
r = d[0]
para cada i de 1 a L−1:
    r = r × m + d[i]
```

A operação inversa usa divisão inteira por `m`, da última posição ativa até a segunda. Cada resto identifica exatamente um byte. Ao fim, o quociente é o índice na primeira lista. Quociente e resto são únicos, logo cada ordinal gera uma chave e cada chave do produto tem um único ordinal. Para `m = 1`, cada resto é zero. Para `L = 1`, as duas conversões acessam somente a primeira lista.

O acesso direto custa O(L) operações aritméticas de controle. Dentro de um segmento, o enumerador mantém a chave em 32 bytes e consulta tabelas de 256 entradas para obter os índices dos bytes. O avanço incrementa a posição mais à direita que ainda admite um sucessor e repõe o menor byte nas posições seguintes. O retrocesso usa o predecessor e repõe o maior byte. No limite, ambos retornam `false` preservando a chave. O custo por passo varia com o número de posições afetadas pelo carry. A memória do enumerador independe do número de combinações.

## Sorteio reproduzível e identidade

A versão `mt19937_64-fy-v1` usa os seguintes passos.

1. Ordenar o conjunto legal de entrada e verificar que o tamanho desejado está entre 1 e o tamanho do conjunto.
2. Inicializar `std::mt19937_64` com o XOR da seed de 64 bits e uma constante. A primeira lista usa `0xa0761d6478bd642f`. A lista compartilhada usa `0xe7037ed1a0b428db`.
3. Na posição `i`, tomar `b = tamanho − i`, descartar saídas de 64 bits menores que `2^64 mod b` e escolher `j = i + (saída mod b)`. Trocar as posições `i` e `j`.
4. Após o número solicitado de trocas, recolher e ordenar o prefixo selecionado.

O intervalo aceito no passo 3 tem cardinalidade divisível por `b`, dando a cada índice o mesmo número de preimagens entre palavras uniformes de 64 bits. A troca retira o escolhido do sufixo restante. O gerador é determinístico e o sorteio serve para reproduzir experimentos. Com o puzzle 160, seed 369 e tamanhos 1 e 3, as listas resultam em `87` e `63,70,aa`.

O ID é o SHA-256 de um registro ASCII de quatro linhas, cada uma terminada por LF. A primeira contém `exlottery-ticket-v1`, a segunda o número decimal do puzzle, a terceira a lista inicial e a quarta a compartilhada. Cada valor é codificado com dois dígitos hexadecimais minúsculos e a vírgula separa valores adjacentes. Por exemplo, estas quatro linhas, incluindo o LF final, produzem `8c083070038fa21df29ab64baeadb1adf092023bde3d8c7d0e8a5102c5668979`.

```text
exlottery-ticket-v1
17
01
00,31,ff
```

A identidade descreve a configuração das listas. Se um arquivo e um sorteio produzem as mesmas listas para um puzzle, seus IDs coincidem. O checkpoint contém também os metadados da execução.

## Execução por segmentos

Cada reserva concede a um worker um intervalo ordinal semiaberto. A aritmética de 256 bits situa o segmento e seu primeiro candidato. O tamanho de uma reserva cabe em 64 bits. Dentro dela, o worker alimenta o verificador em lotes e só confirma o segmento inteiro após o último lote. Essa separação permite refazer um segmento interrompido e calcular a cobertura confirmada mesmo se os workers terminarem fora de ordem. O modo reverso inverte tanto a ordem das reservas quanto a visita aos ordinais. Mais detalhes sobre o estado persistente e sua integridade estão em [CHECKPOINT.md](CHECKPOINT.md).

O verificador de fixtures recebe uma chave conhecida. A referência independente deriva a chave pública comprimida usando libsecp256k1 e calcula SHA-256 e RIPEMD-160 com OpenSSL. Para cada candidato, o núcleo local gera a chave pública e calcula HASH160. A verificação identifica a ocorrência da chave conhecida e confere seu digest contra a referência. O checksum de teste incorpora todos os digests, inclusive em testes negativos. A CLI valida um endereço P2PKH antes de iniciar os workers.

O backend EC `reference` multiplica cada escalar pelo gerador usando o núcleo original copiado em `vendor/ex369`. O backend `jump` calcula o primeiro ponto e atualiza o ponto Jacobiano pelos deltas assinados entre escalares. Cada worker conserva em cache até 512 transições e usa inversão em lote para serializar os pontos. O núcleo suporta as representações de campo 5×52 e 4×64 por seleção de build. O hashing usa a implementação escalar ou grupos de quatro lanes em máquinas com SSSE3. As sobras do lote seguem o caminho escalar. As taxas publicadas incluem a derivação da chave pública e os dois hashes. Consulte [BENCHMARKS.md](BENCHMARKS.md).

## Cooperação e cobertura

Em Lottery Mode, a mesma chave pode aparecer em vários volantes. Para duas configurações do mesmo puzzle com `L > 1`, o tamanho da interseção é `|F₁ ∩ F₂| × |S₁ ∩ S₂|^(L−1)`. Para `L = 1`, basta contar a interseção das primeiras listas. IDs distintos, por si só, fornecem pouca informação sobre o tamanho dessa interseção.

Um manifesto futuro de Coverage Mode pode usar o puzzle, a versão do enumerador, o ID e as duas listas canônicas para reconstruir o domínio. Para distribuir um único volante entre máquinas, intervalos ordinais semiabertos com extremos consecutivos de 0 até o total provam disjunção e união completa. Para cobrir a faixa inteira com volantes distintos, uma partição do primeiro byte legal com todos os 256 valores nas demais posições também fornece essa prova. Dividir apenas a lista compartilhada pode deixar lacunas. Por exemplo, com dois bytes compartilhados, os grupos `{00}` e `{01}` juntos produzem `00 00` e `01 01`, deixando de fora `00 01` e `01 00`. A V1 executa o agendamento em uma máquina.

Se existe exatamente uma chave-alvo uniformemente distribuída entre os `T = 2^(N−1)` escalares do puzzle, a probabilidade de ela pertencer a um volante escolhido independentemente do alvo é `TOTAL/T`. Qualquer candidato fixo tem probabilidade prévia `1/T`. Restringir as listas reduz o número de verificações e a probabilidade de cobrir a chave pela mesma proporção. Um experimento com chave plantada mede cobertura e correção dentro do volante.
