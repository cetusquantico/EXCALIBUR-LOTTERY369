# Changelog

## 1.1.0 · 2026-11-23

Publicação open source e reforma do painel.

**Publicação e licença**

- Projeto publicado como open source sob **MIT**. `LICENSE` em texto MIT puro (detectável pelo GitHub) e `NOTICE` creditando o [keyhunt](https://github.com/albertobsd/keyhunt) (MIT, © 2020 Luis Alberto / albertobsd), de onde deriva o núcleo criptográfico vendado, e o libsecp256k1.
- README com crédito ao keyhunt e um endereço de apoio (doação BTC).

**Painel de volantes (GUI)**

- **Leitura da aposta ao vivo**: cobertura (chance real = fração do intervalo que o volante varre) e tempo de varredura, atualizados conforme a seleção de bytes. A cobertura é a chance honesta, não uma promessa.
- **Busca pela interface** (botão *Buscar…*): dispara o motor e mostra progresso, taxa, tempo e o resultado verificado ao vivo, sem travar; *Parar* usa SIGINT gracioso.
- **Checkpoint/resume no diálogo de busca**: salva o progresso e retoma de onde parou (à prova de pausa/reboot); a config vem do próprio checkpoint.
- **Overlay histórico**: mapa de frequência dos bytes nas 83 chaves resolvidas (prova visual de que a distribuição é uniforme, não dica) e opção de revelar a chave de qualquer puzzle resolvido na grade.
- **Usabilidade**: botão *Colar* endereço, botão *Copiar hex* do resultado, e *threads* até 32.
- **Idioma PT/EN** com troca ao vivo (botão no cabeçalho).

**Desempenho e compatibilidade**

- Backend **AVX2** de 8 lanes para HASH160 multi-buffer, com dispatch em runtime (AVX2 → SSSE3 → escalar). `--simd auto` passa a usar o tier mais largo que a CPU executa; `--simd avx2` disponível. Apenas esse objeto usa `-mavx2`, então o binário continua portável e uma CPU sem AVX2 nunca executa instruções AVX2.
- `docs/COMPATIBILITY.md`: distros suportadas, dependências, build portável (padrão) vs. `-DEXLOTTERY_NATIVE=ON`, dispatch de SIMD e status do AVX2.

**Testes e CI**

- Teste headless novo (`panel_run`, sem X11) cobrindo a busca ao vivo e o ciclo checkpoint → interrupção → resume → conclusão; o teste `crypto` passa a conferir o backend AVX2 contra o scalar quando a CPU suporta.
- CI instala `libclang-rt-18-dev` (conserta o link do build com sanitizer no clang); matriz g++/clang++ × 5x52/4x64 totalmente verde.

**Documentação**

- `docs/KANGAROO_DESIGN.md` atualizado: o kangaroo saiu do papel e virou o projeto público EXCALIBUR-KANGAROO369; correção do K medido (~2, não 1,15) e nota sobre a reimplementação Gaudry–Schost.

## 1.0.3 · Revisão local

- Validação offline de número e endereço P2PKH no planejamento dos 160 puzzles.
- Identificação do teste de fixture no resumo e no formato KV. A saída humana redirecionada omite os campos privados por padrão.
- Regressões para catálogo, checksum, ausência de acertos, proteção de saída e permissões de checkpoint. Ambiente de CI fixado por digest e snapshot.

## 1.0.2

- A opção `--stop-on-hit` encerra a varredura quando um bloco confirma a chave conhecida. O checkpoint registra a cobertura processada e permite continuar depois.

## 1.0.1 · 2026-11-21

- A saída padrão agrupa a confirmação da chave conhecida e as métricas da sessão. A chave aparece com 64 dígitos hexadecimais e os bytes ativos também aparecem individualmente na ordem do escalar.
- `--quiet` permite ler o resultado e o resumo da execução. `--output kv` preserva o formato de campos para integrações.
- O painel local permite montar, nomear, validar e selecionar volantes por uma grade de bytes. A CLI aceita o nome salvo com `--volante`.

## 1.0.0 · 2026-11-21

Primeira distribuição independente do EXCALIBUR LOTTERY369.

- Faixas 1–160 com índices exatos de 256 bits, allowlists canônicas e acesso ordinal reversível.
- Sorteio Fisher–Yates parcial versionado, ID SHA-256 e planejamento por dry-run.
- Verificação de fixtures conhecidas com backends de multiplicação EC e saltos, HASH160 escalar e SSSE3.
- Workers por segmentos, afinidade opcional, confirmação durável com retomada e verificação de integridade.
- Vetores resolvidos e sintéticos, testes automatizados, medições no i5-3470 e configurações GCC/Clang com sanitizers.
