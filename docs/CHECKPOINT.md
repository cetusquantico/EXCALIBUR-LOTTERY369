# Agendamento e retomada

Cada segmento contém os ordinais de `begin` até `end - 1`. O agendador reserva até `batch` ordinais consecutivos em cada chamada. No modo sequencial, reserva a partir da menor lacuna. No reverso, começa no fim da maior posição pendente. O worker escolhe a ordem de visita dentro da reserva segundo o mesmo modo.

O worker chama `complete` apenas depois de concluir o segmento inteiro. O agendador verifica as duas extremidades da reserva e incorpora seu intervalo e sua contagem de acertos sob o mesmo mutex. As atualizações no checkpoint são feitas por segmento. Um worker pode executar todo o pipeline de um candidato sem adquirir o mutex do agendador.

## Estado persistido

O JSON canônico contém `schema`, os metadados da configuração em `metadata`, `total`, `mode`, `completed`, `hits`, `ranges` e `sha256`. Os inteiros do estado persistente usam strings decimais canônicas de até 256 bits. Cada elemento de `ranges` é um par de extremidades, também strings decimais. Os pares estão ordenados e segmentos adjacentes são combinados.

O intervalo pendente na retomada é o complemento de `ranges` em `[0,total)`. Uma interrupção pode causar repetição de trabalho feito desde o último snapshot durável. A retomada conta somente segmentos confirmados. Reservas em andamento nunca entram no arquivo, inclusive quando outros workers já confirmaram ordinais posteriores. Esse desenho permite trocar o número de workers ou o tamanho de lote entre sessões do agendador.

O contador `hits` pertence aos mesmos segmentos confirmados. O estado armazena a quantidade de acertos nesses segmentos. O tamanho do estado em memória é proporcional ao número de lacunas, intervalos confirmados e reservas em andamento. Uma sequência concluída de trilhões de ordinais adjacentes ocupa um único par de extremidades.

## Escrita e integridade

No Linux, o gravador cria um arquivo temporário de modo 0600 no diretório de destino, grava os dados, executa `fsync`, fecha o arquivo e usa `rename` para substituir o checkpoint. O último `fsync` no diretório torna o rename durável quando o sistema de arquivos oferece essa garantia. Uma falha reportada após o rename indica que a nova versão está visível e que sua persistência em caso de queda é incerta. A trava consultiva `<checkpoint>.lock` usa `flock` e acompanha a sessão inteira. O arquivo da trava permanece para que processos concorrentes compartilhem o mesmo inode. A liberação do descritor ao encerrar a sessão libera a trava. Sistemas de arquivos remotos podem ter regras próprias de durabilidade e bloqueio.

O SHA-256 cobre o objeto JSON canônico sem o membro final `sha256` e sem a quebra de linha. O leitor confere o digest, desserializa, valida os limites e a ordenação e exige igualdade byte a byte com a serialização canônica. Espaços extras, campos duplicados, truncamento e somatórios divergentes invalidam a retomada. O checksum detecta corrupção acidental. Para intercâmbio entre máquinas é preciso confiar na origem dos checkpoints, pois quem controla o arquivo pode recalcular seu checksum.

## Portabilidade de segmentos

Um segmento só tem significado junto da versão do enumerador, do puzzle e do identificador do volante. Reservas de máquinas distintas cobrem partes disjuntas quando usam a mesma ordenação e intervalos sem interseção no mesmo volante. Grupos diferentes escolhidos em Lottery Mode exigem análise de interseção das listas. Um manifesto de Coverage Mode deve provar disjunção e união dos conjuntos para o domínio declarado. A V1 faz agendamento local.
