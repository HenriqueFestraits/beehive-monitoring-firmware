#include <ScioSense_ENS160.h>
#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <LittleFS.h>

#define SDA_PIN 22
#define SCL_PIN 23
#define SLEEP_TIME 10 // tempo de sono em segundos (10s para teste, 2h para produção)
#define MEASUREMENT_FILE "/measurements.bin"
#define USB_HANDSHAKE_TIMEOUT 30000

bool handshakeStabilished = false;

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
    Serial.print("Tamanho da struct Measurement: ");
    Serial.println(sizeof(Measurement));
}

void loop() {

    saveMeasurementToFile(performMeasurement());
    delay(3000);

    enterDeepSleep();
}






//inicializa o sistema toda vez que o esp32 é bootado
void initializeSystem() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);

    initializeFS();

    //deleteMeasurementFile(); // Apaga o arquivo de medidas para teste(remover no programa final)

    //verificação para o primeiro boot
    if(measurementFileExists()) {
        Serial.println("Measurement file exists.");
        normalBootSetup();
    } else {
        Serial.println("Measurement file does not exist.");
        firstBootSetup();
    }

    initializeSensors();
}




//inicializa o file system usado em todo o programa
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


//ciclo do primeiro boot após upload do programa no esp32 formatado
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

//ciclo dos demais boots para o esp32 funcionando normalmente
void normalBootSetup(){

    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    Serial.println("");
    Serial.println("------------------");

    if(wakeupCause == ESP_SLEEP_WAKEUP_GPIO){
        Serial.println("Wake-up causado pelo GPIO.");
        //fluxo de comunicação com o dispositivo
    }
    else if(wakeupCause == ESP_SLEEP_WAKEUP_TIMER){
        Serial.println("Wake-up causado pelo timer.");
        handleWakeupGPIO();
    }
    else{
        Serial.println("Wake-up causado motivo desconhecido.");
    }
}


// recebimento do timeStamp do dispositivo
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

//salvando o timestamp como primeiro elemento no arquivo de medicoes
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

//apagando o arquivo de medicoes
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


//inicializacao dos sensores
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


//realizacao das medicoes
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


//salvandomedicoes no arquivo
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

    Serial.print("Medida salva com ID: ");
    Serial.println(measurement.id);

    return true;
}


//calculo do id da medicao realizada
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


//entrando em deep sleep
void enterDeepSleep(){
    Serial.println("Entrando em modo de baixo consumo...");
    esp_sleep_enable_timer_wakeup(SLEEP_TIME * 1000000ULL);
    esp_deep_sleep_start();
}


// Espera HELLO e responde READY. Evita logs extras no canal do protocolo.
bool waitForHandshake(){
    unsigned long startTime = millis();

    while(millis() - startTime < USB_HANDSHAKE_TIMEOUT){
        String message;
        unsigned long elapsed = millis() - startTime;
        unsigned long remaining = USB_HANDSHAKE_TIMEOUT - elapsed;

        if(!readProtocolLine(message, remaining)){
            break; // timeout ou linha invalida/muito longa
        }

        if(message == "HELLO"){
            Serial.println("READY");
            Serial.flush();
            handshakeStabilished = true;
            return true;
        }

        Serial.println("ERROR:BAD_COMMAND");
    }

    handshakeStabilished = false;
    return false;
}


//acoes apos o estabelecimento de comunicacao
void handleUsbCommunication(){
    if(waitForHandshake()){
        Serial.println("Dispositivo conectado e pronto para comunicação.");
    }else{
        Serial.println("Nenhum handshake recebido");
    }
}

void handleWakeupGPIO(){
    handleUsbCommunication();
}


// ============================================================================
// Protocolo de transferencia USB Serial (V1)
// Estas funcoes ficam isoladas e NAO sao chamadas pelo fluxo atual do firmware.
// Para iniciar uma sessao explicitamente, chamar runFileTransferProtocol().
// ============================================================================

#define PROTOCOL_LINE_TIMEOUT 30000UL
#define PROTOCOL_MAX_LINE_LENGTH 128
#define TRANSFER_BUFFER_SIZE 256

