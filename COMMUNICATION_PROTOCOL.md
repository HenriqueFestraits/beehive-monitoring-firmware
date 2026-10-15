# PROTOCOLO DE COMUNICAÇÃO USB SERIAL — V2

## 1. Visão geral

O protocolo transfere o arquivo de medições do ESP32 para o celular e, somente após a confirmação da integridade da transferência, inicia um novo ciclo de medições com um timestamp Unix atualizado.

A comunicação ocorre na mesma sessão USB Serial, a 115200 baud no ambiente de teste.

O arquivo `/measurements.bin` possui o seguinte formato binário:

1. Timestamp inicial: `uint64_t` (8 bytes).
2. Zero ou mais registros `Measurement` (16 bytes cada na configuração atual do firmware).

O arquivo é transmitido sem conversão, exatamente como está armazenado no LittleFS.

## 2. Fluxo completo

1. Celular envia `HELLO\n`.
2. ESP32 responde `READY\n`.
3. Celular envia `GET_FILE\n`.
4. ESP32 envia `FILE_INFO:<tamanho_em_bytes>:<crc32>\n`.
5. ESP32 transmite exatamente `<tamanho_em_bytes>` bytes binários.
6. Celular valida o tamanho e o CRC-32 e envia `RECEIVED:<tamanho_em_bytes>:<crc32>\n`.
7. ESP32 valida a confirmação e responde `TRANSFER_OK\n`.
8. ESP32 solicita o novo timestamp com `REQUEST_TIMESTAMP\n`.
9. Celular envia o novo timestamp Unix como inteiro decimal seguido de `\n`.
10. ESP32 grava e verifica o timestamp em `/measurements.tmp`.
11. ESP32 renomeia `/measurements.bin` para `/measurements.bak`, renomeia o temporário para `/measurements.bin` e remove o backup antigo.
12. Se todas as operações forem concluídas, ESP32 responde `SESSION_COMPLETE\n`. A sessão lógica está concluída e o celular pode encerrar a comunicação.

O arquivo anterior não deve ser substituído antes de o celular confirmar a recepção correta do arquivo e antes de o novo timestamp ter sido gravado e verificado no arquivo temporário.

## 3. Mensagens de controle

Todas as mensagens de controle são texto ASCII terminado em LF (\n). O receptor deve aceitar CRLF (\r\n), removendo o CR final.

| Mensagem | Direção | Significado |
|---|---|---|
| `HELLO\n` | Celular → ESP32 | Inicia a sessão. |
| `READY\n` | ESP32 → Celular | ESP32 aceita iniciar a transferência. |
| `GET_FILE\n` | Celular → ESP32 | Solicita o arquivo de medições. |
| `FILE_INFO:<size>:<crc32>\n` | ESP32 → Celular | Informa o tamanho exato em bytes e o CRC-32 do arquivo. |
| `RECEIVED:<size>:<crc32>\n` | Celular → ESP32 | Confirma que recebeu todos os bytes e validou tamanho e CRC. |
| `TRANSFER_OK\n` | ESP32 → Celular | Confirma que a mensagem RECEIVED foi validada. |
| `REQUEST_TIMESTAMP\n` | ESP32 → Celular | Solicita um novo timestamp Unix. |
| `<unix_seconds>\n` | Celular → ESP32 | Envia o timestamp como inteiro decimal, sem prefixo. |
| `SESSION_COMPLETE\n` | ESP32 → Celular | Confirma que o novo arquivo foi instalado e o backup removido. |

### Formato do CRC-32

- O CRC é calculado sobre todos os bytes do arquivo, do primeiro ao último.
- Algoritmo: CRC-32/ISO-HDLC (CRC-32/IEEE).
- Polinômio refletido: `0xEDB88320`.
- O valor em `FILE_INFO` é enviado em hexadecimal, com oito dígitos; letras hexadecimais maiúsculas são usadas pelo firmware.
- O celular deve enviar o CRC em hexadecimal na mensagem `RECEIVED`. O firmware compara o valor numérico, portanto letras maiúsculas ou minúsculas são aceitas.
- O tamanho é um inteiro decimal em bytes.

## 4. Transmissão binária

Após enviar `FILE_INFO`, o ESP32 transmite o conteúdo binário do arquivo. O celular deve ler exatamente a quantidade de bytes indicada em `FILE_INFO`; não deve procurar delimitadores de linha dentro dos dados binários.

O celular só deve enviar `RECEIVED:<size>:<crc32>\n` se recebeu exatamente o tamanho anunciado e o CRC calculado corresponder ao CRC anunciado. Se a validação falhar, pode enviar `ERROR:CHECKSUM_MISMATCH\n`; nesse caso, a transferência não será considerada concluída e o arquivo original deve permanecer no ESP32.

Não se deve interpretar os bytes do arquivo como texto.

## 5. Renovação do arquivo e sincronização do horário

A renovação ocorre somente depois de o ESP32 enviar `TRANSFER_OK\n`, na mesma sessão USB.

1. O ESP32 envia `REQUEST_TIMESTAMP\n`.
2. O celular envia o timestamp Unix atual como texto decimal puro, por exemplo `1792084212\n`. Não usar o prefixo `TIMESTAMP:` e não enviar milissegundos.
3. O ESP32 verifica que a linha contém somente dígitos decimais e converte o valor para `uint64_t`.
4. O ESP32 grava os 8 bytes do timestamp em `/measurements.tmp`.
5. O ESP32 reabre o temporário e verifica tamanho e valor gravado.
6. Se existir um backup antigo `/measurements.bak`, o ESP32 tenta removê-lo.
7. Se existir `/measurements.bin`, o ESP32 renomeia o arquivo para `/measurements.bak`.
8. O ESP32 renomeia `/measurements.tmp` para `/measurements.bin`.
9. Se essa renomeação falhar depois de mover o arquivo antigo, o firmware tenta restaurar o backup para `/measurements.bin`.
10. Depois de instalar o novo arquivo, o ESP32 remove `/measurements.bak`.
11. O ESP32 envia `SESSION_COMPLETE\n` somente se a função de substituição retornar sucesso.

