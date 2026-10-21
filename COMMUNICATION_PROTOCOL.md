# Protocolo de comunicação USB Serial — implementação atual

Este documento descreve o comportamento encontrado em `teste_de_firmware/teste_de_firmware.ino` na branch `main`. Ele documenta o que o código faz hoje, inclusive limitações conhecidas; não representa funcionalidades futuras ou comportamentos desejáveis que ainda não estejam implementados.

## 1. Configuração e formato do arquivo

- Interface: USB Serial, configurada com `Serial.begin(115200)`.
- Final de linha: LF (`\n`). As funções de leitura removem CR e espaços nas linhas recebidas.
- Timeout do handshake: 30.000 ms.
- Timeout de leitura de linha do protocolo: 30.000 ms.
- Comprimento máximo de linha do protocolo: 128 caracteres.
- Buffer de transmissão binária: 256 bytes.

O arquivo `/measurements.bin` é armazenado no LittleFS e contém:

1. Timestamp inicial em formato binário nativo `uint64_t` (8 bytes).
2. Zero ou mais estruturas `Measurement`, gravadas diretamente da memória (16 bytes na compilação atual).

A estrutura atual é:

```cpp
struct Measurement {
    float temperature;
    float humidity;
    uint16_t eco2;
    uint16_t tvoc;
    uint32_t id;
};
```

O firmware transmite o arquivo como bytes binários, sem convertê-lo para texto. O timestamp é interpretado como segundos Unix pelo aplicativo, embora o firmware armazene apenas o valor numérico recebido.

## 2. Fluxo principal de transferência

Quando `/measurements.bin` já existe e o firmware identifica um despertar por GPIO, `normalBootSetup()` chama `handleWakeupGPIO()`, que executa `runFileTransferProtocol()`.

O fluxo implementado é:

1. O cliente envia `HELLO\n`.
2. O ESP32 espera até 30 segundos por uma linha igual a `HELLO`. Linhas diferentes são ignoradas durante essa espera.
3. Ao receber `HELLO`, o ESP32 envia `READY\n` e, em seguida, escreve a mensagem de diagnóstico `Handshake estabelecido!\n`.
4. O cliente envia `GET_FILE\n`.
5. O ESP32 lê uma linha e aceita somente `GET_FILE`. Se a linha for diferente, emite `ERROR:BAD_COMMAND\n` e interrompe o protocolo. Se a leitura falhar por timeout ou linha vazia, retorna falha sem necessariamente emitir uma mensagem de erro de protocolo.
6. O ESP32 abre `/measurements.bin`, calcula o CRC-32 e retorna ao início do arquivo.
7. O ESP32 envia `FILE_INFO:<tamanho_decimal>:<CRC32_hexadecimal_maiusculo>\n`.
8. O ESP32 transmite exatamente o número de bytes anunciado em `FILE_INFO`.
9. O cliente deve ler esse número exato de bytes e calcular o CRC-32. O ESP32 não envia delimitador textual depois do payload binário.
10. O cliente confirma a recepção enviando `RECEIVED:<tamanho_decimal>:<CRC32_hexadecimal>\n`.
11. O ESP32 valida o tamanho e o CRC recebidos. Se ambos corresponderem aos valores do arquivo transmitido, envia `TRANSFER_OK\n`.
12. O ESP32 envia `REQUEST_TIMESTAMP\n` e aguarda uma linha com o novo timestamp.
13. O cliente envia o timestamp como dígitos decimais, seguido de LF, sem prefixo e em segundos Unix.
14. O ESP32 tenta substituir o arquivo de medições pelo novo arquivo que contém apenas o timestamp.
15. Se a função de substituição retornar sucesso, o ESP32 envia `SESSION_COMPLETE\n`.

O cliente só deve considerar a sessão concluída ao receber `SESSION_COMPLETE\n`.

## 3. Formato das mensagens

| Mensagem | Direção | Comportamento atual |
|---|---|---|
| `HELLO\n` | Cliente → ESP32 | Inicia a espera do handshake. |
| `READY\n` | ESP32 → Cliente | Emitido quando o ESP32 recebe `HELLO`. |
| `GET_FILE\n` | Cliente → ESP32 | Solicita o arquivo. |
| `FILE_INFO:<size>:<crc32>\n` | ESP32 → Cliente | Informa tamanho decimal e CRC-32 hexadecimal com oito dígitos maiúsculos. |
| Payload binário | ESP32 → Cliente | Exatamente `size` bytes do arquivo. |
| `RECEIVED:<size>:<crc32>\n` | Cliente → ESP32 | Confirma tamanho e CRC calculados pelo cliente. |
| `TRANSFER_OK\n` | ESP32 → Cliente | Emitido após validar a confirmação. |
| `REQUEST_TIMESTAMP\n` | ESP32 → Cliente | Solicita o timestamp seguinte. |
| `<unix_seconds>\n` | Cliente → ESP32 | Timestamp em dígitos decimais puros, sem prefixo. |
| `SESSION_COMPLETE\n` | ESP32 → Cliente | Emitido quando a substituição do arquivo retorna sucesso. |

