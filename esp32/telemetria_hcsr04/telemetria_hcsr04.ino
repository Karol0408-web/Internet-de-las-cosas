#define ARDUINOJSON_USE_LONG_LONG 1

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <sys/time.h>

Preferences prefs;

// ---------- Pines ----------
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;
const int TURBIDITY_PIN = 34;
const int RELAY_PIN = 2;  // GPIO2 = LED integrado; cambialo al pin de tu rele

// ---------- Configuracion ----------
const char* WIFI_SSID = "Megacable_2.4G_FD80";
const char* WIFI_PASSWORD = "xbkpGdEX";
const char* MQTT_BROKER = "192.168.1.7";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC = "iot/tanque/telemetria";
const char* CONTROL_TOPIC = "iot/tanque/control";

const float FACTOR_DIVISOR = 1.0;
const int TURBIDITY_SAMPLES = 15;

const unsigned long SAMPLE_INTERVAL = 100;
unsigned long publishInterval = 1000;  // se cambia desde el dashboard
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

bool relayState = false;

// ---------- Calibracion ----------
int voltageClear = -1;
int voltageTurbid = -1;

// ---------- Hora (NTP) ----------
void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  unsigned long t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 15000) {
    delay(200);
  }

  Serial.println(time(nullptr) > 1700000000 ? "Hora sincronizada" : "Sin NTP: latencia no disponible");
}

uint64_t epochMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

// ---------- Comandos Web -> ESP32 ----------
void onCommand(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<128> cmd;
  if (deserializeJson(cmd, payload, length)) return;

  const char* name = cmd["cmd"] | "";

  if (!strcmp(name, "relay")) {
    relayState = (cmd["value"] | 0) != 0;
    digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  } else if (!strcmp(name, "interval")) {
    publishInterval = constrain((int)(cmd["value"] | 1000), 50, 5000);
  }

  Serial.printf("Comando: %s\n", name);
}

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
      mqtt.subscribe(CONTROL_TOPIC);
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

// ---------- Publicacion ----------
void publishTelemetry(unsigned long now) {
  StaticJsonDocument<512> doc;

  doc["node_id"] = "ESP32_TANQUE_01";
  doc["sequence_id"] = sequenceId++;
  doc["timestamp_ms"] = epochMs();

  JsonObject data = doc.createNestedObject("data");

  if (distanceCount > 0) data["distance_cm"] = round(distanceFiltered * 100) / 100.0;
  else data["distance_cm"] = nullptr;

  if (voltageValid && voltageFiltered >= 0) data["turbidity_voltage_mv"] = voltageFiltered;
  else data["turbidity_voltage_mv"] = nullptr;

  if (voltageClear >= 0 && voltageTurbid >= 0 && voltageValid) data["turbidity_pct"] = turbidityPercent();
  else data["turbidity_pct"] = nullptr;

  doc["diagnostico_turbidez"] = getTurbidityStatus();
  doc["relay"] = relayState;
  doc["interval_ms"] = publishInterval;

  char payload[512];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  if (len > 0 && mqtt.publish(MQTT_TOPIC, payload)) {
    Serial.print("JSON MQTT: ");
    Serial.println(payload);
  } else {
    Serial.println("Error publicando MQTT");
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  prefs.begin("turbidez", false);
  voltageClear = prefs.getInt("clear", -1);
  voltageTurbid = prefs.getInt("turbid", -1);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(TURBIDITY_PIN, ADC_11db);

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setCallback(onCommand);

  Serial.println("Monitor de tanque");
  Serial.println("Escribe L con agua clara.");
  Serial.println("Escribe T con agua turbia.");

  connectWiFi();
  syncTime();
  connectMQTT();
}

// ---------- Loop ----------
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();

  mqtt.loop();
  processSerialCalibration();

  unsigned long now = millis();

  if (now - lastSample >= SAMPLE_INTERVAL) {
    lastSample = now;

    addDistance(readDistanceCm());
    readTurbidity();
  }

  if (now - lastPublish >= publishInterval) {
    lastPublish = now;
    publishTelemetry(now);
  }
}