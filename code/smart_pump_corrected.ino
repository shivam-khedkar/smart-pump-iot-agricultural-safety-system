/*
  Smart Pump – IoT Based Agricultural Safety System
  Corrected/reconstructed from the project report code.

  Hardware:
  - ESP8266 NodeMCU
  - L298N motor driver
  - DC water pump
  - Safety/status inputs
  - Wi-Fi web control and monitoring

  IMPORTANT:
  Sensor input logic and voltage-divider calibration must be adjusted
  to match the actual hardware used in the prototype.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

// -------------------- Wi-Fi --------------------
const char* sta_ssid = "";       // Enter Wi-Fi name
const char* sta_password = "";   // Enter Wi-Fi password

ESP8266WebServer server(80);

// -------------------- Pin configuration --------------------
// L298N: ENA/PWM, IN1, IN2
const uint8_t PUMP_PWM = D1;
const uint8_t PUMP_IN1 = D3;
const uint8_t PUMP_IN2 = D4;

// Status indicators
const uint8_t BUZZER_PIN = D5;
const uint8_t WIFI_LED_PIN = D0;

// Safety/status inputs
// Change these pins/logic to match the actual sensors in your prototype.
const uint8_t WATER_LEVEL_PIN = D6;
const uint8_t OVERFLOW_PIN    = D7;
const uint8_t OVERLOAD_PIN    = D2;

// Analog voltage-monitoring input.
// ESP8266 has one analog input (A0).
const uint8_t VOLTAGE_PIN = A0;

// -------------------- Settings --------------------
const uint8_t PUMP_SPEED = 850;          // ESP8266 PWM range: 0–1023
const bool SENSOR_ACTIVE_LOW = true;

bool pumpRequested = false;
bool faultActive = false;

float measuredVoltage = 0.0;

// Adjust this after measuring your actual voltage-divider output.
// Example: if 12 V becomes about 3.0 V at A0, the ratio is about 4.0.
const float ADC_REFERENCE = 3.3;
const float VOLTAGE_DIVIDER_RATIO = 4.0;

// -------------------- Helper functions --------------------
bool inputActive(uint8_t pin) {
  int state = digitalRead(pin);
  return SENSOR_ACTIVE_LOW ? (state == LOW) : (state == HIGH);
}

void stopPump() {
  analogWrite(PUMP_PWM, 0);
  digitalWrite(PUMP_IN1, LOW);
  digitalWrite(PUMP_IN2, LOW);
}

void startPump() {
  digitalWrite(PUMP_IN1, HIGH);
  digitalWrite(PUMP_IN2, LOW);
  analogWrite(PUMP_PWM, PUMP_SPEED);
}

void beep(uint16_t durationMs = 150) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);
}

void readVoltage() {
  int adcValue = analogRead(VOLTAGE_PIN);

  // Convert ADC reading to input voltage using the configured divider ratio.
  float adcVoltage = (adcValue / 1023.0) * ADC_REFERENCE;
  measuredVoltage = adcVoltage * VOLTAGE_DIVIDER_RATIO;
}

void checkSafety() {
  bool waterLevelUnsafe = inputActive(WATER_LEVEL_PIN);
  bool overflowUnsafe   = inputActive(OVERFLOW_PIN);
  bool overloadUnsafe   = inputActive(OVERLOAD_PIN);

  faultActive = waterLevelUnsafe || overflowUnsafe || overloadUnsafe;

  if (faultActive) {
    stopPump();

    if (pumpRequested) {
      beep();
    }

    pumpRequested = false;
  }
}

String getStatusText() {
  if (faultActive) {
    return "FAULT";
  }

  return pumpRequested ? "ON" : "OFF";
}

// -------------------- Web page --------------------
void handleRoot() {
  readVoltage();
  checkSafety();

  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Smart Pump</title>";
  html += "<style>";
  html += "body{font-family:Arial;text-align:center;margin:30px;background:#f5f5f5;}";
  html += ".card{max-width:500px;margin:auto;background:white;padding:25px;border-radius:15px;}";
  html += "button{padding:14px 25px;margin:8px;font-size:18px;border:0;border-radius:8px;}";
  html += ".on{background:#28a745;color:white}.off{background:#dc3545;color:white}";
  html += "</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>Smart Pump</h1>";
  html += "<h2>Status: " + getStatusText() + "</h2>";
  html += "<p>Monitored Voltage: " + String(measuredVoltage, 2) + " V</p>";

  if (faultActive) {
    html += "<p><b>Safety fault detected. Pump is stopped.</b></p>";
  }

  html += "<a href='/pump/on'><button class='on'>PUMP ON</button></a>";
  html += "<a href='/pump/off'><button class='off'>PUMP OFF</button></a>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// -------------------- Pump control --------------------
void handlePumpOn() {
  readVoltage();
  checkSafety();

  if (faultActive) {
    server.send(409, "text/plain",
                "Pump cannot start: safety fault is active.");
    return;
  }

  pumpRequested = true;
  startPump();

  server.sendHeader("Location", "/");
  server.send(303);
}

void handlePumpOff() {
  pumpRequested = false;
  stopPump();

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStatus() {
  readVoltage();
  checkSafety();

  String json = "{";
  json += "\"pump\":\"" + getStatusText() + "\",";
  json += "\"fault\":" + String(faultActive ? "true" : "false") + ",";
  json += "\"voltage\":" + String(measuredVoltage, 2);
  json += "}";

  server.send(200, "application/json", json);
}

// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("Smart Pump - IoT Based Agricultural Safety System");
  Serial.println("------------------------------------------------");

  // Output pins
  pinMode(PUMP_PWM, OUTPUT);
  pinMode(PUMP_IN1, OUTPUT);
  pinMode(PUMP_IN2, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(WIFI_LED_PIN, OUTPUT);

  // Safety inputs
  pinMode(WATER_LEVEL_PIN, INPUT_PULLUP);
  pinMode(OVERFLOW_PIN, INPUT_PULLUP);
  pinMode(OVERLOAD_PIN, INPUT_PULLUP);

  // Safe initial state
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(WIFI_LED_PIN, HIGH);
  stopPump();

  // -------------------- Wi-Fi --------------------
  WiFi.mode(WIFI_STA);

  if (strlen(sta_ssid) > 0) {
    WiFi.begin(sta_ssid, sta_password);

    Serial.print("Connecting to Wi-Fi");

    unsigned long startAttempt = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttempt < 10000) {
      delay(500);
      Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Wi-Fi connected.");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      digitalWrite(WIFI_LED_PIN, LOW);
    } else {
      Serial.println("Wi-Fi connection failed.");
    }
  } else {
    Serial.println("Wi-Fi credentials are empty.");
    Serial.println("Enter your Wi-Fi SSID and password in the code.");
  }

  // -------------------- Web server --------------------
  server.on("/", handleRoot);
  server.on("/pump/on", handlePumpOn);
  server.on("/pump/off", handlePumpOff);
  server.on("/status", handleStatus);

  server.onNotFound([]() {
    server.send(404, "text/plain", "Page not found");
  });

  server.begin();
  Serial.println("Web server started.");

  // OTA firmware updates
  ArduinoOTA.setHostname("smart-pump");
  ArduinoOTA.begin();

  Serial.println("OTA update service started.");
}

// -------------------- Main loop --------------------
void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  // Continuously monitor safety conditions.
  checkSafety();

  // Stop pump immediately if a fault occurs.
  if (faultActive) {
    stopPump();
  } else if (pumpRequested) {
    startPump();
  }

  delay(20);
}
