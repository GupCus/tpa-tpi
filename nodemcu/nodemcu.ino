// ===================================================
// NODEMCU - solo hace de puente entre thingsboard y arduino
// TP IoT - Control PI LED - UTN FRRO ISI 4K01
// Agustín Barroso Bollero - 52818
// Victoria Caracchi - 53482
// Lautaro Ponce - 52898
// Tomás Ramos - 52216
// Irina Repupilli - 52417
// Enrico Reschini - 52973
// ===================================================

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>

// Teniamos conflicto con la comunicación del arduino, tuvimos que especificar otros dos puertos
SoftwareSerial arduinoSerial(D1, D2);


const char* ssid = " wifi ";
const char* password = " contrasena ";


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
  
  // Procesamos lo recibido
  for (int i = 0; i < 11; i++) {
    int coma = data.indexOf(',');
    
    if (coma == -1) {
      // Si ya no hay más comas, este es el último dato
      values[i] = data;
      values[i].trim();
      break;
    }
    
    // Extraemos hasta la posición de la coma
    values[i] = data.substring(0, coma);
    values[i].trim();
    
    // Descartamos lo que ya guardamos y nos quedamos con el resto
    data = data.substring(coma + 1);
  }
  
  // Armamos el JSON con los datos
  StaticJsonDocument<256> doc;
  doc["y"] = values[0].toInt();             // Iluminación (ui) 
  doc["y_percent"] = values[1].toFloat();   // Iluminación (%) 
  doc["r"] = values[2].toInt();             // Setpoint (ui) 
  doc["r_percent"] = values[3].toFloat();   // Setpoint (%) 
  doc["u"] = values[4].toInt();             // PWM Bruto 
  doc["u_percent"] = values[5].toFloat();   // PWM Aplicado 
  doc["kp"] = values[6].toFloat();          // KP Actual 
  doc["ki"] = values[7].toFloat();          // KI Actual 
  doc["e"] = values[8].toFloat();           // Error
  doc["e_percent"] = values[9].toFloat();   // Error (%) 
  doc["mode"] = values[10];                 // Modo de control
  
  //junta todo y lo envía a tb
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  if (client.publish("v1/devices/me/telemetry", jsonBuffer)) {
    Serial.print("[DATOS ENVIADOS A THINGSBOARD]: ");
    Serial.println(jsonBuffer);
  }
}

//metodo para agarrar lo que manda tb y pasarlo al arduino
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

//metodo para la conexion, cuando se conecta envía una señal de vida
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
  
  Serial.println("[STATUS] -> PUENTE LISTO");
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
        int comaCount = 0;
        for (int i = 0; i < arduinoData.length(); i++) {
          if (arduinoData[i] == ',') comaCount++;
        }
        
        if (comaCount == 10) {
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