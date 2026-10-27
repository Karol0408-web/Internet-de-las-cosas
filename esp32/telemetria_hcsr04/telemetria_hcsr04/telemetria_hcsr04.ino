#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

Preferences prefs;

// ---------- Pines ----------
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;
const int TURBIDITY_PIN = 34;

// ---------- Configuracion ----------
const char* WIFI_SSID = "Megacable_2.4G_FD80";
const char* WIFI_PASSWORD = "xbkpGdEX";
const char* MQTT_BROKER = "192.168.1.11";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC = "iot/tanque/telemetria";

const float FACTOR_DIVISOR = 1.0;
const int TURBIDITY_SAMPLES = 15;

const unsigned long SAMPLE_INTERVAL = 100;
const unsigned long PUBLISH_INTERVAL = 1000;
const int WINDOW_SIZE = 10;

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ---------- Filtros de promedio movil ----------
float distanceWindow[WINDOW_SIZE] = {0};
int voltageWindow[WINDOW_SIZE] = {0};

int distanceIndex = 0;
int voltageIndex = 0;
int distanceCount = 0;
int voltageCount = 0;

float distanceSum = 0;
long voltageSum = 0;

float distanceFiltered = -1;
int voltageFiltered = -1;
bool voltageValid = false;

unsigned long lastSample = 0;
unsigned long lastPublish = 0;
unsigned long sequenceId = 0;

// ---------- Calibracion ----------
int voltageClear = -1;
int voltageTurbid = -1;

// ---------- WiFi ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado");
  Serial.print("IP ESP32: ");
  Serial.println(WiFi.localIP());
}

