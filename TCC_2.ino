#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"          //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <FS.h>

/* TOPICS:
 *  Message to a mobile device:
 *      <clientId>/<token module>/<in OR out>
 *      token: MAC address;
 *         in:   subscribe;
 *        out:   publish;
 *
 *  Message to sync all:
 *      <clientId>/ALL
 * QOS:
 *   1
 */

// ###################################################################### DEFINIÇÕES

// WI-FI configurações do ponto de acesso
#define SSID_THIS "HomeAutomation"
#define PW_THIS   "12345678"
#define MINIMUM_SIGNAL 10

// MQTT configurações
#define MQTT_PORT            1883
String mqttTopicToken      = WiFi.softAPmacAddress();
String mqttTopicAll        = "ALL";
String mqttTopicBase;
String mqttTopicClientId;
String mqttServer;

// Portas
#define RELAY_PORT  D1
#define SENSOR_PORT A0

// Flag para o status da ENERGIA
bool currentPower;

// Chaves para referências
#define ON  '1'
#define OFF '0'

#define FILE_SERVER "/server.txt"
#define FILE_CLIENT "/client.txt"

// Instanciar WI-FI and MQTT
WiFiClient espClient;
PubSubClient mqtt(espClient);

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
  for(int i=0; i<length; i++) {
    Serial.print((char) payload[i]);
  }
  Serial.println();

  // Verificar mensagem
  if(length != 1) {
    Serial.println("MQTT inconsistent message");
    return;
  }
  Serial.println("Executing instruction...");

  // Executar a instrução da mensagem
  String topicAll = mqttTopicClientId+"/"+mqttTopicAll;
  if(strcmp(topic, topicAll.c_str()) != 0) {
    // Função ligar/desligar
    Serial.println("Function status: " + payload[0]);
    funcPower(payload[0] == ON ? true : false);
  } else {
    // Get status
    String powerMsg = powerStatus() ? "1" : "0";
    
    Serial.println("Get power's status: " + powerMsg);
    String topic = mqttTopicBase +"/out";
    mqtt.publish(topic.c_str(), powerMsg.c_str());
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

      // Se inscreve no tópico para ESTE MÓDULO
      String topic = mqttTopicBase +"/in";
      mqtt.subscribe(topic.c_str());
      Serial.println("MQTT subscribe topic: " + topic);

      // Se inscreve no tópico para TODOS OS MÓDULO DO USUÁRIO
      topic = mqttTopicClientId +"/"+ mqttTopicAll;
      mqtt.subscribe(topic.c_str());
      Serial.println("MQTT subscribe topic: " + topic);
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
  return false;
}

// Verificar mudança de ENERGIA
void checkPowerStatus() {
  bool checkStatus = powerStatus();

  if(currentPower != checkStatus) {
    currentPower = checkStatus;
    
    String message = currentPower ? "1" : "0";
    String topic = mqttTopicBase +"/out";
    mqtt.publish(topic.c_str(), message.c_str());

    Serial.println("Update the energy status: " + message);
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

  // Memória: ler configurações prévias para MQTT
  Serial.println("Reading memory to previous configuration...");
  SPIFFS.begin();
  File fServer;
  File fClient;
  if(!SPIFFS.exists(FILE_SERVER)) {
    fServer = SPIFFS.open(FILE_SERVER, "w+");
    fServer.print('\n');
    fServer.close();
  }
  if(!SPIFFS.exists(FILE_CLIENT)) {
    fClient = SPIFFS.open(FILE_CLIENT, "w+");
    fClient.print('\n');
    fClient.close();
  }
  fServer = SPIFFS.open(FILE_SERVER, "r+");
  fClient = SPIFFS.open(FILE_CLIENT, "r+");
  mqttServer        = fServer.readStringUntil('\n');
  mqttTopicClientId = fClient.readStringUntil('\n');
  fServer.close();
  fClient.close();

  Serial.println("Previous MQTT Server Address: " + mqttServer);
  Serial.println("Previous MQTT Client ID: " + mqttTopicClientId);
  
  // Receber configurações para MQTT por GET
  WiFiManagerParameter toGetServerMqtt("m", "", mqttServer.c_str(), 51);
  WiFiManagerParameter toGetClientId("c", "", mqttTopicClientId.c_str(), 12);
  
  // WiFiManager
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setMinimumSignalQuality(10);
  wifiManager.addParameter(&toGetClientId);
  wifiManager.addParameter(&toGetServerMqtt);

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

  // Configurar definições MQTT
  mqttServer = toGetServerMqtt.getValue();
  mqttTopicClientId = toGetClientId.getValue();
  Serial.println("Current MQTT Server Address: " + mqttServer);
  Serial.println("Current MQTT Topic Client ID: " + mqttTopicClientId);
  
  mqtt.setServer(mqttServer.c_str(), MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqttTopicBase = mqttTopicClientId +"/"+ mqttTopicToken;

  // Memória: salvar configurações para MQTT
  fServer = SPIFFS.open(FILE_SERVER, "r+");
  fClient = SPIFFS.open(FILE_CLIENT, "r+");
  fServer.print(mqttServer);
  fClient.print(mqttTopicClientId);
  fServer.close();
  fClient.close();
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

