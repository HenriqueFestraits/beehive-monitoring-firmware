#include <ScioSense_ENS160.h>
#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <LittleFS.h>

#define SDA_PIN 22
#define SCL_PIN 23
#define SLEEP_TIME 120

Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2CADDR_0);


void setup() {
  //estabelecendo comunicacao
  Serial.begin(115200);
  delay(1000);

  //iniciando esp32
  Serial.println("Iniciando programa...");
  Wire.begin(SDA_PIN, SCL_PIN);

  //iniciando o file system
  if(!LittleFS.begin(true)){
    Serial.println("ERRO: Falha ao iniciar LittleFS!");
  }else{
    Serial.println("LittleFS iniciado com sucesso!");
  }

  File arquivo = LittleFS.open("/medicoes.bin", FILE_WRITE);

  if(!arquivo){
    Serial.println("ERRO: nao foi possivel criar o arquivo!");
  }else{
    Serial.println("Arquivo criado e escrito com sucesso!");
  }

  //iniciando aht21
  if(!aht.begin()){
    Serial.println("ERRO: AHT21 nao encontrado!");
    while(1) delay(100);
  }
  
  Serial.println("AHT21 econtrado!");


  //iniciando ens160
  if(ens160.begin() != 0){
    Serial.println("Erro: ENS160 nao encontrado!");
    while(1) delay(100);
  }

  Serial.println("ENS160 encontrado!");

  //configurando ens160
  if(ens160.setMode(ENS160_OPMODE_STD) == 0){
    Serial.println("ENS160 em modo padrao");
  }else{
    Serial.println("ERRO em configurar ENS160 em modo padrao!");
  }

  //iniciando leitura em espera para instabilizacao do ens160 (aprox. 3min)
  Serial.println("-----------------------------------");
  Serial.println("Aguardando estabilizacao do ENS160...");

}

void loop() {

  //ambiente aht21
  sensors_event_t humidity;
  sensors_event_t temperature;

  aht.getEvent(&humidity, &temperature);

  float temp = temperature.temperature;
  float hum = humidity.relative_humidity;


  //ambiente ens160
  //ens160.set_envdata(temp, hum);//set de informacoes de contexto iniciais que aumentam precisao de leitura

  if (ens160.available()){
    ens160.measure(true);
  }else{
    Serial.println("ENS160: Aguardando nova medicao!");
  }





  //LEITURA

  Serial.println();
  Serial.println("================ LEITURA ================");

  Serial.print("Temperatura: ");
  Serial.print(temp);
  Serial.println(" °C");

  Serial.print("Umidade: ");
  Serial.print(hum);
  Serial.println(" %");

  Serial.print("eCO2: ");
  Serial.print(ens160.geteCO2());
  Serial.println(" ppm");

  Serial.print("TVOC: ");
  Serial.print(ens160.getTVOC());
  Serial.println(" ppb");

  Serial.print("AQI: ");
  Serial.println(ens160.getAQI());

  Serial.println("========================================");

  Serial.println("Entrando em Deep Sleep....");


  delay(5000);

  esp_sleep_enable_timer_wakeup(SLEEP_TIME * 1000000ULL);
  esp_deep_sleep_start();



}
