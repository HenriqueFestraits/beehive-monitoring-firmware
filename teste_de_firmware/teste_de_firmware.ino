#include <ScioSense_ENS160.h>
#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <LittleFS.h>

#define SDA_PIN 22   // XIAO ESP32-C6 D4 / GPIO22
#define SCL_PIN 23   // XIAO ESP32-C6 D5 / GPIO23
#define BUTTON_PIN 0 // XIAO ESP32-C6 D0 / GPIO0
#define SLEEP_TIME 10 // tempo de sono em segundos (10s para teste, 2h para produção)
#define MEASUREMENT_FILE "/measurements.bin"
#define TEMP_MEASUREMENT_FILE "/measurements.tmp"
#define BACKUP_MEASUREMENT_FILE "/measurements.bak"
#define USB_HANDSHAKE_TIMEOUT 30000
#define PROTOCOL_LINE_TIMEOUT 30000UL
#define PROTOCOL_MAX_LINE_LENGTH 128
#define TRANSFER_BUFFER_SIZE 256


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
    // A PCB usa pull-down: botao solto em LOW e pressionado em HIGH.
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
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
    if(!LittleFS.begin(false)){
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
        handleWakeupGPIO();
        //fluxo de comunicação com o dispositivo
    }
    else if(wakeupCause == ESP_SLEEP_WAKEUP_TIMER){
        Serial.println("Wake-up causado pelo timer.");
        
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

    esp_err_t result = esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);

    if(result != ESP_OK){
        // Mantem o ciclo por temporizador, mesmo se GPIO wakeup falhar.
        Serial.println("ERRO: Falha ao configurar wakeup por GPIO; usando somente o timer.");
    }
    esp_deep_sleep_start();
}

//esperando pelo handshake do dispositivo
bool waitForHandshake(){
    Serial.println("Aguardando handshake USB...");

    unsigned long startTime = millis();
    String message;

    while(millis() - startTime < USB_HANDSHAKE_TIMEOUT){
        while(Serial.available() > 0){
            char receivedChar = Serial.read();

            if(receivedChar == '\n'){
                message.trim();

                if(message == "HELLO"){
                    Serial.println("READY");

                    Serial.flush();
                    return true;
                }

                message = "";
            }else{
                message += receivedChar;
            }
        }
        delay(100);
    }

    Serial.println("Timeout aguardando handshake.");
    return false;
}


void handleWakeupGPIO(){
    if(!runFileTransferProtocol()) {
        Serial.println("ERRO: Falha na transferencia de arquivo.");
        return;
    } else {
        Serial.println("Transferencia de arquivo concluida com sucesso.");
    }
}


bool readProtocolLine(String &line, unsigned long timeoutMs){
    line = "";
    unsigned long startTime = millis();

    while (millis() - startTime < timeoutMs){
        while (Serial.available() > 0){
            char receivedChar = Serial.read();

            if(receivedChar == '\n'){
                line.trim();
                return line.length() > 0;
            }

            if(receivedChar != '\r'){
                if(line.length() >= PROTOCOL_MAX_LINE_LENGTH){
                    Serial.println("ERROR:BAD_COMMAND");
                    line = "";
                    return false;
                }
                line += receivedChar;
            }
        }
        delay(10);
    }
    
    return false;
}


// CRC-32/ISO-HDLC (CRC-32/IEEE): polynomial reflected 0xEDB88320
uint32_t calculatedFileCrc32(File &file){
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t buffer[TRANSFER_BUFFER_SIZE];

    file.seek(0);

    while(file.available()){
        size_t bytesRead = file.read(buffer, sizeof(buffer));
        if(bytesRead == 0) break;

        for(size_t i =0; i < bytesRead; i++){
            crc ^= buffer[i];
            for(uint8_t bit = 0; bit < 8; bit++){
                if(crc & 1U){
                    crc = (crc >> 1) ^ 0xEDB88320UL;
                }else{
                    crc >>= 1;
                }
            }
        }
    }
    file.seek(0);
    return crc ^ 0xFFFFFFFFUL;
}


bool waitForFileRequest(){
    String message;

    if(!readProtocolLine(message, PROTOCOL_LINE_TIMEOUT)){
        return false;
    }
    if(message == "GET_FILE"){
        return true;
    }
    Serial.println("ERROR:BAD_COMMAND");
    return false;
}


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
    const uint32_t fileCrc32 = calculatedFileCrc32(measurementFile);

    //O calculo do CRC percorreu o arquivo
    //Precisa voltar para o inicio do arquivo antes de iniciar a transmissao
    if(!measurementFile.seek(0)){
        measurementFile.close();
        Serial.println("ERROR:FILE_SEEK_FAILED");
        return false;
    }

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


