# Protocolo de comunicação USB Serial — implementação atual

Este documento descreve o comportamento implementado em `teste_de_firmware/teste_de_firmware.ino` e as rotinas equivalentes de `firmware/firmware.ino`, na branch `main`. O objetivo é documentar o comportamento atual, incluindo erros e limitações conhecidas.

## 1. Configuração e formato de `measurements.bin`

- Interface: USB Serial a 115200 baud.
- Mensagens de controle terminam em LF (\n); a leitura também descarta CR (\r) e remove espaços nas extremidades.
- Timeout do handshake: 30.000 ms.
- Timeout de leitura de linha do protocolo: 30.000 ms.
- Comprimento máximo de linha do protocolo: 128 caracteres.
- Buffer de transmissão binária: 256 bytes.
- Sistema de arquivos: LittleFS.

### Organização atual do arquivo

O arquivo `/measurements.bin` tem um cabeçalho seguido por zero ou mais medições:

| Ordem | Conteúdo | Tamanho esperado |
|---|---|---:|
| 1 | Endereço MAC da placa em texto ASCII, por exemplo `AA:BB:CC:11:22:33`, seguido de terminador nulo (\0) | 18 bytes |
| 2 | Timestamp inicial, gravado diretamente como `uint64_t` binário nativo | 8 bytes |
| 3 | Registros `Measurement`, gravados diretamente da memória | 16 bytes cada na compilação atual |

Portanto, o cabeçalho esperado ocupa **26 bytes**: 18 bytes do MAC e 8 bytes do timestamp. A primeira medição começa no byte 26. O MAC não é gravado como seis bytes binários; ele é armazenado como a string retornada por `WiFi.macAddress()`, incluindo o terminador nulo.

A estrutura de cada medição é:

```cpp
struct Measurement {
    float temperature;
    float humidity;
    uint16_t eco2;
    uint16_t tvoc;
    uint32_t id;
};
```

O firmware transmite o arquivo como bytes binários, sem converter o payload em texto. O CRC inclui o cabeçalho completo (MAC e timestamp) e todos os registros de medição. O timestamp é interpretado pelo aplicativo como segundos Unix; o firmware armazena o valor numérico recebido.

> **Compatibilidade:** arquivos antigos que começam diretamente com o timestamp (sem MAC) não seguem o formato atual e não devem ser interpretados com o novo layout.

## 2. Fluxo principal de transferência

Quando `/measurements.bin` existe e o firmware identifica um despertar por GPIO, `normalBootSetup()` chama `handleWakeupGPIO()`, que executa `runFileTransferProtocol()`.

O fluxo de controle implementado é:

1. O firmware aguarda o handshake USB por até 30 segundos. A rotina de handshake reconhece a linha `HELLO` e responde com `READY\n`. Na versão de teste, também é impresso um `HELLO\n` antes da espera.
2. O cliente envia `GET_FILE\n`.
3. O ESP32 aceita somente `GET_FILE`. Um comando inesperado gera `ERROR:BAD_COMMAND\n`; timeout ou linha vazia interrompe o fluxo sem necessariamente gerar uma resposta de erro específica.
4. O ESP32 abre `/measurements.bin`, calcula o CRC-32 do arquivo inteiro e reposiciona a leitura no início.
5. O ESP32 envia `FILE_INFO:<tamanho_decimal>:<CRC32_hexadecimal_maiusculo>\n`.
6. O ESP32 transmite exatamente o número de bytes indicado em `FILE_INFO`. Não há delimitador textual após o payload binário.
7. O cliente lê exatamente esse número de bytes, calcula o CRC e envia `RECEIVED:<tamanho_decimal>:<CRC32_hexadecimal>\n`.
8. Se o tamanho e o CRC forem válidos, o ESP32 responde `TRANSFER_OK\n`.
9. O ESP32 envia `REQUEST_TIMESTAMP\n` e aguarda o novo timestamp.
10. O cliente envia o timestamp como dígitos decimais puros, seguido de LF, em segundos Unix.
11. O ESP32 cria e verifica um arquivo temporário contendo o MAC da placa e o novo timestamp; depois tenta substituir o arquivo principal usando `.tmp` e `.bak`.
12. Se a substituição retornar sucesso, o ESP32 envia `SESSION_COMPLETE\n`.

