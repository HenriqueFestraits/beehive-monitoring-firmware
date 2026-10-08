#include <ScioSense_ENS160.h>
#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <LittleFS.h>

#define SDA_PIN 22
#define SCL_PIN 23
#define SLEEP_TIME 120
#define MEASUREMENT_FILE "/measurements.bin"

struct Measurement {
    float temperature;
    float humidity;
    uint16_t eco2;
    uint16_t tvoc;
    uint32_t id;
};

Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2CADDR_0);


void setup() {
    initializeSystem();
    Serial.println(sizeof(Measurement));
}

void loop() {

}







void initializeSystem() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);

    initializeFS();

    deleteMeasurementFile(); // Apaga o arquivo de medidas para teste    

    if(measurementFileExists()) {
        Serial.println("Measurement file exists.");
        normalBootSetup();
    } else {
        Serial.println("Measurement file does not exist.");
        firstBootSetup();
    }

    initializeSensors();
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
}

void normalBootSetup(){

    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();

    if(wakeupCause == ESP_SLEEP_WAKEUP_GPIO){
        Serial.println("Wake-up causado pelo GPIO.");
        //fluxo de comunicação com o dispositivo
    }
    else if(wakeupCause == ESP_SLEEP_WAKEUP_TIMER){
        Serial.println("Wake-up causado pelo timer.");
    }
    else{
        Serial.println("Wake-up causado motivo desconhecido.");
    }
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

void deleteMeasurementFile() {
    if (LittleFS.exists(MEASUREMENT_FILE)) {
        if (LittleFS.remove(MEASUREMENT_FILE)) {
            Serial.println("Arquivo de medidas apagado com sucesso!");
        } else {
            Serial.println("ERRO: Nao foi possivel apagar o arquivo!");
        }
    } else {
        Serial.println("Arquivo de medidas nao existe.");
    }
}


void initializeSensors() {

    if(!aht.begin()){
        Serial.println("ERRO: AHT21 nao encontrado!");
        while(1) delay(100);
    }
    Serial.println("AHT21 encontrado!");

    if(ens160.begin() != 0){
        Serial.println("Erro: ENS160 nao encontrado!");
        while(1) delay(100);
    }
    Serial.println("ENS160 encontrado!");

    if(ens160.setMode(ENS160_OPMODE_STD) == 0){
        Serial.println("ENS160 em modo padrao");
    }else{
        Serial.println("ERRO em configurar ENS160 em modo padrao!");
    }
}

Measurement performMeasurement(){
    Measurement measurement;

    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);

    measurement.temperature = temp.temperature;
    measurement.humidity = humidity.relative_humidity;
    ens160.set_envdata(measurement.temperature, measurement.humidity);

    //valido colocar um delay de 3 minutos para a estabilizacao do ens160, mas para teste sera colocado um delay de 10 segundos
    delay(10000);
    ens160.measure(true);
    measurement.eco2 = ens160.geteCO2();
    measurement.tvoc = ens160.getTVOC();
    return measurement;
}

bool saveMeasurementToFile(Measurement measurement){
    if(!LittleFS.exists(MEASUREMENT_FILE)) {
        Serial.println("ERRO: Arquivo de medidas nao existe!");
        return false;
    }

    File measurementFile = LittleFS.open(MEASUREMENT_FILE, FILE_APPEND);
    if (!measurementFile) {
        Serial.println("ERRO: Nao foi possivel abrir o arquivo de medidas!");
        return false;
    }

    measurement.id = getNextMeasurementId();

    size_t bytesWritten = measurementFile.write(
        (uint8_t*)&measurement,
        sizeof(measurement)
    );

    measurementFile.close();

    if (bytesWritten != sizeof(measurement)) {
        Serial.println("ERRO: Nao foi possivel gravar a medida completa!");
        return false;
    }

    Serial.println("Medida salva com sucesso!");
    return true;
}

uint32_t getNextMeasurementId() {

    File measurementFile = LittleFS.open(MEASUREMENT_FILE, FILE_READ);

    if (!measurementFile) {
        Serial.println("ERRO: nao foi possivel abrir o arquivo!");
        return 0;
    }

    size_t fileSize = measurementFile.size();

    // Arquivo possui apenas o timestamp inicial
    if (fileSize == sizeof(uint64_t)) {
        measurementFile.close();
        return 0;
    }

    // Calcula onde começa o ultimo Measurement
    size_t lastMeasurementPosition =
        fileSize - sizeof(Measurement);

    measurementFile.seek(lastMeasurementPosition);

    Measurement lastMeasurement;

    size_t bytesRead = measurementFile.read(
        (uint8_t*)&lastMeasurement,
        sizeof(Measurement)
    );

    measurementFile.close();

    if (bytesRead != sizeof(Measurement)) {
        Serial.println("ERRO: nao foi possivel ler o ultimo Measurement!");
        return 0;
    }

    return lastMeasurement.id + 1;
}