bool waitForTransferConfirmation(size_t expectedSize, uint32_t expectedCrc32){
    String confirmation;

    if(!readProtocolLine(confirmation, PROTOCOL_LINE_TIMEOUT)){
        return false;
    }

    //celular pode informar que o checksum falhou
    if(confirmation == "ERROR:CHECKSUM_MISMATCH"){
        return false;
    }

    //verifica o inicio da mensagem
    if(!confirmation.startsWith("RECEIVED:")){
        Serial.println("ERROR:BAD_CONFIRMATION");
        return false;
    }

    //Remove "RECEIVED:"
    String payload = confirmation.substring(9);

    int separator = payload.indexOf(':');

    if(separator < 0){
        Serial.println("ERROR:BAD_CONFIRMATION");
        return false;
    }

    String sizeString = payload.substring(0, separator);
    String crcString = payload.substring(separator + 1);

    if(sizeString.length() == 0 || crcString.length() == 0){
        Serial.println("ERROR:BAD_CONFIRMATION");
        return false;
    }

    //Converte o tamanho decimal
    char *endSize;
    unsigned long long receivedSize = strtoul(sizeString.c_str(), &endSize, 10);
    if(*endSize != '\0' || receivedSize != expectedSize){
        Serial.println("ERROR:SIZE_MISMATCH");
        return false;
    }

    //Converte o CRC hexadecimal
    char *endCrc;
    unsigned long receivedCrc = strtoul(crcString.c_str(), &endCrc, 16);
    if(*endCrc != '\0' || receivedCrc != expectedCrc32){
        Serial.println("ERROR:BAD_CONFIRMATION");
        return false;
    }

    Serial.println("TRANSFER_OK");
    Serial.flush();

    return true;
}

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

    uint64_t nextTimestamp = 0;

    if (!receiveNextTimestamp(nextTimestamp)) {
        return false;
    }

    if(!replaceMeasurementFileWithTimestamp(nextTimestamp)) {
        return false;
    }

    Serial.println("SESSION_COMPLETE");
    Serial.flush();

    return true;
}

bool receiveNextTimestamp(uint64_t &timestamp){
    Serial.println("REQUEST_TIMESTAMP");
    Serial.flush();

    String receivedData;

    if(!readProtocolLine(receivedData, PROTOCOL_LINE_TIMEOUT)){
        Serial.println("ERROR:TIMESTAMP_TIMEOUT");
        return false;
    }

    if(receivedData.length() == 0) {
        Serial.println("ERROR:TIMESTAMP_EMPTY");
        return false;
    }

    for(size_t i = 0; i < receivedData.length(); i++){
        if(receivedData[i] < '0' || receivedData[i] > '9'){
            Serial.println("ERROR:INVALID_TIMESTAMP");
            return false;
        }
    }

    char *endPointer = nullptr;
    unsigned long long parsedTimestamp = strtoull(receivedData.c_str(), &endPointer, 10);

    if(endPointer == receivedData.c_str() || *endPointer != '\0'){
        Serial.println("ERROR:INVALID_TIMESTAMP");
        return false;
    }

    timestamp = static_cast<uint64_t>(parsedTimestamp);

    Serial.println("Timestamp recebido: " + String(timestamp));
    return true;
}


bool replaceMeasurementFileWithTimestamp(uint64_t timestamp){
    if(LittleFS.exists(TEMP_MEASUREMENT_FILE)){
        if(!LittleFS.remove(TEMP_MEASUREMENT_FILE)){
            Serial.println("ERROR:REMOVE_TEMP_FAILED");
            return false;
        }
    }

    File tempFile = LittleFS.open(TEMP_MEASUREMENT_FILE, FILE_WRITE);

    if(!tempFile){
        Serial.println("ERROR:TEMP_OPEN_FAILED");
        return false;
    }

    size_t bytesWritten = tempFile.write(reinterpret_cast<uint8_t*>(&timestamp), sizeof(timestamp));

    tempFile.close();

    if(bytesWritten != sizeof(timestamp)){
        LittleFS.remove(TEMP_MEASUREMENT_FILE);
        Serial.println("ERROR:TEMP_WRITE_FAILED");
        return false;
    }
    

    File verificationFile = LittleFS.open(TEMP_MEASUREMENT_FILE, FILE_READ);

    if(!verificationFile){
        Serial.println("ERROR:TEMP_VERIFY_OPEN_FAILED");
        return false;
    }

    uint64_t storedTimestamp = 0;

    size_t bytesRead = verificationFile.read(reinterpret_cast<uint8_t*>(&storedTimestamp), sizeof(storedTimestamp));

    bool validFile = bytesRead == sizeof(storedTimestamp) && verificationFile.size() == sizeof(uint64_t) && storedTimestamp == timestamp;


    verificationFile.close();

    if(!validFile){
        Serial.println("ERROR:TEMP_VERIFY_FAILED");
        return false;
    }

    if(LittleFS.exists(BACKUP_MEASUREMENT_FILE)){
        if(!LittleFS.remove(BACKUP_MEASUREMENT_FILE)){
            Serial.println("ERROR:REMOVE_BACKUP_FAILED");
            return false;
        }
    }

    bool oldFileMoved = false;

    if(LittleFS.exists(MEASUREMENT_FILE)){
        if(!LittleFS.rename(MEASUREMENT_FILE, BACKUP_MEASUREMENT_FILE)){
            Serial.println("ERROR:OLD_FILE_RENAME_FAILED");
            return false;
        }
        oldFileMoved = true;
    }

    if(!LittleFS.rename(TEMP_MEASUREMENT_FILE, MEASUREMENT_FILE)){
        if(oldFileMoved){
            if(!LittleFS.rename(BACKUP_MEASUREMENT_FILE, MEASUREMENT_FILE)){
                Serial.println("ERROR:FILE_RESTORE_FAILED");
                return false;
            }
        }
        
        Serial.println("ERROR:NEW_FILE_RENAME_FAILED");
        return false;
    }

    if(oldFileMoved){
        if(!LittleFS.remove(BACKUP_MEASUREMENT_FILE)){
            Serial.println("ERROR:BACKUP_CLEANUP_FAILED");
            return false;
        }
    }

    Serial.println("Novo arquivo de medicoes criado com sucesso!");
    return true;
}

