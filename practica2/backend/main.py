import asyncio
import json
from contextlib import asynccontextmanager

import paho.mqtt.client as mqtt
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

MQTT_HOST = "127.0.0.1"
MQTT_PORT = 1883
MQTT_TOPIC = "sensores/casouse_01/telemetria"
MQTT_QOS = 0

main_loop: asyncio.AbstractEventLoop | None = None
mqtt_connected = False


class ConnectionManager:
    def __init__(self):
        self.active: set[WebSocket] = set()

    async def connect(self, ws: WebSocket):
        await ws.accept()
        self.active.add(ws)

    def disconnect(self, ws: WebSocket):
        self.active.discard(ws)

    async def broadcast(self, message: str):
        dead = []
        for ws in list(self.active):
            try:
                await ws.send_text(message)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.disconnect(ws)


manager = ConnectionManager()
mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)


def on_connect(client, userdata, flags, reason_code, properties):
    global mqtt_connected
    mqtt_connected = (reason_code == 0)
    print(f"[MQTT] Conectado: {mqtt_connected}")
    if mqtt_connected:
        client.subscribe(MQTT_TOPIC, qos=MQTT_QOS)


def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
    global mqtt_connected
    mqtt_connected = False
    print(f"[MQTT] Desconectado: {reason_code}")


def on_message(client, userdata, msg):
    # Corre en el hilo de red de paho, no en el event loop de asyncio
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
        if not isinstance(payload, dict):
            return
        print("[MQTT] Recibido:", payload)
        if main_loop and main_loop.is_running():
            asyncio.run_coroutine_threadsafe(
                manager.broadcast(json.dumps(payload)), main_loop
            )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        print("[MQTT] JSON invalido:", exc)

@asynccontextmanager
async def lifespan(app: FastAPI):
    global main_loop
    main_loop = asyncio.get_running_loop()

    mqtt_client.on_connect = on_connect
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.on_message = on_message
    mqtt_client.reconnect_delay_set(min_delay=1, max_delay=10)

    try:
        mqtt_client.connect(MQTT_HOST, MQTT_PORT, 60)
        mqtt_client.loop_start()
    except Exception as exc:
        print("[MQTT] No se pudo conectar:", exc)

    yield

    mqtt_client.loop_stop()
    mqtt_client.disconnect()


app = FastAPI(title="Middleware IoT", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health")
async def health():
    return {"status": "ok", "mqtt_connected": mqtt_connected, "topic": MQTT_TOPIC}


@app.websocket("/ws/telemetria")
async def ws_telemetria(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        manager.disconnect(websocket)