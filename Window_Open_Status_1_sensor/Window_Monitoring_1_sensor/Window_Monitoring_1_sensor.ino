#include <dummy.h>

// // Works on ESP8266 or ESP32
// #if defined(ESP32)
//   #include <WiFiMulti.h>
//   WiFiMulti wifiMulti;
//   #define DEVICE "ESP32"
// #elif defined(ESP8266)
//   #include <ESP8266WiFiMulti.h>
//   ESP8266WiFiMulti wifiMulti;
//   #define DEVICE "ESP8266"
// #endif

// #include <ESP8266WiFi.h>
// #include <ESP8266HTTPClient.h>
// #include <WiFiClient.h>

// //
// // ===== CONFIGURE THESE =====
// //
// #define WIFI_SSID    "Sensor monitoring"
// #define WIFI_PASS    "Bl@ckAngel@2023"

// // InfluxDB v1 settings (CentOS host)
// const char* INFLUX_HOST = "172.25.1.15";   // <<--- set to your InfluxDB IP or hostname
// const uint16_t INFLUX_PORT = 8086;          // default v1 port
// const char* INFLUX_DB = "window_monitoring"; // your DB name
// // If you created a write user, set them here. If not, leave empty ("")
// const char* INFLUX_USER = "";               // e.g. "esp"
// const char* INFLUX_PASS = "";               // e.g. "esp_password"

// // Tags for the measurement
// const char* HOST_TAG = "SHOPFLOOR";
// const char* LOCATION_TAG = "Window_1";

// // Measurement name (Influx line-protocol)
// const char* MEASUREMENT = "status";
// //
// // ===== END CONFIG =====
// //

// const int SWITCH_PIN = 2;      // as in your test (GPIO2 / D4 on NodeMCU)
// const unsigned long SEND_INTERVAL_MS = 30UL * 1000UL; // ensure at least this freq
// const unsigned long DEBOUNCE_MS = 50;

// unsigned long lastSend = 0;
// int lastLogical = -1;
// unsigned long lastBounce = 0;

// WiFiClient wifiClient;

// void setup() {
//   Serial.begin(115200);
//   delay(50);
//   pinMode(SWITCH_PIN, INPUT_PULLUP);  // using internal pull-up; switch to GND when closed

//   // Add your WiFi networks to wifiMulti (same as your R&D code)
//   wifiMulti.addAP(WIFI_SSID, WIFI_PASS);
//   Serial.print("Connecting to WiFi");
//   unsigned long start = millis();
//   while (wifiMulti.run() != WL_CONNECTED && millis() - start < 20000) {
//     Serial.print(".");
//     delay(500);
//   }
//   Serial.println();
//   if (WiFi.status() == WL_CONNECTED) {
//     Serial.print("WiFi OK, IP=");
//     Serial.println(WiFi.localIP());
//   } else {
//     Serial.println("WiFi failed");
//   }
// }

// String makeWriteURL() {
//   String url = String("http://") + INFLUX_HOST + ":" + String(INFLUX_PORT) + "/write?db=" + INFLUX_DB;
//   if (strlen(INFLUX_USER) > 0) {
//     url += "&u=";
//     url += INFLUX_USER;
//     url += "&p=";
//     url += INFLUX_PASS;
//   }
//   return url;
// }

// // Build a correct line-protocol payload for InfluxDB v1
// String makeLineProtocol(int stateVal) {
//   // IMPORTANT: no spaces around "=" in tags or fields, no trailing commas
//   // Using integer suffix 'i' forces integer type in Influx
//   String lp = String(MEASUREMENT) + ",hostname=" + HOST_TAG + ",location=" + LOCATION_TAG
//               + " state=" + String(stateVal) + "i";
//   return lp;
// }

// bool sendPoint(int stateVal) {
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("WiFi not connected, can't send");
//     return false;
//   }

//   HTTPClient http;
//   String url = makeWriteURL();
//   String payload = makeLineProtocol(stateVal);

//   Serial.print("POST ");
//   Serial.println(url);
//   Serial.print("Payload: ");
//   Serial.println(payload);

//   http.begin(wifiClient, url); // HTTP
//   http.addHeader("Content-Type", "text/plain");
//   int httpCode = http.POST(payload);

//   if (httpCode > 0) {
//     Serial.printf("HTTP %d\n", httpCode);
//     String resp = http.getString();
//     if (resp.length() > 0) Serial.println(resp);
//     http.end();
//     // Influx v1 returns 204 No Content on success (body empty)
//     return (httpCode == 204 || httpCode == 200);
//   } else {
//     Serial.printf("HTTP POST failed, error: %s\n", http.errorToString(httpCode).c_str());
//     http.end();
//     return false;
//   }
// }

// void loop() {
//   int raw = digitalRead(SWITCH_PIN);
//   // With INPUT_PULLUP & switch to GND: LOW == closed, HIGH == open
//   int logicalState = (raw == LOW) ? 1 : 0; // 1=closed, 0=open as you requested

//   if (lastLogical == -1) lastLogical = logicalState;
//   if (logicalState != lastLogical) {
//     lastBounce = millis();
//     lastLogical = logicalState;
//   }

//   if (millis() - lastBounce > DEBOUNCE_MS) {
//     unsigned long now = millis();
//     static int lastSentState = -1;
//     if (logicalState != lastSentState || (now - lastSend) >= SEND_INTERVAL_MS) {
//       bool ok = sendPoint(logicalState);
      if (ok) {
//         Serial.printf("Sent state=%d\n", logicalState);
//         lastSentState = logicalState;
//         lastSend = now;
//       } else {
//         Serial.println("Send failed (will retry later)");
//         // Will try again after ENTERING next loop as per SEND_INTERVAL_MS
//       }
//     }
//   }
//   delay(100);
// }

// Multi-sensor -> InfluxDB v1 (batched posts, per-sensor mapping, debounce, clean serial)
// Works on ESP8266 (LOLIN D1 mini Pro) and ESP32 (portable includes)

// Multi-sensor -> InfluxDB v1 (clean output, explicit per-sensor mapping, batched posts)
// For ESP8266 (LOLIN D1 mini Pro) or ESP32

// Multi-sensor -> InfluxDB v1 (4 sensors)
// Supports ESP8266 and ESP32 (select correct board in Arduino IDE)
// - Debounce per sensor
// - Per-sensor mapping (closed -> configured value)
// - Batched POST (one HTTP call containing multiple line-protocol lines)
// - Only sends changed sensors or heartbeat

/********************************************************************************
  4-sensor ESP -> InfluxDB v1 writer (complete .ino)
  - Compatible with ESP8266 (LOLIN D1 mini Pro / NodeMCU) and ESP32
  - Reads 4 magnetic/reed switches, debounces them independently
  - Per-sensor mapping: configure what GPIO level means "closed" and what value
    should be written to Influx when CLOSED (0 or 1)
  - Batches multiple sensor writes into a single HTTP POST (one line per sensor)
  - Sends only when a sensor changed OR when the sensor heartbeat interval elapsed
  - Clean, compact Serial output for debugging
  - IMPORTANT: Edit WIFI_SSID, WIFI_PASS, INFLUX_HOST, INFLUX_DB as needed
********************************************************************************/