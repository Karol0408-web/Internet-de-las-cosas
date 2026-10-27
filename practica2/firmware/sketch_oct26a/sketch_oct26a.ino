#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>   // v6

// ---------- Pines ----------
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;
const int TURBIDITY_PIN = 34;

// ---------- Red ----------
const char* WIFI_SSID = "uto";
const char* WIFI_PASSWORD = "12345678999";
const char* MQTT_BROKER = "192.168.137.59";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC = "sensores/casouse_01/telemetria";
const char* NODE_ID = "ESP32_CASE_STUDY_01";

// ---------- QoS: 0 = dispara y olvida, 1 = espera PUBACK ----------
const uint8_t QOS = 1;
const unsigned long ACK_TIMEOUT_MS = 2000;

// ---------- Sensores ----------
// Divisor OUT -> R1 -> GPIO -> R2 -> GND: factor = (R1 + R2) / R2
// 1k arriba y 2k a GND = 1.5. Si el de 2k va arriba, usa 3.0.
const float DIVIDER_FACTOR = 1.5;
const int TURBIDITY_SAMPLES = 10;
const unsigned long PUBLISH_INTERVAL = 500;

WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastPublish = 0;
uint16_t packetId = 0;
uint32_t okCount = 0, failCount = 0;
unsigned long totalMs = 0, maxMs = 0;

// ---------- WiFi / MQTT ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nIP: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    String id = "ESP32_CASE_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("Conectando a MQTT... ");
    if (mqtt.connect(id.c_str())) {
      Serial.println("OK");
    } else {
      Serial.print("error ");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

// ---------- Sensores ----------
float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long d = pulseIn(ECHO_PIN, HIGH, 30000UL);
  if (d == 0) return -1;

  float cm = d * 0.0343 / 2.0;
  return (cm < 2 || cm > 400) ? -1 : cm;
}

float readTurbidityVolts() {
  long sum = 0;
  for (int i = 0; i < TURBIDITY_SAMPLES; i++) {
    sum += analogReadMilliVolts(TURBIDITY_PIN);
    delayMicroseconds(200);
  }
  float pinMv = (float)sum / TURBIDITY_SAMPLES;
  return pinMv * DIVIDER_FACTOR / 1000.0;
}

// ---------- Publicacion QoS 1 (PubSubClient solo publica QoS 0) ----------
bool publishQoS1(const char* topic, const char* payload, size_t len) {
  static uint8_t buf[600];
  size_t topicLen = strlen(topic);
  size_t rem = 2 + topicLen + 2 + len;
  if (rem + 5 > sizeof(buf)) return false;

  if (++packetId == 0) packetId = 1;

  size_t n = 0;
  buf[n++] = 0x32;                       // PUBLISH, QoS 1
  size_t r = rem;
  do {                                   // longitud restante variable
    uint8_t b = r % 128;
    r /= 128;
    if (r > 0) b |= 0x80;
    buf[n++] = b;
  } while (r > 0);

  buf[n++] = topicLen >> 8;
  buf[n++] = topicLen & 0xFF;
  memcpy(buf + n, topic, topicLen); n += topicLen;
  buf[n++] = packetId >> 8;
  buf[n++] = packetId & 0xFF;
  memcpy(buf + n, payload, len); n += len;
  

  if (espClient.write(buf, n) != n) return false;

  // Bloquea hasta recibir el PUBACK (0x40 0x02 id_hi id_lo) o vencer el timeout
  unsigned long t0 = millis();
  while (millis() - t0 < ACK_TIMEOUT_MS) {
    if (espClient.available() >= 2) {
      uint8_t type = espClient.read();
      uint8_t length = espClient.read();
      if (type == 0x40 && length == 2) {
        while (espClient.available() < 2) delay(1);
        uint16_t id = (espClient.read() << 8) | espClient.read();
        if (id == packetId) return true;
      } else {
        while (length--) espClient.read();   // descarta PINGRESP u otros
      }
    }
    delay(1);
  }
  return false;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(TURBIDITY_PIN, ADC_11db);

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);

  connectWiFi();
  connectMQTT();
}

// ---------- Loop ----------
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastPublish < PUBLISH_INTERVAL) return;
  lastPublish = now;

  float dist = readDistanceCm();
  float turb = readTurbidityVolts();
  float avg = okCount ? (float)totalMs / okCount : 0;

  StaticJsonDocument<384> doc;
  doc["node_id"] = NODE_ID;
  JsonObject data = doc.createNestedObject("data");
  if (dist >= 0) data["distancia_cm"] = round(dist * 100) / 100.0;
  else data["distancia_cm"] = nullptr;
  data["turbidez_v"] = round(turb * 1000) / 1000.0;
  doc["heap_free"] = ESP.getFreeHeap();

  JsonObject st = doc.createNestedObject("stats");
  st["qos"] = QOS;
  st["avg_ms"] = round(avg * 100) / 100.0;
  st["max_ms"] = maxMs;
  st["ok"] = okCount;
  st["fail"] = failCount;

  char payload[384];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  unsigned long t0 = millis();
  bool ok = (QOS == 1)
      ? publishQoS1(MQTT_TOPIC, payload, len)
      : mqtt.publish(MQTT_TOPIC, (const uint8_t*)payload, len);
  unsigned long dt = millis() - t0;

  if (ok) { okCount++; totalMs += dt; } else failCount++;
  if (dt > maxMs) maxMs = dt;

  Serial.printf("QoS%d | %s | %lu ms | avg=%.1f max=%lu | ok=%u fail=%u\n",
                QOS, ok ? "OK" : "FALLO", dt, avg, maxMs, okCount, failCount);
}