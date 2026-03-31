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

/* Board selection & includes */
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266WiFiMulti.h>
  ESP8266WiFiMulti wifiMulti;
#elif defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <WiFiMulti.h>
  WiFiMulti wifiMulti;
#else
  #error "Unsupported board — define ESP8266 or ESP32 in your board manager"
#endif

#include <WiFiClient.h>

/* ========================= USER CONFIGURATION ============================== */
// WiFi credentials
#define WIFI_SSID    "Sensor monitoring"
#define WIFI_PASS    "Bl@ckAngel@2023"

// InfluxDB v1 settings (CentOS server)
const char* INFLUX_HOST = "172.25.1.15";   // change to your InfluxDB host/IP
const uint16_t INFLUX_PORT = 8086;        // default v1 port
const char* INFLUX_DB = "window_monitoring"; // database must already exist

// Measurement / tags
const char* MEASUREMENT = "status";
const char* HOST_TAG = "SHOPFLOOR";

// Sensors configuration
const int NUM_SENSORS = 4;
// GPIO pins used for the 4 sensors (NodeMCU Dn numbering). Change if you wired differently.
const int SENSOR_PINS[NUM_SENSORS] = {2, 0, 15, 5}; 
const char* LOCATIONS[NUM_SENSORS] = {"Window_1", "Window_2", "Window_3", "Window_4"};

// Per-sensor physical mapping:
// rawClosedLevel: which raw logic level (HIGH/LOW) corresponds to the sensor being physically CLOSED.
// typical reed with INPUT_PULLUP -> CLOSED = LOW
// If your sensor is wired differently, set to HIGH for that sensor.
const int rawClosedLevel[NUM_SENSORS]      = {LOW, LOW, HIGH, HIGH};

// closedStateValue: what integer value should be written into Influx when the window is CLOSED.
// (0 or 1). For your spec:
// Window_1 closed -> 0
// Window_2 closed -> 0
// Window_3 closed -> 1 
// Window_4 closed -> 1
const int closedStateValue[NUM_SENSORS]   = {0, 0, 1, 1};

// Timing & debounce
const unsigned long DEBOUNCE_MS   = 50UL;        // debounce window per sensor
const unsigned long HEARTBEAT_MS  = 30000UL;     // at least send each sensor once every 30s

/* ========================= END CONFIGURATION =============================== */

/* Runtime variables */
WiFiClient wifiClient;

int lastRaw[NUM_SENSORS];                // last raw read value for each pin
unsigned long lastChangeMs[NUM_SENSORS]; // last time raw value changed (for debounce)
int stableState[NUM_SENSORS];            // computed logical state (0 or 1) after mapping
int lastSentState[NUM_SENSORS];          // last state that was successfully sent (-1 = never sent)
unsigned long lastSentMs[NUM_SENSORS];   // last successful send time per sensor

/* Helper: build write URL */
String makeWriteURL() {
  return String("http://") + INFLUX_HOST + ":" + String(INFLUX_PORT) + "/write?db=" + INFLUX_DB;
}

/* Helper: build a line-protocol line for a sensor index */
String makeLineProtocol(int idx) {
  // measurement,hostname=...,location=... state=Ni
  String s = String(MEASUREMENT) + ",hostname=" + HOST_TAG + ",location=" + LOCATIONS[idx];
  s += " state=" + String(stableState[idx]) + "i";
  return s;
}

/* Send a multi-line payload (one line per included sensor) */
bool sendPayload(const String &payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected -> cannot send");
    return false;
  }

  HTTPClient http;
  String url = makeWriteURL();

  Serial.println();
  Serial.println("---- POST START ----");
  Serial.print("POST ");
  Serial.println(url);
  Serial.println("Payload:");
  Serial.println(payload);
  Serial.println("---- POST SENDING ----");

  #if defined(ESP8266)
    http.begin(wifiClient, url); // ESP8266
  #else
    http.begin(url); // ESP32 sometimes prefers this overload
  #endif

  http.addHeader("Content-Type", "text/plain");
  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    Serial.printf("HTTP %d\n", httpCode);
    if (httpCode == 204 || httpCode == 200) {
      http.end();
      Serial.println("POST successful (204/200)");
      return true;
    } else {
      String body = http.getString();
      Serial.print("Body: ");
      Serial.println(body);
      http.end();
      return false;
    }
  } else {
    Serial.print("HTTP POST failed: ");
    Serial.println(http.errorToString(httpCode));
    http.end();
    return false;
  }
}

