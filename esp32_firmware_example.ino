/*
 * SMARTFOOT ESP32 Real Hardware Firmware Sketch (Arduino IDE)
 * Reads 3 FSR analog sensors (GPIO 32, 33, 34) and DS18B20 temperature (GPIO 4)
 * and transmits telemetry to the SmartFoot Tele-monitoring Backend API.
 * 
 * Hardware Pinout Configuration:
 * - FSR 1 (Heel - R1): GPIO 32
 * - FSR 2 (Midfoot/Arch - R2): GPIO 33
 * - FSR 3 (Forefoot/Big Toe - R3): GPIO 34
 * - DS18B20 Temperature Sensor (T1): GPIO 4 (1-Wire Bus)
 * - Remaining zone (Lateral Forefoot - R4): 0.0 kPa (Unused)
 * 
 * Required Libraries (Arduino IDE Library Manager):
 * - OneWire (by Paul Stoffregen)
 * - DallasTemperature (by Miles Burton)
 * - ArduinoJson (by Benoit Blanchon - version 6 or 7)
 * - HTTPClient (built-in for ESP32)
 * - WiFi (built-in for ESP32)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// -------------------------------------------------------------
// 1. WiFi & Server Configuration
// -------------------------------------------------------------
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Replace with your laptop/server IP address on your local network
// (e.g. http://192.168.1.100:5000/api/v1/telemetry)
const char* SERVER_URL    = "http://192.168.1.100:5000/api/v1/telemetry";

// Device Token assigned to Sita Devi (Patient 1) in SmartFoot database
const char* DEVICE_TOKEN  = "dev-token-sita-101";

// -------------------------------------------------------------
// 2. FSR & Temperature Sensor Pin Setup
// -------------------------------------------------------------
#define FSR1_PIN 32 // Heel (R1 -> R_heel)
#define FSR2_PIN 33 // Arch (R2 -> R_arch)
#define FSR3_PIN 34 // Big Toe (R3 -> R_toe)

#define ONE_WIRE_BUS 4 // DS18B20 Data Pin (1-Wire Bus)

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Helper: Convert raw ADC analog reading (0 - 4095) to estimated pressure (0.0 - 100.0 kPa)
float adcToKpa(int adcValue) {
  if (adcValue < 50) return 0.0;
  float kPa = (adcValue / 4095.0) * 100.0; // Scaled 0.0 - 100.0 kPa
  return kPa;
}

void setup() {
  Serial.begin(9600);
  delay(1000);

  // Start DS18B20 Temperature Sensor
  sensors.begin();

  Serial.println("================================");
  Serial.println("ESP32 FSR + Temperature Sensor");
  Serial.println("================================");
  Serial.println();

  // Connect to WiFi network
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connected successfully!");
  Serial.print("[WiFi] ESP32 IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}

void loop() {
  // -------- Read FSR sensors --------
  int fsr1 = analogRead(FSR1_PIN);
  int fsr2 = analogRead(FSR2_PIN);
  int fsr3 = analogRead(FSR3_PIN);

  // -------- Read temperature --------
  sensors.requestTemperatures();
  float temperature = sensors.getTempCByIndex(0);

  // Fallback if temperature sensor is disconnected or out of range
  if (temperature == DEVICE_DISCONNECTED_C || temperature < -20.0 || temperature > 85.0) {
    temperature = 25.0; // Default ambient temperature baseline
  }

  // -------- Display readings --------
  Serial.println("--------------------------------");
  Serial.print("FSR 1 (GPIO 32): ");
  Serial.println(fsr1);
  Serial.print("FSR 2 (GPIO 33): ");
  Serial.println(fsr2);
  Serial.print("FSR 3 (GPIO 34): ");
  Serial.println(fsr3);
  Serial.print("Temperature: ");
  Serial.print(temperature, 2);
  Serial.println(" C");

  // -------- Convert ADC to kPa for backend --------
  float kpa_heel = adcToKpa(fsr1);
  float kpa_arch = adcToKpa(fsr2);
  float kpa_toe  = adcToKpa(fsr3);
  float kpa_met  = 0.0; // Unused 4th zone set to 0.0 kPa

  // -------- Send HTTP POST to SmartFoot Backend API --------
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Token", DEVICE_TOKEN);

    StaticJsonDocument<512> doc;
    doc["device_token"] = DEVICE_TOKEN;
    doc["is_simulated"] = false; // Real hardware flag

    JsonObject p_zones = doc.createNestedObject("pressure_zones");
    p_zones["R_heel"] = kpa_heel;
    p_zones["R_arch"] = kpa_arch;
    p_zones["R_toe"]  = kpa_toe;
    p_zones["R_met"]  = kpa_met;

    doc["temperature_right"] = temperature;
    doc["temperature_left"]  = temperature;

    JsonObject gait = doc.createNestedObject("gait_data");
    gait["cadence"]   = 100;
    gait["asymmetry"] = 0.0;
    gait["impact_g"]  = 1.0;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      Serial.print("[Backend Sync] Data sent! HTTP Response Code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("[Backend Sync] POST failed, Error Code: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("[WiFi Error] Disconnected. Reconnecting...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  // Sample and send data every 2 seconds
  delay(2000);
}