// ---------- MQTT ----------
void connectMQTT() {
  while (!mqtt.connected()) {
    String id = "ESP32_TANQUE_" + String((uint32_t)ESP.getEfuseMac(), HEX);

    Serial.print("Conectando a MQTT... ");

    if (mqtt.connect(id.c_str())) {
      Serial.println("Conectado");
    } else {
      Serial.print("Error: ");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

// ---------- Distancia ----------
float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) return -1;

  float cm = duration * 0.0343 / 2.0;

  if (cm < 2 || cm > 200) return -1;

  return cm;
}

void addDistance(float value) {
  if (value < 0) return;

  if (distanceCount == WINDOW_SIZE) {
    distanceSum -= distanceWindow[distanceIndex];
  } else {
    distanceCount++;
  }

  distanceWindow[distanceIndex] = value;
  distanceSum += value;

  distanceIndex = (distanceIndex + 1) % WINDOW_SIZE;
  distanceFiltered = distanceSum / distanceCount;
}

void addVoltage(int value) {
  if (voltageCount == WINDOW_SIZE) {
    voltageSum -= voltageWindow[voltageIndex];
  } else {
    voltageCount++;
  }

  voltageWindow[voltageIndex] = value;
  voltageSum += value;

  voltageIndex = (voltageIndex + 1) % WINDOW_SIZE;
  voltageFiltered = voltageSum / voltageCount;
}

// ---------- Leer turbidez (mediana de varias muestras) ----------
void readTurbidity() {
  int s[TURBIDITY_SAMPLES];

  for (int i = 0; i < TURBIDITY_SAMPLES; i++) {
    s[i] = analogReadMilliVolts(TURBIDITY_PIN);
    delayMicroseconds(200);
  }

  for (int i = 0; i < TURBIDITY_SAMPLES - 1; i++)
    for (int j = i + 1; j < TURBIDITY_SAMPLES; j++)
      if (s[j] < s[i]) { int t = s[i]; s[i] = s[j]; s[j] = t; }

  int mv = s[TURBIDITY_SAMPLES / 2];

  addVoltage((int)(mv * FACTOR_DIVISOR + 0.5));
  voltageValid = true;
}

// ---------- Diagnostico de turbidez ----------
int turbidityPercent() {
  float span = voltageClear - voltageTurbid;
  float pct = (voltageClear - voltageFiltered) / span * 100.0;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (int)(pct + 0.5);
}

const char* getTurbidityStatus() {
  if (!voltageValid || voltageFiltered < 0) return "FUERA_DE_RANGO";
  if (voltageClear < 0 || voltageTurbid < 0) return "SIN_CALIBRAR";
  if (abs(voltageClear - voltageTurbid) < 50) return "RECALIBRAR";

  int pct = turbidityPercent();

  if (pct < 25) return "AGUA_CLARA";
  if (pct < 50) return "AGUA_POCO_TURBIA";
  if (pct < 75) return "AGUA_MEDIA_TURBIA";
  return "AGUA_TURBIA";
}

// ---------- Calibracion por Serial ----------
void processSerialCalibration() {
  if (!Serial.available()) return;

  char command = Serial.read();

  if (!voltageValid || voltageFiltered < 0) {
    Serial.println("No se puede calibrar: sin lectura.");
    return;
  }

  if (command == 'L' || command == 'l') {
    voltageClear = voltageFiltered;
    prefs.putInt("clear", voltageClear);
    Serial.print("Referencia agua clara: ");
    Serial.print(voltageClear);
    Serial.println(" mV");
  }

  if (command == 'T' || command == 't') {
    voltageTurbid = voltageFiltered;
    prefs.putInt("turbid", voltageTurbid);
    Serial.print("Referencia agua turbia: ");
    Serial.print(voltageTurbid);
    Serial.println(" mV");
  }

  Serial.print("Estado: ");
  Serial.println(getTurbidityStatus());
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  prefs.begin("turbidez", false);
  voltageClear = prefs.getInt("clear", -1);
  voltageTurbid = prefs.getInt("turbid", -1);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(TURBIDITY_PIN, ADC_11db);

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  Serial.println("Monitor de tanque");
  Serial.println("Escribe L con agua clara.");
  Serial.println("Escribe T con agua turbia.");

  connectWiFi();
  connectMQTT();
}

// ---------- Loop ----------
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqtt.connected()) {
    connectMQTT();
  }

  mqtt.loop();
  processSerialCalibration();

  unsigned long now = millis();

  if (now - lastSample >= SAMPLE_INTERVAL) {
    lastSample = now;

    addDistance(readDistanceCm());
    readTurbidity();
  }

  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;

    StaticJsonDocument<512> doc;

    doc["node_id"] = "ESP32_TANQUE_01";
    doc["sequence_id"] = sequenceId++;
    doc["timestamp_ms"] = now;
    doc["heap_free"] = ESP.getFreeHeap();

    if (distanceCount > 0) {
      doc["distance_cm"] = round(distanceFiltered * 100) / 100.0;
    } else {
      doc["distance_cm"] = nullptr;
    }

    doc["turbidity_valid"] = voltageValid;

    if (voltageValid && voltageFiltered >= 0) {
      doc["turbidity_voltage_mv"] = voltageFiltered;
    } else {
      doc["turbidity_voltage_mv"] = nullptr;
    }

    doc["diagnostico_turbidez"] = getTurbidityStatus();
    if (voltageClear >= 0 && voltageTurbid >= 0 && voltageValid) {
      doc["turbidity_pct"] = turbidityPercent();
    } else {
      doc["turbidity_pct"] = nullptr;
    }

    char payload[512];
    size_t len = serializeJson(doc, payload, sizeof(payload));

    Serial.println("-------------------------");
    Serial.print("Distancia filtrada: ");
    Serial.println(distanceFiltered);
    Serial.print("Voltaje (mV): ");
    Serial.println(voltageFiltered);
    Serial.print("Diagnostico: ");
    Serial.println(getTurbidityStatus());

    if (len > 0 && mqtt.publish(MQTT_TOPIC, payload)) {
      Serial.print("JSON MQTT: ");
      Serial.println(payload);
    } else {
      Serial.println("Error publicando MQTT");
    }
  }
}