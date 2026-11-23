# Changelog

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
