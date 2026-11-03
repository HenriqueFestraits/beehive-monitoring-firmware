# formatacao_esp

Utilitario para formatar completamente o sistema de arquivos LittleFS do Seeed Studio XIAO ESP32-C6.

## O que faz

1. Tenta montar o LittleFS sem formatacao automatica.
2. Solicita uma confirmacao explicita pela USB Serial.
3. Executa `LittleFS.format()`, apagando todos os arquivos e diretorios da particao LittleFS.
4. Monta o sistema novamente e verifica as informacoes de capacidade e uso.

A formatacao apaga permanentemente os dados armazenados no LittleFS, incluindo:

- `/measurements.bin`
- `/measurements.tmp`
- `/measurements.bak`
- quaisquer outros arquivos criados pelo firmware.

## Como utilizar

1. Abra a pasta `formatacao_esp` na Arduino IDE.
2. Abra `formatacao_esp.ino`.
3. Selecione a placa **Seeed XIAO ESP32C6** e a porta serial correspondente.
4. Compile e envie o sketch para a placa.
5. Abra o Monitor Serial em **115200 baud**.
6. Leia o aviso e digite exatamente `FORMAT`, seguido de ENTER, para confirmar.
7. Aguarde a mensagem de sucesso.

Qualquer outra entrada cancela a operacao. Para executar novamente, reinicie a placa.

## Observacoes

- O sketch usa `LittleFS.begin(false)`: nao solicita formatacao automatica durante a montagem.
- A operacao formata a particao LittleFS configurada para a placa; nao apaga a flash inteira nem altera o firmware/bootloader.
- Se a montagem inicial falhar, o utilitario ainda permite tentar a formatacao explicitamente.
- Apos a formatacao, reinicie a placa e carregue o firmware principal. O fluxo de primeiro boot podera criar um novo arquivo de medicoes.