O novo `/measurements.bin` deve conter exatamente 8 bytes no início do novo ciclo: o timestamp binário `uint64_t`. As medições seguintes serão anexadas após esses 8 bytes.

O timestamp deve ser um inteiro decimal em **segundos** desde 1º de janeiro de 1970 (Unix time), em UTC. Não enviar milissegundos nem caracteres adicionais.

## 6. Erros

O firmware pode emitir as seguintes mensagens de erro. O celular não deve interpretar nenhuma delas como sucesso.

| Mensagem | Significado |
|---|---|
| `ERROR:BAD_COMMAND\n` | Comando inesperado ou linha de protocolo inválida. |
| `ERROR:NO_FILE\n` | O arquivo de medições não existe. |
| `ERROR:FILE_OPEN_FAILED\n` | Não foi possível abrir o arquivo para leitura. |
| `ERROR:FILE_SEEK_FAILED\n` | Não foi possível reposicionar o arquivo para iniciar a transmissão. |
| `ERROR:SIZE_MISMATCH\n` | O tamanho informado pelo celular não corresponde ao esperado. |
| `ERROR:BAD_CONFIRMATION\n` | Confirmação ausente ou malformada, ou CRC inválido. |
| `ERROR:CHECKSUM_MISMATCH\n` | O celular informa que o CRC recebido não corresponde. |
| `ERROR:TIMESTAMP_TIMEOUT\n` | O celular não enviou uma linha de timestamp dentro do timeout. |
| `ERROR:TIMESTAMP_EMPTY\n` | A linha do timestamp está vazia. |
| `ERROR:INVALID_TIMESTAMP\n` | O timestamp contém caracteres que não são dígitos ou não pôde ser convertido. |
| `ERROR:REMOVE_TEMP_FAILED\n` | Não foi possível remover um temporário anterior. |
| `ERROR:TEMP_OPEN_FAILED\n` | Não foi possível abrir o arquivo temporário. |
| `ERROR:TEMP_WRITE_FAILED\n` | Não foi possível gravar todos os bytes do timestamp. |
| `ERROR:TEMP_VERIFY_OPEN_FAILED\n` | Não foi possível reabrir o temporário para verificação. |
| `ERROR:TEMP_VERIFY_FAILED\n` | O tamanho ou o valor gravado no temporário não corresponde ao esperado. |
| `ERROR:REMOVE_BACKUP_FAILED\n` | Não foi possível remover um backup anterior. |
| `ERROR:OLD_FILE_RENAME_FAILED\n` | Não foi possível renomear o arquivo atual para backup. |
| `ERROR:NEW_FILE_RENAME_FAILED\n` | Não foi possível promover o temporário para o nome definitivo. |
| `ERROR:FILE_RESTORE_FAILED\n` | A tentativa de restaurar o arquivo antigo falhou. |
| `ERROR:BACKUP_CLEANUP_FAILED\n` | O novo arquivo foi instalado, mas não foi possível remover o backup. |

O firmware também pode encerrar uma etapa com retorno de falha sem emitir uma mensagem específica em alguns caminhos. Se o celular não receber a resposta esperada dentro do timeout, deverá considerar a sessão incompleta.

## 7. Timeouts e encerramento

- Timeout do handshake no firmware: 30 segundos.
- Timeout de leitura de linhas do protocolo: 30 segundos.
- O celular deve usar timeouts próprios e não esperar indefinidamente.
- Após enviar `HELLO\n`, o celular aguarda `READY\n`.
- Após enviar `GET_FILE\n`, o celular aguarda `FILE_INFO` e então lê exatamente o tamanho anunciado.
- Após enviar `RECEIVED`, o celular aguarda `TRANSFER_OK\n`.
- Após receber `TRANSFER_OK\n`, o celular aguarda `REQUEST_TIMESTAMP\n` e então envia o timestamp.
- A sessão só deve ser considerada concluída após `SESSION_COMPLETE\n`.
- Em caso de erro ou timeout, o celular não deve informar que a sessão foi concluída.

## 8. Regras de implementação e observações

1. USB Serial utiliza 115200 baud no ambiente de teste.
2. As mensagens de controle usam ASCII e terminam em LF; CRLF também deve ser aceito pelo receptor.
3. O arquivo é transmitido em formato binário sem conversão.
4. O ESP32 deve enviar `TRANSFER_OK` somente após validar tamanho e CRC recebidos na confirmação do celular.
5. O arquivo antigo não deve ser apagado antes de a transferência ter sido confirmada.
6. A troca usa `/measurements.tmp` e `/measurements.bak` para reduzir o risco de perda de dados, mas uma interrupção de energia durante operações de renomeação ainda exige recuperação de arquivos na inicialização.
7. Mensagens de depuração não fazem parte do protocolo. Na versão atual do firmware, algumas mensagens de diagnóstico são escritas na mesma serial que o protocolo. Antes de integrar ao aplicativo, remova essas mensagens do canal de protocolo ou faça o cliente ignorá-las com segurança. Nunca escreva mensagens de depuração entre `FILE_INFO` e o fim do fluxo binário.
8. Os nomes e os formatos documentados aqui descrevem o código atual. Mudanças no firmware e no aplicativo devem manter este documento sincronizado.
