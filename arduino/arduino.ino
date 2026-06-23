// ===================================================
// TP IoT - Control PI LED - UTN FRRO ISI 4K01
// Agustín Barroso Bollero - 52818
// Victoria Caracchi - 53482
// Lautaro Ponce - 52898
// Tomás Ramos - 52216
// Irina Repupilli - 52417
// Enrico Reschini - 52973
// ===================================================

// PINES
const byte ledPin = 9;
const byte ldrPin = A0;

// Parámetros base
float Kp = 0.08;
float Ki = 0.8;
const float Ts = 0.02; 

float integral = 255.0 / Ki; // Pre-carga para arranque suave 
int setpoint = 600;          // Setpoint inicial
String modoControl = "AUTO";

// tiempos
unsigned long lastControlTime = 0;
unsigned long lastLogTime = 0;

// variables de control
int yCrudo = 0;
float uAccion = 0.0;
float errorActual = 0.0;

void setup() 
{
    Serial.begin(9600);
    pinMode(ledPin, OUTPUT);
}

void loop() 
{
    // Escuchar comandos RPC desde la nube (Setpoint, Kp, Ki, Modo)
    procesarComandosRPC();

    if (millis() - lastControlTime >= 20) {
        lastControlTime = millis();
        ejecutarControlPI();
                
        if (millis() - lastLogTime >= 1000) {
            lastLogTime = millis();
            enviarTelemetriaCSV();
        }
    }
}

// ===================================================
// PROCESAMIENTO DE COMANDOS 
// ===================================================
void procesarComandosRPC() 
{
    static String bufferComando = ""; 

    while (Serial.available() > 0) {
        char c = Serial.read();
        
        if (c == '\n') {
            bufferComando.trim();
            
            int colonIndex = bufferComando.indexOf(':');
            if (colonIndex != -1) {
                String metodo = bufferComando.substring(0, colonIndex);
                String parametro = bufferComando.substring(colonIndex + 1);
                
                if (metodo == "setSetpoint")       setpoint = parametro.toInt();
                else if (metodo == "setKp")        Kp = parametro.toFloat();
                else if (metodo == "setKi")        Ki = parametro.toFloat();
                else if (metodo == "setMode")      modoControl = parametro;
                else if (metodo == "setPWM" && modoControl == "MANUAL")       uAccion = parametro.toInt(); 
                else if (metodo == "simularEscalon")       uAccion = 255; 
                else if (metodo == "reset"){
                    Kp = 0.08;
                    Ki = 0.8;
                }
                
                // Responder confirmación limpia al NodeMCU, aunque te confirma para todo metodo sin funcionamiento
                Serial.print("OK_RES: Comando ");
                Serial.print(metodo);
                Serial.println(" ejecutado con exito");
            }
            bufferComando = "";
        } 
        else if (c != '\r') {
            bufferComando += c; 
        }
    }
}

// ===================================================
// Controlador pi
// ===================================================
void ejecutarControlPI() 
{
    // Lectura de la planta analógica
    yCrudo = analogRead(ldrPin);

    // Cálculo del error del lazo
    errorActual = setpoint - yCrudo;

    if(modoControl != "MANUAL"){
    // Ecuación del controlador PI
        integral += errorActual * Ts;
        uAccion = Kp * errorActual + Ki * integral;

    // Algoritmo para evitar saturación del controlador
        if (uAccion > 255) {
            uAccion = 255;
            integral -= errorActual * Ts; 
        }
        else if (uAccion < 0) {
            uAccion = 0;
            integral -= errorActual * Ts; 
        }
    } else {
       // Para evitar excesos manuales
        if (uAccion > 255) {
            uAccion = 255;
        }
        else if (uAccion < 0) {
            uAccion = 0;
        }
    }

    // Escritura en el actuador (PWM)
    analogWrite(ledPin, (int)uAccion);
}

// ===================================================
// telemtría
// ===================================================
void enviarTelemetriaCSV() 
{
    // Conversión de variables a rangos porcentuales
    float y_percent = (yCrudo / 1023.0) * 100.0;
    float r_percent = (setpoint / 1023.0) * 100.0;
    float u_percent = (uAccion / 255.0) * 100.0;
    float e_percent = (errorActual / 1023.0) * 100.0;

    // datos enviados por serial
    Serial.print(yCrudo);             Serial.print(","); // [0]  -> Key: y
    Serial.print(y_percent, 2);  Serial.print(","); // [1]  -> Key: y_percent
    Serial.print(setpoint);      Serial.print(","); // [2]  -> Key: r
    Serial.print(r_percent, 2);  Serial.print(","); // [3]  -> Key: r_percent
    Serial.print((int)uAccion);  Serial.print(","); // [4]  -> Key: u
    Serial.print(u_percent, 2);  Serial.print(","); // [5]  -> Key: u_percent
    Serial.print(Kp, 4);         Serial.print(","); // [6]  -> Key: kp
    Serial.print(Ki, 4);         Serial.print(","); // [7]  -> Key: ki
    Serial.print(errorActual, 2); Serial.print(","); // [8]  -> Key: e
    Serial.print(e_percent, 2);  Serial.print(","); // [9]  -> Key: e_percent
    Serial.println(modoControl);                    // [10] -> Key: mode
}