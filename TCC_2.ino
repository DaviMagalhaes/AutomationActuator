#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"          //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <FS.h>

/* TOPICS:
 *  Message input:  <MAC_address>/in
 *  Message output: <MAC_address>/out
 *  
 * QOS:
 *   1
 */

// ###################################################################### DEFINIÇÕES

// WI-FI configurações do ponto de acesso
#define SSID_THIS "HomeAutomation"
#define PW_THIS   "12345678"
#define MINIMUM_SIGNAL 30

// MQTT configurações
#define MQTT_PORT       1883
#define MQTT_IN         "/in"
#define MQTT_OUT        "/out"
String mqttMacAddress = WiFi.softAPmacAddress();
String mqttServer;

// Portas
#define RELAY_PORT  D1
#define SENSOR_PORT A0

// Flag para o status da ENERGIA
bool currentPower;

// Chaves para referências
#define ON   '1'
#define OFF  '0'
#define SYNC 's'
#define TYPE_LIGHT 'l'
#define TYPE_PLUG 'p'

#define FILE_SERVER "/server.txt"
#define FILE_TYPE   "/type.txt"

// Instancias Globais
WiFiClient espClient;
PubSubClient mqtt(espClient);
String moduleType;

// ###################################################################### WIFIMANAGER

// Conexão prévia falhou
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("WifiManager entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

// Conectado com sucesso
void saveConfigCallback () {
  Serial.println("WifiManager should save config");
}

// ###################################################################### MQTT

// Callback MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Debugging
  Serial.print("MQTT message received [");
  Serial.print(topic);
  Serial.print("]: ");
  for(int i=0; i<length; i++)
    Serial.print((char) payload[i]);
  Serial.println();

  // Verificar mensagem
  if(length != 1) {
    Serial.println("MQTT inconsistent message");
    return;
  }

  // Executar a instrução da mensagem
  switch(payload[0]) {
    case ON:
    case OFF:
      // Função ligar/desligar
      Serial.print("Energy status instruction: ");
      Serial.println((char) payload[0]);
      funcPower(payload[0] == ON ? true : false);
      break;
      
    case SYNC: {
      // Enviar estado atual para sincronia
      String powerMsg = powerStatus() ? "1" : "0";
      Serial.print("Get power's status: ");
      Serial.println(powerMsg);
      
      String topic = mqttMacAddress + MQTT_OUT;
      mqtt.publish(topic.c_str(), powerMsg.c_str());
      
      Serial.print("MQTT publish [");
      Serial.print(topic);
      Serial.print("]: ");
      Serial.println(powerMsg);
    } break;
      
    default:
      Serial.println("Unknown instruction");
      break;
  }
}

// Reconectar ao servidor MQTT
void mqttReconnect() {
  // Criar um ID para cliente baseado no endereço MAC
  String clientId = WiFi.softAPmacAddress();
    
  while(!mqtt.connected()) {
    Serial.print("Attempting MQTT connection... ");
    
    // Tentativa de conexão
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("MQTT connected");

      // Se inscreve no tópico para receber mensagens
      String topic = mqttMacAddress + MQTT_IN;
      mqtt.subscribe(topic.c_str());
      Serial.print("MQTT subscribe topic: ");
      Serial.println(topic);
    } else {
      Serial.print("MQTT failed, rc=");
      Serial.print(mqtt.state());
      Serial.println("MQTT try again in 5 seconds");
      delay(5000);
    }
  }
}

// ###################################################################### FUNÇÕES

// Função do módulo / Alterar ENERGIA
void funcPower(bool cmd) {
  if(cmd != powerStatus())
    digitalWrite(RELAY_PORT, !digitalRead(RELAY_PORT));
}

// Pegar status da ENERGIA
bool powerStatus() {
  // DAVI: https://www.youtube.com/watch?v=GBySmlfuKmg

  switch(moduleType[0]) {
    case TYPE_LIGHT:
      // Iluminação
      pinMode(D2, INPUT);
      return digitalRead(D2);
    case TYPE_PLUG:
      // Tomada
      return !digitalRead(RELAY_PORT);
    default:
      Serial.println("Unknown module type. Power status false.");
      return false;
  }
}