O cliente só deve considerar a sessão concluída ao receber `SESSION_COMPLETE\n`. Diagnósticos adicionais podem ser escritos na mesma serial; o cliente deve distinguir as mensagens de controle do protocolo das mensagens de diagnóstico.

## 3. Mensagens de controle

| Mensagem | Direção | Significado |
|---|---|---|
| `HELLO\n` | Cliente → ESP32 | Inicia/estabelece o handshake. |
| `READY\n` | ESP32 → Cliente | Handshake reconhecido. |
| `GET_FILE\n` | Cliente → ESP32 | Solicita a transmissão do arquivo. |
| `FILE_INFO:<size>:<crc32>\n` | ESP32 → Cliente | Tamanho decimal do arquivo e CRC-32 hexadecimal de oito dígitos. |
| Payload binário | ESP32 → Cliente | Exatamente `size` bytes de `/measurements.bin`. |
| `RECEIVED:<size>:<crc32>\n` | Cliente → ESP32 | Confirma o tamanho e o CRC calculados pelo cliente. |
| `TRANSFER_OK\n` | ESP32 → Cliente | Confirmação aceita pelo ESP32. |
| `REQUEST_TIMESTAMP\n` | ESP32 → Cliente | Solicita o timestamp que iniciará o próximo arquivo. |
| `<unix_seconds>\n` | Cliente → ESP32 | Timestamp decimal, sem prefixo, em segundos Unix. |
| `SESSION_COMPLETE\n` | ESP32 → Cliente | Arquivo novo instalado e rotina de substituição concluída com sucesso. |

### CRC-32

O firmware usa CRC-32/ISO-HDLC (também chamado CRC-32/IEEE), com polinômio refletido `0xEDB88320`, valor inicial `0xFFFFFFFF` e XOR final `0xFFFFFFFF`.

- O CRC cobre todos os bytes do arquivo, incluindo MAC, terminador nulo, timestamp e medições.
- O CRC em `FILE_INFO` é enviado em hexadecimal maiúsculo com oito dígitos.
- Na confirmação `RECEIVED`, o tamanho é decimal e o CRC é hexadecimal; o código compara os valores convertidos.
- O cliente deve tratar o payload como binário, nunca como texto.

## 4. Recebimento do timestamp e substituição do arquivo

A rotina `receiveNextTimestamp()`:

1. Envia `REQUEST_TIMESTAMP\n`.
2. Aguarda uma linha por até 30 segundos.
3. Rejeita caracteres que não estejam entre `0` e `9`.
4. Converte o valor com `strtoull()`.
5. Emite também um diagnóstico `Timestamp recebido: ...`.

O código não verifica explicitamente o indicador de overflow de `strtoull()`; portanto, a rejeição de valores acima do intervalo de `uint64_t` não está garantida.

A rotina `replaceMeasurementFileWithTimestamp()` realiza estas etapas:

1. Remove `/measurements.tmp` antigo, caso exista.
2. Cria `/measurements.tmp`.
3. Obtém o MAC com `WiFi.macAddress()` e grava a string seguida de `\0` (normalmente 18 bytes).
4. Grava os 8 bytes do novo timestamp após o MAC.
5. Fecha e reabre o temporário para verificar o MAC, o timestamp e o tamanho total esperado de 26 bytes.
6. Remove `/measurements.bak` antigo, caso exista.
7. Renomeia `/measurements.bin` para `/measurements.bak`, se o arquivo principal existir.
8. Renomeia `/measurements.tmp` para `/measurements.bin`.
9. Se a promoção do temporário falhar depois de mover o arquivo antigo, tenta restaurar o backup.
10. Se a promoção funcionar, remove o backup antigo.

O novo arquivo fica com o mesmo MAC da placa e o timestamp recém-recebido; as próximas medições são anexadas depois do cabeçalho de 26 bytes. O firmware só envia `SESSION_COMPLETE\n` se a função retornar sucesso.

