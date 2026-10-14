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

O ESP32 somente considerará a transferência concluída após validar a mensagem `RECEIVED`.

## 6. TRATAMENTO DE ERROS

- O handshake terá timeout de 30 segundos.
- O celular poderá realizar até três tentativas de handshake, com intervalo de um segundo entre elas.
- Se houver erro durante a transmissão, o celular não deverá confirmar o recebimento.
- Se o ESP32 não receber uma confirmação válida, a transferência será considerada incompleta.
- O arquivo original deverá ser preservado em caso de falha.

## 7. REGRAS GERAIS

1. A comunicação utiliza USB Serial a 115200 baud no ambiente de teste. A configuração final deverá ser validada na integração com os celulares.
2. As mensagens de controle utilizam ASCII e terminam em `\n`. O receptor também deverá aceitar `\r\n`.
3. O arquivo será transmitido em formato binário, sem conversão dos dados.
4. O tamanho será expresso em bytes.
5. O CRC32 será calculado sobre o arquivo completo, do primeiro ao último byte.
6. O aplicativo somente enviará a confirmação após validar o arquivo recebido.
7. O ESP32 somente considerará a transferência concluída após validar a confirmação.
8. Mensagens de depuração não poderão ser intercaladas com os bytes binários durante a transferência.
9. O arquivo original não poderá ser apagado antes da confirmação válida.
10. A criação de um novo arquivo e a atualização do timestamp serão tratadas em uma etapa posterior do protocolo.