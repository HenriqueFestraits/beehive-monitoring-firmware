#include <LittleFS.h>
#include <time.h>

#define MEASUREMENT_FILE "/measurements.bin"

struct Measurement {
    float temperature;
    float humidity;
    uint16_t eco2;
    uint16_t tvoc;
    uint32_t id;
};


void setup() {

    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("     LEITOR DE MEDICOES");
    Serial.println("================================");

    // Inicializa o LittleFS
    if (!LittleFS.begin(false)) {
        Serial.println("ERRO: Falha ao iniciar LittleFS!");
        return;
    }

    Serial.println("LittleFS iniciado com sucesso!");

    readMeasurementFile();
}


void loop() {

}


void readMeasurementFile() {

    if (!LittleFS.exists(MEASUREMENT_FILE)) {
        Serial.println("ERRO: Arquivo de medidas nao existe!");
        return;
    }

    File measurementFile = LittleFS.open(MEASUREMENT_FILE, FILE_READ);

    if (!measurementFile) {
        Serial.println("ERRO: Nao foi possivel abrir o arquivo!");
        return;
    }

    Serial.println();
    Serial.println("Arquivo aberto com sucesso!");
    Serial.print("Tamanho do arquivo: ");
    Serial.print(measurementFile.size());
    Serial.println(" bytes");

    // ========================================
    // LEITURA DO TIMESTAMP
    // ========================================

    uint64_t timestamp;

    size_t timestampBytesRead = measurementFile.read(
        (uint8_t*)&timestamp,
        sizeof(timestamp)
    );

    if (timestampBytesRead != sizeof(timestamp)) {
        Serial.println("ERRO: Nao foi possivel ler o timestamp completo!");
        measurementFile.close();
        return;
    }

    Serial.println();
    Serial.println("================================");
    Serial.println("        TIMESTAMP INICIAL");
    Serial.println("================================");

    Serial.print("Unix: ");
    Serial.println(timestamp);

    printTimestamp(timestamp);


    // ========================================
    // LEITURA DAS MEDICOES
    // ========================================

    Serial.println();
    Serial.println("================================");
    Serial.println("          MEDICOES");
    Serial.println("================================");

    uint32_t measurementCount = 0;

    while (measurementFile.available()) {

        Measurement measurement;

        size_t bytesRead = measurementFile.read(
            (uint8_t*)&measurement,
            sizeof(Measurement)
        );

        if (bytesRead != sizeof(Measurement)) {
            Serial.println();
            Serial.println("ERRO: Medicao incompleta encontrada!");
            break;
        }

        Serial.println();
        Serial.println("--------------------------------");

        Serial.print("ID: ");
        Serial.println(measurement.id);

        Serial.print("Temperatura: ");
        Serial.print(measurement.temperature);
        Serial.println(" °C");

        Serial.print("Umidade: ");
        Serial.print(measurement.humidity);
        Serial.println(" %");

        Serial.print("eCO2: ");
        Serial.print(measurement.eco2);
        Serial.println(" ppm");

        Serial.print("TVOC: ");
        Serial.print(measurement.tvoc);
        Serial.println(" ppb");

        measurementCount++;
    }

    measurementFile.close();

    Serial.println();
    Serial.println("================================");
    Serial.println("           RESUMO");
    Serial.println("================================");

    Serial.print("Quantidade de medicoes: ");
    Serial.println(measurementCount);

    Serial.println();
    Serial.println("Leitura finalizada.");
}


void printTimestamp(uint64_t timestamp) {

    // Converte uint64_t para time_t
    time_t rawTime = (time_t)timestamp;

    // Configura o fuso horario de Sao Paulo
    setenv("TZ", "BRT3", 1);
    tzset();

    struct tm timeInfo;

    localtime_r(&rawTime, &timeInfo);

    Serial.print("Data/Hora: ");

    if (timeInfo.tm_mday < 10) {
        Serial.print("0");
    }
    Serial.print(timeInfo.tm_mday);
    Serial.print("/");

    if (timeInfo.tm_mon + 1 < 10) {
        Serial.print("0");
    }
    Serial.print(timeInfo.tm_mon + 1);
    Serial.print("/");

    Serial.print(timeInfo.tm_year + 1900);

    Serial.print(" ");

    if (timeInfo.tm_hour < 10) {
        Serial.print("0");
    }
    Serial.print(timeInfo.tm_hour);
    Serial.print(":");

    if (timeInfo.tm_min < 10) {
        Serial.print("0");
    }
    Serial.print(timeInfo.tm_min);
    Serial.print(":");

    if (timeInfo.tm_sec < 10) {
        Serial.print("0");
    }
    Serial.println(timeInfo.tm_sec);
}