/* Setup */
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("=== 4-sensor InfluxDB writer ===");

  // Initialize pins and runtime arrays
  unsigned long now = millis();
  for (int i = 0; i < NUM_SENSORS; ++i) {
    pinMode(SENSOR_PINS[i], INPUT_PULLUP); // using internal pull-up; change if necessary
    int r = digitalRead(SENSOR_PINS[i]);
    lastRaw[i] = r;
    lastChangeMs[i] = now;
    bool closed = (r == rawClosedLevel[i]);
    // stableState: value to write for current physical closed/open
    stableState[i] = closed ? closedStateValue[i] : (1 - closedStateValue[i]);
    lastSentState[i] = -1;    // not sent yet
    lastSentMs[i] = 0;
  }

  // Connect to WiFi (wifiMulti allows adding fallback networks)
  wifiMulti.addAP(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (wifiMulti.run() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed - check SSID/password or network");
  }

  Serial.println("Setup complete.");
}

/* Main loop */
void loop() {
  unsigned long now = millis();
  wifiMulti.run(); // keep WiFi alive / handle reconnection

  // Build payload lines for sensors that need to be sent (changed or heartbeat)
  String payload = "";
  bool include[NUM_SENSORS] = { false };
  bool anyToSend = false;

  for (int i = 0; i < NUM_SENSORS; ++i) {
    int raw = digitalRead(SENSOR_PINS[i]);

    // Debounce: update lastRaw and lastChangeMs on raw transitions
    if (raw != lastRaw[i]) {
      lastRaw[i] = raw;
      lastChangeMs[i] = now;
    }

    // If stable beyond debounce window, compute logical stable state
    if (now - lastChangeMs[i] >= DEBOUNCE_MS) {
      bool closedNow = (lastRaw[i] == rawClosedLevel[i]); // physical closed?
      int computedState = closedNow ? closedStateValue[i] : (1 - closedStateValue[i]);

      // update stableState if changed
      if (computedState != stableState[i]) {
        stableState[i] = computedState;
      }

      // decide if this sensor should be included in the outgoing payload:
      // include if it changed since last sent or if heartbeat elapsed
      if (stableState[i] != lastSentState[i] || (now - lastSentMs[i] >= HEARTBEAT_MS)) {
        if (payload.length() > 0) payload += "\n";
        payload += makeLineProtocol(i);
        include[i] = true;
        anyToSend = true;
      }
    }
  }

  if (anyToSend) {
    bool ok = sendPayload(payload);
    if (ok) {
      unsigned long sentAt = millis();
      // update last-sent state/time for included sensors
      for (int i = 0; i < NUM_SENSORS; ++i) {
        if (include[i]) {
          lastSentState[i] = stableState[i];
          lastSentMs[i] = sentAt;
        }
      }
      // print concise summary
      Serial.print("Summary:");
      for (int i = 0; i < NUM_SENSORS; ++i) {
        Serial.printf(" %s=%d", LOCATIONS[i], lastSentState[i] >= 0 ? lastSentState[i] : -1);
        if (i < NUM_SENSORS - 1) Serial.print(",");
      }
      Serial.println();
    } else {
      Serial.println("Send failed; will retry next cycle or heartbeat.");
    }
  } else {
    // No send required — print compact status periodically (every 8s)
    static unsigned long lastStatusMs = 0;
    if (now - lastStatusMs >= 8000UL) {
      Serial.print("Status:");
      for (int i = 0; i < NUM_SENSORS; ++i) {
        Serial.printf(" %s=%d", LOCATIONS[i], stableState[i]);
        if (i < NUM_SENSORS - 1) Serial.print(",");
      }
      Serial.println();
      lastStatusMs = now;
    }
  }

  // small delay to avoid busy-looping; keeps responsiveness high
  delay(50);
}





