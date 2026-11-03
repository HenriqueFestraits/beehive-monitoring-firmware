#include <LittleFS.h>

#define SERIAL_BAUD_RATE 115200
#define FORMAT_CONFIRMATION "FORMAT"

bool fileSystemMounted = false;

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);

    printHeader();

    // Tenta montar sem formatacao automatica.
    // Assim, uma falha de montagem nao apaga os dados silenciosamente.
    fileSystemMounted = LittleFS.begin(false);

    if (fileSystemMounted) {
        Serial.println("LittleFS montado com sucesso.");
        printFileSystemInfo();
    } else {
        Serial.println("AVISO: Nao foi possivel montar o LittleFS.");
        Serial.println("A formatacao ainda pode ser tentada.");
    }

    Serial.println();
    Serial.println("ATENCAO: a formatacao apagara TODOS os arquivos do LittleFS.");
    Serial.println("Isso inclui measurements.bin, arquivos temporarios e backups.");
    Serial.println();
    Serial.println("Para confirmar, digite FORMAT e pressione ENTER.");
    Serial.println("Qualquer outra entrada cancela a operacao.");

    waitForConfirmation();
}

void loop() {
    // Utilitario executado uma unica vez durante o setup.
}

void printHeader() {
    Serial.println();
    Serial.println("========================================");
    Serial.println("       FORMATACAO COMPLETA - LITTLEFS");
    Serial.println("       Seeed Studio XIAO ESP32-C6");
    Serial.println("========================================");
}

void waitForConfirmation() {
    while (!Serial.available()) {
        delay(50);
    }

    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command != FORMAT_CONFIRMATION) {
        Serial.println();
        Serial.println("FORMATACAO CANCELADA.");
        finish();
        return;
    }

    formatFileSystem();
}

void formatFileSystem() {
    Serial.println();
    Serial.println("Iniciando formatacao do LittleFS...");

    // Desmonta o sistema antes de formatar, se ele estiver montado.
    if (fileSystemMounted) {
        LittleFS.end();
        fileSystemMounted = false;
    }

    if (!LittleFS.format()) {
        Serial.println("ERRO: Falha ao formatar o LittleFS.");
        Serial.println("Verifique a configuracao da particao e tente novamente.");
        finish();
        return;
    }

    Serial.println("Formatacao concluida.");

    // Monta novamente sem permitir formatacao automatica.
    if (!LittleFS.begin(false)) {
        Serial.println("ERRO: A formatacao ocorreu, mas o LittleFS nao montou.");
        finish();
        return;
    }

    fileSystemMounted = true;

    Serial.println("LittleFS montado novamente com sucesso.");
    Serial.println("Verificando o sistema de arquivos...");

    if (LittleFS.totalBytes() == 0) {
        Serial.println("ERRO: O tamanho da particao retornado e zero.");
        finish();
        return;
    }

    if (LittleFS.usedBytes() != 0) {
        Serial.println("AVISO: O LittleFS reportou espaco utilizado apos a formatacao.");
    } else {
        Serial.println("Verificacao concluida: nenhum espaco utilizado.");
    }

    printFileSystemInfo();

    Serial.println();
    Serial.println("SUCESSO: LittleFS formatado e pronto para uso.");
    Serial.println("O firmware principal podera inicializar um novo measurements.bin.");
    finish();
}

void printFileSystemInfo() {
    Serial.println();
    Serial.println("--------- INFORMACOES DO LITTLEFS ---------");
    Serial.print("Capacidade total: ");
    Serial.print(LittleFS.totalBytes());
    Serial.println(" bytes");

    Serial.print("Espaco utilizado: ");
    Serial.print(LittleFS.usedBytes());
    Serial.println(" bytes");

    Serial.print("Espaco livre: ");
    Serial.print(LittleFS.totalBytes() - LittleFS.usedBytes());
    Serial.println(" bytes");
    Serial.println("-------------------------------------------");
}

void finish() {
    Serial.println();
    Serial.println("Processo finalizado. Reinicie a placa para executar novamente.");
    while (true) {
        delay(1000);
    }
}
