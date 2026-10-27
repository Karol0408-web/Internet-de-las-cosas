GUÍA DE LABORATORIO #2 (CASOS DE USO)
Middleware IoT Multivariable y Servidor Backend en Tiempo Real (FastAPI +
MQTT + WebSockets)
• Asignatura: Sistemas IoT y Procesamiento en el Borde
• Duración: 2 Horas (Sesión de Laboratorio)
• Evaluación: Práctica (25%) + Reporte Técnico (25%)
1. Objetivos de Aprendizaje
• Integrar el protocolo de mensajería MQTT en el firmware de la ESP32 para
publicar la telemetría multivariable procesada en la Práctica 1 hacia un
Broker central.
• Implementar un servidor backend asíncrono mediante FastAPI en Python
para la recepción, deserialización y validación del payload JSON
multivariable.
• Canalizar el flujo de datos multivariable en tiempo real usando
WebSockets desde el backend hacia clientes web.
• Evaluar el impacto técnico de los niveles de Calidad de Servicio (QoS 0 vs
QoS 1) y el rendimiento del servidor ante el envío sostenido de múltiples
sensores.
2. Arquitectura del Sistema Middleware
¿
3. Materiales y Requisitos
• Hardware: Tarjeta ESP32 con la etapa de sensores ensamblada en la
Práctica 1.
• Software Firmware: Arduino IDE / VS Code PlatformIO con librerías WiFi.h,
PubSubClient, ArduinoJson y librerías de sensores.
• Software Backend: Python 3.10+ (fastapi, uvicorn, paho-mqtt,
websockets).
• Broker MQTT: Eclipse Mosquitto (Local o vía Docker).
4. Código Base y Desarrollo
1. Firmware ESP32 Multivariable (Evolución de Práctica 1)
#include

#include
#include

const char* ssid = "TU_RED_WIFI";
const char* password = "TU_PASSWORD";
const char* mqtt_server = "192.168.X.X"; // IP de la PC con Broker Mosquitto

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long previousMillis = 0;
const long interval = 500; // Envío cada 500 ms

void setup_wifi() {
WiFi.begin(ssid, password);
while (WiFi.status() != WL_CONNECTED) {
delay(500);
}
}

void reconnect() {
while (!client.connected()) {
if (client.connect("ESP32_CASE_STUDY_CLIENT")) {
// Conectado al Broker
} else {
delay(2000);
}
}

}

void setup() {
Serial.begin(115200);
setup_wifi();
client.setServer(mqtt_server, 1883);
// Inicializar sensores del Caso de Uso aquí...
}

void loop() {
if (!client.connected()) reconnect();
client.loop();

unsigned long currentMillis = millis();
if (currentMillis - previousMillis >= interval) {
previousMillis = currentMillis;

// LECTURA Y FILTRADO DE SENSORES DEL CASO DE USO
float var1 = analogRead(34); // Adaptar a sensores reales
float var2 = 22.4; // Adaptar a sensores reales

StaticJsonDocument<384> doc;
doc["node_id"] = "ESP32_CASE_STUDY_XX";

JsonObject data = doc.createNestedObject("data");
data["sensor_1"] = round(var1 * 100.0) / 100.0;
data["sensor_2"] = round(var2 * 100.0) / 100.0;

doc["heap_free"] = ESP.getFreeHeap();

char buffer[384];
serializeJson(doc, buffer);

// Publicación MQTT
client.publish("sensores/casouse_XX/telemetria", buffer);
Serial.println(buffer);
}
}
2. Backend Middleware en FastAPI (main.py)
import json
import asyncio
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
import paho.mqtt.client as mqtt

app = FastAPI(title="IoT Middleware - Caso de Uso")

MQTT_BROKER = "localhost"
MQTT_TOPIC = "sensores/casouse_XX/telemetria"

class ConnectionManager:
def __init__(self):
self.active_connections: list[WebSocket] = []

async def connect(self, websocket: WebSocket):
await websocket.accept()
self.active_connections.append(websocket)

def disconnect(self, websocket: WebSocket):
self.active_connections.remove(websocket)

async def broadcast(self, message: str):
for connection in self.active_connections:
await connection.send_text(message)

manager = ConnectionManager()
loop = asyncio.get_event_loop()

def on_message(client, userdata, msg):
payload = msg.payload.decode('utf-8')
print(f"[MQTT Inbox - Caso de Uso]: {payload}")
# Retransmisión asíncrona hacia el canal WebSocket
asyncio.run_coroutine_threadsafe(manager.broadcast(payload), loop)

mqtt_client = mqtt.Client()
mqtt_client.on_message = on_message

@app.on_event("startup")
def startup_event():
mqtt_client.connect(MQTT_BROKER, 1883, 60)
mqtt_client.subscribe(MQTT_TOPIC)
mqtt_client.loop_start()

@app.on_event("shutdown")
def shutdown_event():

mqtt_client.loop_stop()
mqtt_client.disconnect()

@app.websocket("/ws/telemetria")
async def websocket_endpoint(websocket: WebSocket):
await manager.connect(websocket)
try:
while True:
await websocket.receive_text()
except WebSocketDisconnect:
manager.disconnect(websocket)
5. Experimentos Técnicos a Realizar
1. Experimento 1: Calidad de Servicio (QoS 0 vs QoS 1) con Carga
Multivariable:
o Cambia la publicación del firmware entre QoS 0 y QoS 1.
o Simula degradación de red Wi-Fi y mide la latencia de llegada de los
paquetes JSON en la consola de FastAPI. Responde: ¿Cómo afecta
el tiempo de confirmación de QoS 1 al tiempo del loop() de la ESP32
cuando se leen múltiples sensores?

2. Experimento 2: Inspección del Stream WebSocket:
o Conecta un cliente WebSocket (ej. Postman o consola DevTools del
navegador) a ws://localhost:8000/ws/telemetria.
o Captura la recepción continua de tramas multivariable en tiempo
real.

6. Requisitos para el Reporte de Práctica #2
1. Capturas de Evidencia: Consola de la ESP32, consola de FastAPI
recibiendo el JSON multivariable y cliente WebSocket desplegando las
tramas en vivo.
2. Análisis de Experimentos: Respuestas detalladas sobre los resultados de
QoS 0 vs QoS 1 y gestión de hilos/Event Loop en FastAPI.

3. Articulación IEEE (Obligatorio): Explicar en 2 a 3 párrafos cómo el
desacoplamiento mediante un Broker Pub/Sub beneficia la arquitectura de
tu caso de uso específico, citando el artículo de Bharani & Parthipan (2026).