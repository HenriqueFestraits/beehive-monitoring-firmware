# PROTOCOLO DE COMUNICAÇÃO USB SERIAL — V1

## 1. HANDSHAKE

| Sinal | Emissor | Descrição |
|---|---|---|
| `HELLO\n` | Celular | Inicia a comunicação com o ESP32. |
| `READY\n` | ESP32 | Confirma que está pronto para receber comandos. |
| `ERROR:BAD_COMMAND\n` | ESP32 | Indica que foi recebido um comando inválido ou inesperado. |

**Fluxo:** o celular envia `HELLO\n` e aguarda `READY\n`. Caso não receba uma resposta dentro do timeout, poderá tentar novamente.

## 2. SOLICITAÇÃO DO ARQUIVO

| Sinal | Emissor | Descrição |
|---|---|---|
| `GET_FILE\n` | Celular | Solicita o arquivo de medições após receber `READY\n`. |
| `ERROR:NO_FILE\n` | ESP32 | Indica que o arquivo solicitado não existe. |
| `ERROR:FILE_OPEN_FAILED\n` | ESP32 | Indica que não foi possível abrir o arquivo para leitura. |
| `ERROR:BAD_COMMAND\n` | ESP32 | Indica um comando inválido para o estado atual da comunicação. |

## 3. INÍCIO DA TRANSMISSÃO

| Sinal | Emissor | Descrição |
|---|---|---|
| `FILE_INFO:<tamanho_em_bytes>:<crc32>\n` | ESP32 | Informa o tamanho do arquivo e seu checksum CRC32 antes de transmitir os dados. |

O campo `tamanho_em_bytes` indica a quantidade exata de bytes que o celular deverá receber. O campo `crc32` contém o checksum do arquivo completo, representado em hexadecimal.

## 4. TRANSMISSÃO DOS BYTES

Após enviar `FILE_INFO`, o ESP32 transmite o conteúdo completo do arquivo em formato binário.

O celular deverá:

1. Ler exatamente a quantidade de bytes informada.
2. Calcular o CRC32 dos bytes recebidos.
3. Comparar o resultado com o CRC32 informado pelo ESP32.

Durante essa etapa, não devem ser enviadas mensagens textuais de depuração junto aos dados binários.

## 5. CONFIRMAÇÃO DA TRANSFERÊNCIA

| Sinal | Emissor | Descrição |
|---|---|---|
| `RECEIVED:<tamanho_em_bytes>:<crc32>\n` | Celular | Confirma que recebeu o arquivo completo e que o tamanho e o CRC32 são válidos. |
| `TRANSFER_OK\n` | ESP32 | Confirma que recebeu e validou a confirmação do celular. |
| `ERROR:CHECKSUM_MISMATCH\n` | Celular | Indica que o CRC32 calculado não corresponde ao informado pelo ESP32. |

O ESP32 somente considerará a transferência validada após conferir o tamanho e o CRC32 na mensagem `RECEIVED`. Em seguida, envia `TRANSFER_OK` e inicia a etapa de renovação do arquivo e sincronização do horário.

## 6. RENOVAÇÃO DO ARQUIVO E SINCRONIZAÇÃO DO HORÁRIO

Esta etapa ocorre na mesma sessão USB, imediatamente após o celular receber `TRANSFER_OK\n`.

| Sinal | Emissor | Descrição |
|---|---|---|
| `REQUEST_TIMESTAMP\n` | ESP32 | Solicita ao celular a data e hora atuais, representadas por um Unix timestamp. |
| `TIMESTAMP:<unix_seconds>\n` | Celular | Envia o horário atual como segundos desde 1º de janeiro de 1970, em UTC. |
| `TIMESTAMP_SAVED\n` | ESP32 | Confirma que criou o novo arquivo e gravou o timestamp inicial. |
| `ERROR:TIMESTAMP_TIMEOUT\n` | ESP32 | Indica que o celular não enviou o timestamp dentro do timeout. |
| `ERROR:BAD_TIMESTAMP\n` | ESP32 | Indica que a mensagem de timestamp está ausente, malformada ou contém caracteres não numéricos. |
| `ERROR:FILE_DELETE_FAILED\n` | ESP32 | Indica que não foi possível apagar o arquivo anterior. |
| `ERROR:FILE_CREATE_FAILED\n` | ESP32 | Indica que não foi possível criar o novo arquivo. |
| `ERROR:TIMESTAMP_SAVE_FAILED\n` | ESP32 | Indica que não foi possível gravar o timestamp completo no novo arquivo. |

### Fluxo após a confirmação

1. O celular recebe `TRANSFER_OK\n` e mantém a conexão USB aberta.
2. O ESP32 apaga o arquivo de medições anterior.
3. O ESP32 envia `REQUEST_TIMESTAMP\n`.
4. O celular obtém a data e hora atuais e responde com `TIMESTAMP:<unix_seconds>\n`.
5. O ESP32 valida o valor recebido e cria um novo `/measurements.bin`.
6. O timestamp Unix é gravado como o primeiro valor de 8 bytes do novo arquivo, no formato binário `uint64_t), compatível com a estrutura atual do firmware.
7. O ESP32 responde `TIMESTAMP_SAVED\n` quando a gravação é concluída com sucesso.

O timestamp enviado pelo celular deve ser um inteiro decimal positivo, em **segundos** (não milissegundos), e representar UTC. O celular só deve considerar a sincronização concluída após receber `TIMESTAMP_SAVED\n`.

**Importante:** conforme este fluxo, o arquivo antigo é apagado antes de solicitar o timestamp. Se a comunicação falhar ou o timestamp não puder ser salvo, o arquivo anterior já terá sido removido; o ESP32 deverá comunicar o erro e não deverá informar que a sincronização foi concluída.

## 7. TRATAMENTO DE ERROS

- O handshake terá timeout de 30 segundos.
- O celular poderá realizar até três tentativas de handshake, com intervalo de um segundo entre elas.
- Se houver erro durante a transmissão, o celular não deverá confirmar o recebimento.
- Se o ESP32 não receber uma confirmação válida, a transferência será considerada incompleta e o arquivo original deverá ser preservado.
- Se o celular não receber `TRANSFER_OK\n`, não deverá iniciar a etapa de sincronização do timestamp.
- Após `TRANSFER_OK\n`, ambos os dispositivos deverão manter a conexão aberta até a conclusão da etapa de timestamp ou até que ocorra um erro/timeout.

## 8. REGRAS GERAIS

1. A comunicação utiliza USB Serial a 115200 baud no ambiente de teste. A configuração final deverá ser validada na integração com os celulares.
2. As mensagens de controle utilizam ASCII e terminam em `\n`. O receptor também deverá aceitar `\r\n`.
3. O arquivo será transmitido em formato binário, sem conversão dos dados.
4. O tamanho será expresso em bytes.
5. O CRC32 será calculado sobre o arquivo completo, do primeiro ao último byte.
6. O CRC32 usa o algoritmo CRC-32/ISO-HDLC (CRC-32/IEEE), com polinômio refletido `0xEDB88320`.
7. O aplicativo somente enviará a confirmação após validar o arquivo recebido.
8. O ESP32 somente enviará `TRANSFER_OK\n` após validar a confirmação.
9. Mensagens de depuração não poderão ser intercaladas com os bytes binários durante a transferência.
10. O arquivo original não poderá ser apagado antes da confirmação válida `RECEIVED`.
11. A renovação do arquivo e a sincronização do timestamp ocorrem depois de `TRANSFER_OK\n`, na mesma sessão de comunicação.