Se a remoção do backup falhar depois que o arquivo novo já foi instalado, a função retorna falha e não envia `SESSION_COMPLETE`, embora o novo arquivo possa já estar no caminho principal. O esquema temporário/backup reduz o risco de perda, mas não há recuperação automática geral de `.tmp` e `.bak` na inicialização.

## 5. Erros e falhas

As mensagens abaixo são as mensagens explícitas relacionadas ao protocolo encontradas no código. Nem toda falha de I/O produz um código específico; em alguns casos o fluxo simplesmente retorna falha e o cliente deixa de receber a próxima mensagem esperada.

| Mensagem | Direção | Quando ocorre |
|---|---|---|
| `ERROR:BAD_COMMAND\n` | ESP32 → Cliente | Linha maior que o limite em `readProtocolLine()` ou comando inesperado em `waitForFileRequest()`. |
| `ERROR:NO_FILE\n` | ESP32 → Cliente | `/measurements.bin` não existe quando a transmissão começa. |
| `ERROR:FILE_OPEN_FAILED\n` | ESP32 → Cliente | Falha ao abrir o arquivo principal para leitura. |
| `ERROR:FILE_SEEK_FAILED\n` | ESP32 → Cliente | Falha ao reposicionar o arquivo antes de transmitir. |
| `ERROR:BAD_CONFIRMATION\n` | ESP32 → Cliente | Mensagem `RECEIVED` malformada ou CRC recebido inválido/diferente do esperado. |
| `ERROR:SIZE_MISMATCH\n` | ESP32 → Cliente | Tamanho em `RECEIVED` não corresponde ao tamanho transmitido ou não foi convertido integralmente. |
| `ERROR:CHECKSUM_MISMATCH\n` | Cliente → ESP32 | O cliente pode enviar esse código ao detectar divergência de checksum; o ESP32 reconhece-o e aborta a transferência. O ESP32 não gera esse código nesse caminho. |
| `ERROR:TIMESTAMP_TIMEOUT\n` | ESP32 → Cliente | Falha ao ler uma linha de timestamp, inclusive timeout ou linha vazia. |
| `ERROR:TIMESTAMP_EMPTY\n` | ESP32 → Cliente | Existe uma verificação para timestamp vazio em `receiveNextTimestamp()`, mas, no caminho atual, `readProtocolLine()` já retorna falha para linha vazia; por isso, esse código não é normalmente alcançado. |
| `ERROR:INVALID_TIMESTAMP\n` | ESP32 → Cliente | Timestamp contém caracteres não decimais ou a conversão não consome toda a linha. |
| `ERROR:REMOVE_TEMP_FAILED\n` | ESP32 → Cliente | Não foi possível remover um arquivo temporário antigo. |
| `ERROR:TEMP_OPEN_FAILED\n` | ESP32 → Cliente | Não foi possível abrir `/measurements.tmp` para escrita. |
| `ERROR:TEMP_MAC_WRITE_FAILED\n` | ESP32 → Cliente | A gravação do MAC no temporário não escreveu todos os bytes esperados. **Novo erro relacionado ao cabeçalho com MAC.** |
| `ERROR:TEMP_WRITE_FAILED\n` | ESP32 → Cliente | A gravação do timestamp no temporário não escreveu os 8 bytes esperados. |
| `ERROR:TEMP_VERIFY_OPEN_FAILED\n` | ESP32 → Cliente | Não foi possível reabrir o temporário para verificação. |
| `ERROR:TEMP_VERIFY_FAILED\n` | ESP32 → Cliente | O MAC, terminador nulo, timestamp ou tamanho do temporário não corresponde ao esperado. |
| `ERROR:REMOVE_BACKUP_FAILED\n` | ESP32 → Cliente | Não foi possível remover o backup antigo. |
| `ERROR:OLD_FILE_RENAME_FAILED\n` | ESP32 → Cliente | Não foi possível renomear o arquivo principal para backup. |
| `ERROR:NEW_FILE_RENAME_FAILED\n` | ESP32 → Cliente | Não foi possível promover o temporário para `/measurements.bin`. |
| `ERROR:FILE_RESTORE_FAILED\n` | ESP32 → Cliente | A tentativa de restaurar o backup após falha de promoção também falhou. |
| `ERROR:BACKUP_CLEANUP_FAILED\n` | ESP32 → Cliente | O novo arquivo já foi instalado, mas o backup não pôde ser removido. |

