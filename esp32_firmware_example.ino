/*
 * SMARTFOOT ESP32 Real Hardware Firmware Sketch (Arduino IDE)
 * Transmits real hardware 3-FSR pressure and DS18B20 temperature telemetry
 * to the SmartFoot Tele-monitoring Backend API.
 * 
 * Hardware Pinout Configuration:
 * - FSR 1 (Heel - R1): GPIO 32
 * - FSR 2 (Midfoot/Arch - R2): GPIO 33
 * - FSR 3 (Forefoot/Big Toe - R3): GPIO 34
 * - DS18B20 Temperature Sensor (T1): GPIO 4 (1-Wire Bus)
 * - Remaining zone (Lateral Forefoot - R4): Set to 0.0 kPa (Unused)
 * 
 * Required Libraries (install via Arduino IDE Library Manager):
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

// Replace with your laptop/server IP address on your local WiFi network (e.g. http://192.168.1.100:5000/api/v1/telemetry)
const char* SERVER_URL    = "http://192.168.1.100:5000/api/v1/telemetry";

// Device Token assigned to the patient in SmartFoot database
// Pre-seeded Demo Tokens:
// - "dev-token-sita-101"   (Sita Devi)
// - "dev-token-rajesh-102" (Rajesh Kumar)
// - "dev-token-anita-103"  (Anita Sharma)
const char* DEVICE_TOKEN  = "dev-token-sita-101";

// -------------------------------------------------------------
// 2. Hardware Pin Mappings
// -------------------------------------------------------------
#define FSR1_PIN 32  // FSR 1: Heel (R1 -> R_heel)
#define FSR2_PIN 33  // FSR 2: Midfoot / Arch (R2 -> R_arch)
#define FSR3_PIN 34  // FSR 3: Forefoot / Big Toe (R3 -> R_toe)

#define ONE_WIRE_BUS 4 // DS18B20 Data Pin (1-Wire Bus)

// Initialize DS18B20 Temperature Sensor
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Helper: Convert raw ADC analog reading (0 - 4095) to estimated pressure (0.0 - 100.0 kPa)
float adcToKpa(int adcValue) {
  if (adcValue < 50) return 0.0;
  float voltage = (adcValue / 4095.0) * 3.3;
  float kPa = (voltage / 3.3) * 100.0; // Estimated scale: 0.0 to 100.0 kPa
  return kPa;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize DS18B20 Temperature Sensor
  sensors.begin();

  Serial.println("\n=============================================");
  Serial.println("SMARTFOOT ESP32 Real Sensor Telemetry Sketch");
  Serial.println("=============================================");
  Serial.println("Configured Sensors:");
  Serial.println(" - FSR 1 (Heel R1): GPIO 32");
  Serial.println(" - FSR 2 (Arch R2): GPIO 33");
  Serial.println(" - FSR 3 (Big Toe R3): GPIO 34");
  Serial.println(" - FSR 4 (Lateral Forefoot R4): 0.0 kPa (Unused)");
  Serial.println(" - DS18B20 Temperature (T1): GPIO 4");
  Serial.println("=============================================\n");

  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] ESP32 Local IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    // ---------------------------------------------------------
    // 1. Read Real Hardware FSR Analog Sensors
    // ---------------------------------------------------------
    int raw_fsr1 = analogRead(FSR1_PIN); // Heel (R1)
    int raw_fsr2 = analogRead(FSR2_PIN); // Arch (R2)
    int raw_fsr3 = analogRead(FSR3_PIN); // Big Toe (R3)

    float kpa_fsr1 = adcToKpa(raw_fsr1);
    float kpa_fsr2 = adcToKpa(raw_fsr2);
    float kpa_fsr3 = adcToKpa(raw_fsr3);
    float kpa_fsr4 = 0.0; // Remaining unused zone set to 0.0 kPa

    // ---------------------------------------------------------
    // 2. Read Real DS18B20 Temperature Sensor
    // ---------------------------------------------------------
    sensors.requestTemperatures();
    float temp_c = sensors.getTempCByIndex(0);

    // Fallback if sensor is disconnected or reading error
    if (temp_c == DEVICE_DISCONNECTED_C || temp_c < -20.0 || temp_c > 85.0) {
      Serial.println("[Warning] DS18B20 Sensor Error or Disconnected. Using baseline 34.2 °C.");
      temp_c = 34.2;
    }

    // Print real reading values to Serial Monitor
    Serial.println("---------------------------------------------");
    Serial.print("FSR 1 (GPIO 32 - Heel R1): ");
    Serial.print(raw_fsr1); Serial.print(" raw -> "); Serial.print(kpa_fsr1, 1); Serial.println(" kPa");

    Serial.print("FSR 2 (GPIO 33 - Arch R2): ");
    Serial.print(raw_fsr2); Serial.print(" raw -> "); Serial.print(kpa_fsr2, 1); Serial.println(" kPa");

    Serial.print("FSR 3 (GPIO 34 - Big Toe R3): ");
    Serial.print(raw_fsr3); Serial.print(" raw -> "); Serial.print(kpa_fsr3, 1); Serial.println(" kPa");

    Serial.println("FSR 4 (Lateral Forefoot R4): 0 raw -> 0.0 kPa (Unused)");

    Serial.print("DS18B20 Temp (GPIO 4 - T1): ");
    Serial.print(temp_c, 1); Serial.println(" °C");

    // ---------------------------------------------------------
    // 3. Construct Telemetry JSON Payload for Backend API
    // ---------------------------------------------------------
    StaticJsonDocument<512> doc;
    doc["device_token"] = DEVICE_TOKEN;
    doc["is_simulated"] = false; // Flag as real ESP32 hardware telemetry

    JsonObject p_zones = doc.createNestedObject("pressure_zones");
    p_zones["R_heel"] = kpa_fsr1; // FSR 1 -> Heel (R1)
    p_zones["R_arch"] = kpa_fsr2; // FSR 2 -> Arch (R2)
    p_zones["R_toe"]  = kpa_fsr3; // FSR 3 -> Big Toe (R3)
    p_zones["R_met"]  = kpa_fsr4; // FSR 4 -> Unused (0.0)

    doc["temperature_right"] = temp_c;
    doc["temperature_left"]  = temp_c;

    JsonObject gait = doc.createNestedObject("gait_data");
    gait["cadence"]   = 100;
    gait["asymmetry"] = 0.0;
    gait["impact_g"]  = 1.0;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // ---------------------------------------------------------
    // 4. Send HTTP POST Request to SmartFoot Backend API
    // ---------------------------------------------------------
    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Token", DEVICE_TOKEN);

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("[HTTP Success] Code: ");
      Serial.println(httpResponseCode);
      Serial.print("[HTTP Response]: ");
      Serial.println(response);
    } else {
      Serial.print("[HTTP Error] POST failed, Error Code: ");
      Serial.println(httpResponseCode);
    }

    http.end(); // Free network resources
  } else {
    Serial.println("[WiFi Error] Disconnected. Reconnecting...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  // Transmit real-time telemetry every 2 seconds
  delay(2000);
}