### CRC-32

O firmware implementa CRC-32/ISO-HDLC (CRC-32/IEEE), com polinômio refletido `0xEDB88320`, inicialização e XOR final `0xFFFFFFFF`.

- O CRC cobre todos os bytes do arquivo.
- `FILE_INFO` contém oito dígitos hexadecimais em maiúsculas.
- Na mensagem `RECEIVED`, o CRC é interpretado como hexadecimal e comparado numericamente; maiúsculas e minúsculas são aceitas.
- O tamanho é interpretado como número decimal.

## 4. Recebimento do timestamp e substituição do arquivo

Após validar `RECEIVED`, o firmware envia `REQUEST_TIMESTAMP\n`. O método `receiveNextTimestamp()`:

1. Aguarda uma linha por até 30 segundos.
2. Rejeita uma linha não vazia que contenha caracteres fora de `0` a `9`.
3. Converte a linha com `strtoull()` para um inteiro sem sinal.
4. Emite uma mensagem de diagnóstico `Timestamp recebido: ...\n`.

O firmware não verifica explicitamente o indicador de overflow de `strtoull()`; portanto, a rejeição de timestamps que excedam o intervalo de `uint64_t` não está garantida na implementação atual.

Para substituir o arquivo, `replaceMeasurementFileWithTimestamp()`:

1. Remove um arquivo temporário antigo, se existir.
2. Cria `/measurements.tmp` e grava os 8 bytes do timestamp.
3. Reabre o temporário e verifica seu tamanho e o valor gravado.
4. Remove `/measurements.bak` antigo, se existir.
5. Renomeia `/measurements.bin` para `/measurements.bak`, caso o arquivo principal exista.
6. Renomeia `/measurements.tmp` para `/measurements.bin`.
7. Se a promoção do temporário falhar após mover o arquivo antigo, tenta restaurar o backup.
8. Se a promoção funcionar, remove o backup antigo.
9. Retorna sucesso somente se todas as operações necessárias, incluindo a remoção do backup, forem concluídas. Só então o fluxo envia `SESSION_COMPLETE\n`.

Se a remoção do backup falhar depois que o novo arquivo já foi instalado, a função retorna falha e o firmware não envia `SESSION_COMPLETE`, embora o novo arquivo já possa estar em `/measurements.bin`.

O novo arquivo principal contém inicialmente apenas o timestamp de 8 bytes. As medições seguintes são anexadas a esse arquivo.

## 5. Erros e falhas observados no código

As mensagens abaixo são as mensagens explícitas encontradas na implementação. Nem todo caminho de falha produz uma mensagem de erro; em alguns casos o cliente apenas deixa de receber a resposta esperada.

| Mensagem | Direção | Quando ocorre |
|---|---|---|
| `ERROR:BAD_COMMAND\n` | ESP32 → Cliente | Linha acima do limite em `readProtocolLine()` ou comando inesperado em `waitForFileRequest()`. |
| `ERROR:NO_FILE\n` | ESP32 → Cliente | `/measurements.bin` não existe ao iniciar a transmissão. |
| `ERROR:FILE_OPEN_FAILED\n` | ESP32 → Cliente | Não foi possível abrir o arquivo para leitura. |
| `ERROR:FILE_SEEK_FAILED\n` | ESP32 → Cliente | Não foi possível reposicionar o arquivo antes da transmissão. |
| `ERROR:SIZE_MISMATCH\n` | ESP32 → Cliente | O tamanho informado em `RECEIVED` difere do tamanho esperado ou não foi convertido integralmente. |
| `ERROR:BAD_CONFIRMATION\n` | ESP32 → Cliente | A mensagem `RECEIVED` está malformada ou o CRC não corresponde. |
| `ERROR:CHECKSUM_MISMATCH\n` | Cliente → ESP32 | O cliente pode enviar essa mensagem se detectar divergência de CRC. O firmware reconhece essa mensagem e aborta a transferência; ele não a emite nesse caminho. |
| `ERROR:TIMESTAMP_TIMEOUT\n` | ESP32 → Cliente | A leitura do timestamp retorna falha, incluindo timeout ou linha vazia. |
| `ERROR:INVALID_TIMESTAMP\n` | ESP32 → Cliente | O timestamp contém caracteres não decimais ou a conversão não consome a linha inteira. |
| `ERROR:REMOVE_TEMP_FAILED\n` | ESP32 → Cliente | Não foi possível remover um temporário antigo. |
| `ERROR:TEMP_OPEN_FAILED\n` | ESP32 → Cliente | Não foi possível abrir o temporário para escrita. |
| `ERROR:TEMP_WRITE_FAILED\n` | ESP32 → Cliente | A gravação do timestamp não escreveu os 8 bytes esperados. |
| `ERROR:TEMP_VERIFY_OPEN_FAILED\n` | ESP32 → Cliente | Não foi possível reabrir o temporário para verificação. |
| `ERROR:TEMP_VERIFY_FAILED\n` | ESP32 → Cliente | O tamanho ou valor do timestamp gravado não corresponde ao esperado. |
| `ERROR:REMOVE_BACKUP_FAILED\n` | ESP32 → Cliente | Não foi possível remover um backup antigo. |
| `ERROR:OLD_FILE_RENAME_FAILED\n` | ESP32 → Cliente | Não foi possível renomear o arquivo principal para backup. |
| `ERROR:NEW_FILE_RENAME_FAILED\n` | ESP32 → Cliente | Não foi possível renomear o temporário para o arquivo principal. |
| `ERROR:FILE_RESTORE_FAILED\n` | ESP32 → Cliente | A tentativa de restaurar o backup falhou. |
| `ERROR:BACKUP_CLEANUP_FAILED\n` | ESP32 → Cliente | O novo arquivo foi instalado, mas o backup não pôde ser removido. |

