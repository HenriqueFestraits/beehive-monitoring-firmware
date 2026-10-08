#include <ScioSense_ENS160.h>
#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <LittleFS.h>

#define SDA_PIN 22
#define SCL_PIN 23
#define SLEEP_TIME 120
#define MEASUREMENT_FILE "/measurements.bin"

Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2CADDR_0);


void setup() {
    initializeSystem();
}

void loop() {

}


void initializeSystem() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);

    initializeFS();

    if(measurementFileExists()) {
        Serial.println("Measurement file exists.");
        normalBootSetup();
    } else {
        Serial.println("Measurement file does not exist.");
        firstBootSetup();
    }
}

void initializeFS(){
    if(!LittleFS.begin(true)){
    Serial.println("ERRO: Falha ao iniciar LittleFS!");
  }else{
    Serial.println("LittleFS iniciado com sucesso!");
    }
}

bool measurementFileExists() {
    return LittleFS.exists(MEASUREMENT_FILE);
}

void firstBootSetup(){

    uint64_t initialTimestamp;

    if(!receiveInitialTimestamp(initialTimestamp)) {
        Serial.println("ERRO: Falha ao receber o horario do dispositivo!");
        return;
    }
    Serial.println("Primeiro boot configurado!");
    Serial.print("Timestamp inicial: ");
    Serial.println(initialTimestamp);
    //criar arquivo de medidas

    if (!saveInitialTimestamp(initialTimestamp)) {
        Serial.println("ERRO: Falha ao salvar o timestamp inicial!");
        return;
}

void normalBootSetup(){

}

bool receiveInitialTimestamp(uint64_t &timestamp){
    Serial.println("Aguardando timestamp inicial do dispositivo...");

    while(!Serial.available()) {
        delay(100);
    }

    String receivedData = Serial.readStringUntil('\n');
    receivedData.trim(); // Remove espaços em branco no início e no final

    if(receivedData.length() == 0) {
        Serial.println("ERRO: Nenhum dado recebido.");
        return false;
    }

    timestamp = strtoull(receivedData.c_str(), nullptr, 10);
    Serial.print("Timestamp inicial recebido: ");
    Serial.println(timestamp);
    return true;
}

bool saveInitialTimestamp(uint64_t timestamp) {

    File measurementFile = LittleFS.open(MEASUREMENT_FILE, FILE_WRITE);

    if (!measurementFile) {
        Serial.println("ERRO: nao foi possivel abrir o arquivo de medidas!");
        return false;
    }

    size_t bytesWritten = measurementFile.write(
        (uint8_t*)&timestamp,
        sizeof(timestamp)
    );

    measurementFile.close();

    if (bytesWritten != sizeof(timestamp)) {
        Serial.println("ERRO: nao foi possivel gravar o timestamp completo!");
        return false;
    }

    Serial.println("Timestamp inicial salvo com sucesso!");

    return true;
}