// ===================================================
//   NODEMCU - PUENTE SERIAL-WIFI PARA THINGSBOARD
// ===================================================

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>

// Pines para comunicación exclusiva con Arduino
SoftwareSerial arduinoSerial(D1, D2);

const char* ssid = "HITRON-9C90";
const char* password = "RESUWH3NXY9B  ";

const char* mqttServer = "thingsboard.cloud";
const int mqttPort = 1883;
const char* accessToken = "wfxxf115zzzlqyusg1bl";

unsigned long lastTelemetryTime = 0;
const unsigned long telemetryInterval = 1000;

String arduinoData = "";

WiFiClient wifiClient;
PubSubClient client(wifiClient);

void setup_wifi() {
  delay(10);
  Serial.println("\n=== DIAGNOSTICO DE CONEXION ===");
  Serial.print("Conectando a: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);
  WiFi.begin(ssid, password);
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 100) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[STATUS] -> CONECTADO A INTERNET");
    Serial.print("[IP ASIGNADA]: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[ERROR] No se pudo conectar. Codigo de estado: ");
    switch (WiFi.status()) {
      case WL_NO_SSID_AVAIL:
        Serial.println("WL_NO_SSID_AVAIL -> El NodeMCU no encuentra ninguna red con ese nombre.");
        break;
      case WL_CONNECT_FAILED:
        Serial.println("WL_CONNECT_FAILED -> La contrasenia es incorrecta o la conexion fue rechazada.");
        break;
      case WL_IDLE_STATUS:
        Serial.println("WL_IDLE_STATUS -> El modulo esta cambiando de estado (espera un momento).");
        break;
      default:
        Serial.print("Codigo desconocido: ");
        Serial.println(WiFi.status());
        break;
    }
  }
}
void sendTelemetry(String data) {
  if (!client.connected()) return;
  
  String values[11];
  
  // Procesamos las primeras 10 columnas buscando las comas
  for (int i = 0; i < 11; i++) {
    int commaIndex = data.indexOf(',');
    
    if (commaIndex == -1) {
      // Si ya no hay más comas, este es el último dato (el modo de control)
      values[i] = data;
      values[i].trim();
      break;
    }
    
    // Extraemos desde el principio (0) hasta la posición de la coma
    values[i] = data.substring(0, commaIndex);
    values[i].trim();
    
    // Descartamos el pedazo que ya guardamos y nos quedamos con el resto
    data = data.substring(commaIndex + 1);
  }
  
  // Armamos el JSON con los datos perfectamente indexados
  StaticJsonDocument<256> doc;
  doc["y"] = values[0].toInt();             // Iluminación (ADC) -> 223
  doc["y_percent"] = values[1].toFloat();   // Iluminación (%) -> 21.80
  doc["r"] = values[2].toInt();             // Setpoint (ADC) -> 220
  doc["r_percent"] = values[3].toFloat();   // Setpoint (%) -> 21.51
  doc["u"] = values[4].toInt();             // PWM Bruto -> 0
  doc["u_percent"] = values[5].toFloat();   // PWM Aplicado -> 0.28
  doc["kp"] = values[6].toFloat();          // KP Actual -> 0.0800
  doc["ki"] = values[7].toFloat();          // KI Actual -> 0.8000
  doc["e"] = values[8].toFloat();           // Error -> -3.00
  doc["e_percent"] = values[9].toFloat();   // Error (%) -> -0.29
  doc["mode"] = values[10];                 // Modo de control -> AUTO
  
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  
  if (client.publish("v1/devices/me/telemetry", jsonBuffer)) {
    Serial.print("[DATOS ENVIADOS A THINGSBOARD]: ");
    Serial.println(jsonBuffer);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("[COMANDO RECIBIDO DE TB]: ");
  Serial.println(message);

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("[ERROR] Parseando JSON: ");
    Serial.println(error.c_str());
    return;
  }

  const char* method = doc["method"];
  String params = doc["params"].as<String>();
  
  String comando = String(method) + ":" + params;
  
  arduinoSerial.println(comando); 
  
  Serial.print("[REENVIADO A ARDUINO]: ");
  Serial.println(comando);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");
    if (client.connect("ESP8266Client", accessToken, NULL)) {
      Serial.println("\n[STATUS] -> CONECTADO A THINGSBOARD");
      
      // --- SEÑAL DE VIDA AL CONECTARSE ---
      StaticJsonDocument<64> aliveDoc;
      aliveDoc["alive"] = true;
      char aliveBuffer[64];
      serializeJson(aliveDoc, aliveBuffer);
      
      if (client.publish("v1/devices/me/telemetry", aliveBuffer)) {
        Serial.println("[STATUS] -> Mensaje de vida enviado: 'alive': true");
      }
      // ------------------------------------

      client.subscribe("v1/devices/me/rpc/request/+");
    } else {
      Serial.print("[FALLO] rc=");
      Serial.print(client.state());
      Serial.println(" Reintentando en 5 segundos...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);        
  arduinoSerial.begin(9600);   
  
  setup_wifi();
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  
  Serial.println("[STATUS] -> PUENTE LISTO Y OPERATIVO");
  Serial.println("----------------------------------------------");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  while (arduinoSerial.available() > 0) {
    char c = arduinoSerial.read();
    arduinoData += c;
    
    if (c == '\n') {
      arduinoData.trim();
      
      Serial.print("[DATOS RECIBIDOS DE ARDUINO]: ");
      Serial.println(arduinoData);
      
      if (arduinoData.startsWith("OK_RES")) { 
        Serial.print("  -> info: El Arduino confirmo la ejecucion de un comando.\n");
      } 
      else {
        int commaCount = 0;
        for (int i = 0; i < arduinoData.length(); i++) {
          if (arduinoData[i] == ',') commaCount++;
        }
        
        if (commaCount == 10) {
          sendTelemetry(arduinoData);
        } 
        else if (arduinoData.length() > 0 && !arduinoData.startsWith("===") && !arduinoData.startsWith("STATUS")) {
          Serial.println("  -> info: Trama no reconocida como telemetria completa.");
        }
      }
      
      Serial.println("----------------------------------------------");
      arduinoData = ""; 
    }
    
    yield(); 
  }
  
  if (millis() - lastTelemetryTime >= telemetryInterval) {
    lastTelemetryTime = millis();
  }
}