// Verificar mudança de ENERGIA
void checkPowerStatus() {
  bool checkStatus = powerStatus();

  if(currentPower != checkStatus) {
    currentPower = checkStatus;
    
    String message = currentPower ? "1" : "0";
    String topic = mqttMacAddress + MQTT_OUT;
    mqtt.publish(topic.c_str(), message.c_str());

    Serial.print("Update the energy status: ");
    Serial.println(message);
    Serial.print("MQTT publish [");
    Serial.print(topic);
    Serial.print("]: ");
    Serial.println(message);
  }
}

// ###################################################################### PRINCIPAL

// SETUP
void setup() {
  // Comunicação Serial
  Serial.begin(115200);
  Serial.println("\nStarting...");

  // Iniciar portas
  pinMode(RELAY_PORT, OUTPUT);
  pinMode(SENSOR_PORT, INPUT);
  digitalWrite(RELAY_PORT, LOW);

  // Memória: endereço prévio do servidor MQTT e do tipo do módulo
  Serial.println("Reading memory to previous configuration...");
  SPIFFS.begin();
  File fServer;
  File fType;
  if(!SPIFFS.exists(FILE_SERVER)) {
    fServer = SPIFFS.open(FILE_SERVER, "w+");
    fServer.print('\n');
    fServer.close();
  }
  if(!SPIFFS.exists(FILE_TYPE)) {
    fType = SPIFFS.open(FILE_TYPE, "w+");
    fType.print('\n');
    fType.close();
  }
  fServer = SPIFFS.open(FILE_SERVER, "r+");
  fType = SPIFFS.open(FILE_TYPE, "r+");
  mqttServer = fServer.readStringUntil('\n');
  moduleType = fType.readStringUntil('\n');
  fServer.close();
  fType.close();

  Serial.print("Previous MQTT server address: ");
  Serial.println(mqttServer.length() != 0 ? mqttServer : "empty");
  Serial.print("Previous module type: ");
  Serial.println(moduleType.length() != 0 ? moduleType : "empty");
  
  // Receber configurações por GET
  WiFiManagerParameter toGetServerMqtt("m", "", mqttServer.c_str(), 51);
  WiFiManagerParameter toGetModuleType("t", "", moduleType.c_str(), 2);
  
  // WiFiManager
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setMinimumSignalQuality(10);
  wifiManager.addParameter(&toGetServerMqtt);
  wifiManager.addParameter(&toGetModuleType);

  // Definir endereço IP para ponto de acesso: 10.0.1.1
  wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  // Criar ponto de acesso caso não existam configurações anteriores
  if(!wifiManager.autoConnect(SSID_THIS, PW_THIS)) {
    Serial.println("WifiManager failed to connect and hit timeout");
    ESP.reset();
    delay(1000);
  }

  // Conectado a rede WI-FI
  Serial.println("Connected WIFI");

  // Configurar definições
  mqttServer = toGetServerMqtt.getValue();
  moduleType = toGetModuleType.getValue();
  Serial.print("Current MQTT server address: ");
  Serial.println(mqttServer);
  Serial.print("Current type module: ");
  Serial.println(moduleType);
  
  mqtt.setServer(mqttServer.c_str(), MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  // Memória: salvar configurações
  fServer = SPIFFS.open(FILE_SERVER, "r+");
  fType = SPIFFS.open(FILE_TYPE, "r+");
  fServer.print(mqttServer +'\n');
  fType.print(moduleType +'\n');
  fServer.close();
  fType.close();
  SPIFFS.end();

  // Atualizar o status inicial da ENERGIA
  currentPower = powerStatus();
}

// LOOP
void loop() {
  if(!mqtt.connected()) {
    mqttReconnect();
  }
  mqtt.loop();
  checkPowerStatus();
}