// Le uma linha de controle terminada em LF. Aceita CRLF e limita o tamanho.
bool readProtocolLine(String &line, unsigned long timeoutMs) {
    line = "";
    unsigned long startTime = millis();

    while (millis() - startTime < timeoutMs) {
        while (Serial.available() > 0) {
            char receivedChar = (char)Serial.read();

            if (receivedChar == '\n') {
                line.trim();
                return line.length() > 0;
            }

            if (receivedChar != '\r') {
                if (line.length() >= PROTOCOL_MAX_LINE_LENGTH) {
                    Serial.println("ERROR:BAD_COMMAND");
                    line = "";
                    return false;
                }
                line += receivedChar;
            }
        }
        delay(5);
    }

    return false;
}

// CRC-32/ISO-HDLC (CRC-32/IEEE): polynomial reflected 0xEDB88320.
uint32_t calculateFileCrc32(File &file) {
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t buffer[TRANSFER_BUFFER_SIZE];

    file.seek(0);

    while (file.available()) {
        size_t bytesRead = file.read(buffer, sizeof(buffer));
        if (bytesRead == 0) {
            break;
        }

        for (size_t i = 0; i < bytesRead; i++) {
            crc ^= buffer[i];
            for (uint8_t bit = 0; bit < 8; bit++) {
                if (crc & 1U) {
                    crc = (crc >> 1) ^ 0xEDB88320UL;
                } else {
                    crc >>= 1;
                }
            }
        }
    }

    file.seek(0);
    return crc ^ 0xFFFFFFFFUL;
}

// Aguarda GET_FILE depois do handshake.
bool waitForFileRequest() {
    String message;

    if (!readProtocolLine(message, PROTOCOL_LINE_TIMEOUT)) {
        return false;
    }

    if (message == "GET_FILE") {
        return true;
    }

    Serial.println("ERROR:BAD_COMMAND");
    return false;
}

// Valida RECEIVED:<tamanho_em_bytes>:<crc32> enviado pelo aplicativo.
bool validateTransferConfirmation(const String &message,
                                 size_t expectedSize,
                                 uint32_t expectedCrc32) {
    if (message == "ERROR:CHECKSUM_MISMATCH") {
        return false;
    }

    if (!message.startsWith("RECEIVED:")) {
        Serial.println("ERROR:BAD_COMMAND");
        return false;
    }

    int separator = message.indexOf(':', 9);
    if (separator < 0) {
        Serial.println("ERROR:BAD_COMMAND");
        return false;
    }

    String sizeField = message.substring(9, separator);
    String crcField = message.substring(separator + 1);
    sizeField.trim();
    crcField.trim();

    if (sizeField.length() == 0 || crcField.length() == 0) {
        Serial.println("ERROR:BAD_COMMAND");
        return false;
    }

    char *endPointer = nullptr;
    unsigned long receivedSize = strtoul(sizeField.c_str(), &endPointer, 10);
    if (*endPointer != '\0' || receivedSize != expectedSize) {
        return false;
    }

    endPointer = nullptr;
    unsigned long receivedCrc = strtoul(crcField.c_str(), &endPointer, 16);
    if (*endPointer != '\0' || receivedCrc != expectedCrc32) {
        return false;
    }

    return true;
}

// Envia FILE_INFO e, em seguida, exatamente fileSize bytes binarios.
// Nao escreve logs textuais enquanto os bytes do arquivo estao sendo enviados.
bool transmitMeasurementFile(size_t &transmittedSize, uint32_t &transmittedCrc32) {
    transmittedSize = 0;
    transmittedCrc32 = 0;

    if (!LittleFS.exists(MEASUREMENT_FILE)) {
        Serial.println("ERROR:NO_FILE");
        return false;
    }

    File measurementFile = LittleFS.open(MEASUREMENT_FILE, FILE_READ);
    if (!measurementFile) {
        Serial.println("ERROR:FILE_OPEN_FAILED");
        return false;
    }

    const size_t fileSize = measurementFile.size();
    const uint32_t fileCrc32 = calculateFileCrc32(measurementFile);

    // Metadados textuais antes do fluxo binario.
    Serial.printf("FILE_INFO:%lu:%08lX\n",
                  (unsigned long)fileSize,
                  (unsigned long)fileCrc32);
    Serial.flush();

    uint8_t buffer[TRANSFER_BUFFER_SIZE];
    size_t totalSent = 0;

    while (totalSent < fileSize) {
        size_t remaining = fileSize - totalSent;
        size_t bytesToRead = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        size_t bytesRead = measurementFile.read(buffer, bytesToRead);

        if (bytesRead == 0) {
            measurementFile.close();
            return false;
        }

        size_t offset = 0;
        while (offset < bytesRead) {
            size_t bytesWritten = Serial.write(buffer + offset, bytesRead - offset);
            if (bytesWritten == 0) {
                measurementFile.close();
                return false;
            }
            offset += bytesWritten;
            totalSent += bytesWritten;
        }
    }

    measurementFile.close();
    Serial.flush();

    transmittedSize = totalSent;
    transmittedCrc32 = fileCrc32;
    return totalSent == fileSize;
}