Mensagens como `ERRO: ...`, `HELLO`, `TIMEOUT`, `Timestamp recebido: ...`, mensagens de inicialização e mensagens sobre sensores são diagnósticos do firmware, não códigos formais do protocolo de transferência. Alguns desses diagnósticos também usam a mesma USB Serial, então o cliente não deve presumir que toda linha recebida seja uma mensagem de controle.

No primeiro boot, as rotinas de criação também podem imprimir diagnósticos locais, como falha ao abrir o arquivo, falha ao gravar o MAC ou falha ao gravar o timestamp. Essas mensagens não fazem parte da sequência de controle `HELLO/GET_FILE`.

## 6. Inicialização e primeiro boot

O comportamento depende da existência de `/measurements.bin`:

- **Arquivo existente:** o firmware consulta `esp_sleep_get_wakeup_cause()`. Se a causa for `ESP_SLEEP_WAKEUP_GPIO`, executa o protocolo de transferência. O despertar por timer não inicia a transferência.
- **Arquivo inexistente:** `firstBootSetup()` espera um timestamp inicial diretamente pela serial, sem handshake `HELLO/READY`. Depois, `saveInitialTimestamp()` cria o arquivo e grava MAC (string com terminador nulo) seguido do timestamp binário.
- **Ciclo normal:** o `loop()` realiza uma medição, tenta anexá-la ao arquivo, aguarda 3 segundos e entra em deep sleep.

A configuração usa `SLEEP_TIME` para o temporizador e `BUTTON_PIN` com `ESP_GPIO_WAKEUP_GPIO_HIGH` para despertar por GPIO. Na XIAO ESP32-C6, `BUTTON_PIN` é GPIO0 (D0). O barramento I²C usa GPIO22 (D4/SDA) e GPIO23 (D5/SCL).

## 7. Limitações conhecidas da implementação

1. **O MAC e o timestamp alteram os offsets do arquivo.** O cabeçalho atual tem 26 bytes, e qualquer leitor ou rotina que procure o timestamp ou as medições precisa pular os 18 bytes do MAC.
2. **A rotina de ID da medição precisa acompanhar o novo cabeçalho.** No código atual, `getNextMeasurementId()` ainda verifica se o tamanho do arquivo é igual a `sizeof(uint64_t)` para detectar um arquivo sem medições e calcula a posição da última medição sem descontar o cabeçalho de 26 bytes. Isso não corresponde ao layout atual e pode produzir IDs/leitura incorretos; essa rotina precisa ser corrigida no firmware.
3. A confirmação de CRC não valida a estrutura interna do arquivo; apenas confirma tamanho e checksum do payload transferido.
4. O temporário/backup não tem uma rotina geral de recuperação automática após queda de energia.
5. Os diagnósticos compartilham a USB Serial com o protocolo. Nenhum texto extra deve ser enviado entre `FILE_INFO` e o fim do payload binário, pois o cliente o interpretaria como bytes do arquivo.
6. O cliente deve implementar seus próprios timeouts para a transferência binária; o firmware tem timeout para o handshake e leitura de linhas, mas não implementa um timeout de protocolo separado para cada byte enviado.
7. Arquivos antigos sem MAC não são compatíveis com o novo layout sem migração ou recriação.

## 8. Requisitos para o cliente

- Abrir a porta serial a 115200 baud, 8 data bits, sem paridade e 1 stop bit.
- Enviar mensagens de controle terminadas em LF.
- Ler exatamente o tamanho indicado em `FILE_INFO`, sem tratar o payload como texto.
- Calcular CRC-32/ISO-HDLC sobre o arquivo completo recebido.
- Enviar `RECEIVED:<size>:<CRC32>` somente depois de validar o payload.
- Enviar o novo timestamp como inteiro decimal em segundos Unix, sem prefixo e terminado em LF.
- Considerar a sessão concluída somente após receber `SESSION_COMPLETE\n`.
- Em timeout, erro explícito ou ausência da resposta esperada, considerar a sessão incompleta.

Este documento descreve a implementação disponível na branch `main`; atualize-o novamente quando o código do firmware mudar.
