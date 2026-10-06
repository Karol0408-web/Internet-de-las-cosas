# Dashboard IoT - Monitoreo de Tanque

Versión inicial del proyecto:
ESP32 + HC-SR04 -> MQTT/Mosquitto -> FastAPI -> WebSocket -> Dashboard.

## Estructura

- esp32/telemetria_hcsr04.ino
- backend/main.py
- backend/requirements.txt
- mosquitto/config/mosquitto.conf
- docker-compose.yml
- dashboard/index.html
- dashboard/style.css
- dashboard/app.js

## 1. Mosquitto

Instala Docker Desktop y abre una terminal en la carpeta raíz:

```powershell
docker compose up -d
docker ps
```

El broker MQTT queda en:
- MQTT: localhost:1883
- WebSocket: localhost:9001 (opcional)

Para esta primera versión se usa MQTT sin TLS únicamente en laboratorio/local.

## 2. Backend

En `backend`:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

Dashboard: abre `dashboard/index.html` con Live Server de VS Code, o cualquier servidor HTTP local.

## 3. ESP32

En Arduino IDE instala:
- PubSubClient
- ArduinoJson

Cambia `WIFI_SSID`, `WIFI_PASSWORD` y `MQTT_BROKER` por los datos de tu red.

IMPORTANTE: el MQTT_BROKER debe ser la IP de la PC donde corre Mosquitto, por ejemplo `192.168.137.10`, NO `127.0.0.1`.

Conecta:
- HC-SR04 VCC -> 5V
- HC-SR04 GND -> GND
- TRIG -> GPIO 33
- ECHO -> GPIO 32 mediante divisor de voltaje si ECHO entrega 5 V.

## 4. Flujo

ESP32 publica:
`iot/tanque/telemetria`

Ejemplo:
```json
{"node_id":"ESP32_TANQUE_01","sequence_id":15,"timestamp_ms":12345,"distance_cm":34.5}
```

FastAPI recibe MQTT y retransmite por WebSocket:
`ws://localhost:8000/ws/telemetria`

## 5. Verificación

1. Comprueba que el HC-SR04 funciona en el monitor serial.
2. Comprueba que Mosquitto está levantado.
3. Ejecuta FastAPI.
4. Abre el dashboard.
5. Debe cambiar automáticamente la distancia cuando acerques/alejes un objeto.

Si el dashboard dice "Sin datos", revisa primero la IP del broker en el ESP32.