As mensagens de diagnóstico, como `Handshake estabelecido!`, `Timestamp recebido: ...`, `Transferencia de arquivo concluida com sucesso.` e mensagens de inicialização, também são escritas na mesma USB Serial. Elas não são mensagens de controle do protocolo.

## 6. Inicialização e primeiro boot

O comportamento depende da existência de `/measurements.bin`:

- **Arquivo existente:** o firmware consulta `esp_sleep_get_wakeup_cause()`. Se a causa for `ESP_SLEEP_WAKEUP_GPIO`, chama o protocolo de transferência. Se for `ESP_SLEEP_WAKEUP_TIMER`, apenas registra a causa e segue para a inicialização dos sensores e o ciclo de medição.
- **Arquivo inexistente:** `firstBootSetup()` espera um timestamp inicial diretamente pela serial. Esse caminho não utiliza o handshake `HELLO/READY` nem a sequência de transferência. O valor é lido até LF e convertido com `strtoull()`; a validação estrita de dígitos usada em `receiveNextTimestamp()` não é aplicada nesse caminho.
- **Após a inicialização:** `setup()` retorna e o `loop()` executa uma medição, tenta anexá-la ao arquivo, aguarda 3 segundos e chama `enterDeepSleep()`.

A configuração atual de sono usa um temporizador definido por `SLEEP_TIME` e configura também o wake-up por GPIO usando `BUTTON_PIN` e `ESP_GPIO_WAKEUP_GPIO_HIGH`. A polaridade e o comportamento físico do botão dependem da ligação elétrica e devem ser verificados no hardware.

## 7. Limitações importantes da implementação atual

1. **Diagnósticos compartilham a serial com o protocolo.** Após `READY\n`, o firmware envia também `Handshake estabelecido!\n`. Como o cliente pode enviar `GET_FILE\n` ao receber `READY`, essa linha de diagnóstico pode ser lida por `waitForFileRequest()` antes de `GET_FILE`, provocando `ERROR:BAD_COMMAND`. Esse é um risco real de sincronização. Os diagnósticos devem ser removidos do canal de protocolo ou o código de leitura deve ser ajustado para ignorá-los com segurança.
2. Nunca devem ser enviados textos de diagnóstico entre `FILE_INFO` e o fim do payload binário: qualquer byte extra será interpretado como conteúdo do arquivo.
3. `readProtocolLine()` retorna falha para uma linha vazia, assim como para timeout; por isso, `receiveNextTimestamp()` atualmente reporta `ERROR:TIMESTAMP_TIMEOUT` também no caso de linha vazia, embora não exista um erro separado funcional para esse caso.
4. A confirmação de CRC é validada, mas o código não faz uma validação estrutural do arquivo para garantir que seu tamanho seja exatamente 8 bytes mais um múltiplo de `sizeof(Measurement)`.
5. O esquema temporário/backup reduz o risco de perda durante a substituição, mas não há uma rotina geral de recuperação de `.tmp` e `.bak` na inicialização após uma queda de energia.
6. A sessão de protocolo é executada quando o firmware detecta o despertar por GPIO no caminho de arquivo existente. O despertar por timer não inicia a transferência.
7. Os timeouts do cliente são responsabilidade do cliente. O firmware usa 30 segundos para o handshake e para leitura de linhas, mas o caminho de transmissão binária não implementa um timeout de protocolo separado para cada byte enviado.

## 8. Parâmetros para o cliente

- Abrir a porta serial a 115200 baud, 8 data bits, sem paridade e 1 stop bit.
- Enviar as mensagens de controle terminadas em LF.
- Não tratar o payload binário como texto.
- Ler exatamente o tamanho indicado em `FILE_INFO`.
- Calcular CRC-32/ISO-HDLC sobre o payload completo.
- Enviar `RECEIVED:<size>:<CRC32>` somente depois de validar o payload.
- Enviar o timestamp como inteiro decimal em segundos Unix, sem prefixo e terminado em LF.
- Considerar sucesso somente após receber `SESSION_COMPLETE\n`.
- Em caso de timeout, erro explícito ou ausência da resposta esperada, considerar a sessão incompleta.

Este documento deve ser atualizado novamente quando o comportamento do firmware mudar. As limitações descritas acima refletem o código atual e não devem ser interpretadas como recomendações para o protocolo final.