// Aguarda a confirmacao do aplicativo e responde TRANSFER_OK somente se valida.
bool waitForTransferConfirmation(size_t expectedSize, uint32_t expectedCrc32) {
    String confirmation;

    if (!readProtocolLine(confirmation, PROTOCOL_LINE_TIMEOUT)) {
        Serial.println("ERROR:TRANSFER_CONFIRMATION_TIMEOUT");
        return false;
    }

    if (!validateTransferConfirmation(confirmation, expectedSize, expectedCrc32)) {
        return false;
    }

    Serial.println("TRANSFER_OK");
    Serial.flush();
    return true;
}

// Aceita TIMESTAMP:<unix_seconds> e valida que o campo contem apenas digitos.
bool parseTimestampResponse(const String &message, uint64_t &timestamp) {
    const String prefix = "TIMESTAMP:";

    if (!message.startsWith(prefix)) {
        return false;
    }

    String timestampField = message.substring(prefix.length());
    if (timestampField.length() == 0) {
        return false;
    }

    for (size_t i = 0; i < timestampField.length(); i++) {
        if (timestampField[i] < '0' || timestampField[i] > '9') {
            return false;
        }
    }

    char *endPointer = nullptr;
    unsigned long long parsedTimestamp = strtoull(timestampField.c_str(), &endPointer, 10);

    if (endPointer == timestampField.c_str() || *endPointer != '\0' || parsedTimestamp == 0) {
        return false;
    }

    timestamp = (uint64_t)parsedTimestamp;
    return true;
}

// Apos TRANSFER_OK, remove o arquivo antigo e solicita o Unix timestamp atual.
// O novo arquivo comeca pelos 8 bytes do timestamp, como no fluxo de primeiro boot.
bool resetMeasurementFileAndReceiveTimestamp() {
    if (LittleFS.exists(MEASUREMENT_FILE) && !LittleFS.remove(MEASUREMENT_FILE)) {
        Serial.println("ERROR:FILE_DELETE_FAILED");
        return false;
    }

    Serial.println("REQUEST_TIMESTAMP");
    Serial.flush();

    String response;
    if (!readProtocolLine(response, PROTOCOL_LINE_TIMEOUT)) {
        Serial.println("ERROR:TIMESTAMP_TIMEOUT");
        return false;
    }

    uint64_t currentTimestamp = 0;
    if (!parseTimestampResponse(response, currentTimestamp)) {
        Serial.println("ERROR:BAD_TIMESTAMP");
        return false;
    }

    File newMeasurementFile = LittleFS.open(MEASUREMENT_FILE, FILE_WRITE);
    if (!newMeasurementFile) {
        Serial.println("ERROR:FILE_CREATE_FAILED");
        return false;
    }

    size_t bytesWritten = newMeasurementFile.write(
        (const uint8_t *)&currentTimestamp,
        sizeof(currentTimestamp)
    );
    newMeasurementFile.close();

    if (bytesWritten != sizeof(currentTimestamp)) {
        LittleFS.remove(MEASUREMENT_FILE);
        Serial.println("ERROR:TIMESTAMP_SAVE_FAILED");
        return false;
    }

    Serial.println("TIMESTAMP_SAVED");
    Serial.flush();
    return true;
}

// Ponto de entrada do protocolo completo. Intencionalmente nao chamado por
// setup(), loop(), normalBootSetup() ou handleWakeupGPIO() nesta etapa.
bool runFileTransferProtocol() {
    if (!waitForHandshake()) {
        return false;
    }

    if (!waitForFileRequest()) {
        return false;
    }

    size_t transmittedSize = 0;
    uint32_t transmittedCrc32 = 0;

    if (!transmitMeasurementFile(transmittedSize, transmittedCrc32)) {
        return false;
    }

    if (!waitForTransferConfirmation(transmittedSize, transmittedCrc32)) {
        return false;
    }

    return resetMeasurementFileAndReceiveTimestamp();
}
