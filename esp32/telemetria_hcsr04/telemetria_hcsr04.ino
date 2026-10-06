#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define TRIGGER_PIN 33
#define ECHO_PIN 32
#define MAX_DISTANCE_CM 200

const char* WIFI_SSID = "dinero";
const char* WIFI_PASSWORD = "123456789-";

// IP DE LA PC donde corre Mosquitto.
// Ejemplo: 192.168.137.10
const char* MQTT_BROKER = "192.168.56.1";
const int MQTT_PORT = 1883;

const char* MQTT_TOPIC = "iot/tanque/telemetria";

WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long sequenceId = 0;
unsigned long lastPublish = 0;
const unsigned long publishInterval = 1000;

void connectWiFi() {
  Serial.print("Conectando a WiFi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectado");
  Serial.print("IP ESP32: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Conectando a MQTT... ");

    String clientId = "ESP32_TANQUE_01_" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqtt.connect(clientId.c_str())) {
      Serial.println("OK");
    } else {
      Serial.print("fallo, rc=");
      Serial.print(mqtt.state());
      Serial.println(" reintentando en 2 segundos");
      delay(2000);
    }
  }
}

float readDistanceCm() {
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) {
    return -1.0;
  }

  float distance = duration * 0.0343 / 2.0;

  if (distance < 2.0 || distance > MAX_DISTANCE_CM) {
    return -1.0;
  }

  return distance;
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(TRIGGER_PIN, LOW);

  connectWiFi();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqtt.connected()) {
    connectMQTT();
  }

  mqtt.loop();

  if (millis() - lastPublish >= publishInterval) {
    lastPublish = millis();

    float distance = readDistanceCm();

    if (distance < 0) {
      Serial.println("Sin eco / fuera de rango");
      return;
    }

    Serial.print("Distancia: ");
    Serial.print(distance, 1);
    Serial.println(" cm");

    StaticJsonDocument<256> doc;

    doc["node_id"] = "ESP32_TANQUE_01";
    doc["sequence_id"] = sequenceId++;
    doc["timestamp_ms"] = millis();
    doc["distance_cm"] = distance;

    char buffer[256];
    serializeJson(doc, buffer);

    if (mqtt.publish(MQTT_TOPIC, buffer)) {
      Serial.println("MQTT publicado");
    } else {
      Serial.println("Error publicando MQTT");
    }
  }
}
