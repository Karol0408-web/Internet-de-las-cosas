
import asyncio
import json
from contextlib import asynccontextmanager

import paho.mqtt.client as mqtt
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

MQTT_HOST = "127.0.0.1"
MQTT_PORT = 1883
MQTT_TOPIC = "iot/tanque/telemetria"

clients: set[WebSocket] = set()
mqtt_connected = False
mqtt_client = mqtt.Client(
    callback_api_version=mqtt.CallbackAPIVersion.VERSION2
)
main_loop: asyncio.AbstractEventLoop | None = None


async def broadcast(payload: dict):
    message = json.dumps(payload)
    dead = []

    for ws in list(clients):
        try:
            await ws.send_text(message)
        except Exception:
            dead.append(ws)

    for ws in dead:
        clients.discard(ws)


def on_connect(client, userdata, flags, reason_code, properties):
    global mqtt_connected

    mqtt_connected = (reason_code == 0)

    print(f"[MQTT] Conectado: {mqtt_connected}, codigo={reason_code}")

    if mqtt_connected:
        client.subscribe(MQTT_TOPIC)
        print(f"[MQTT] Suscrito a {MQTT_TOPIC}")


def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
    global mqtt_connected
    mqtt_connected = False
    print(f"[MQTT] Desconectado: {reason_code}")


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))

        if not isinstance(payload, dict):
            print("[MQTT] Mensaje ignorado: se esperaba un objeto JSON")
            return

        print("[MQTT] Telemetria recibida:", payload)

        if main_loop and main_loop.is_running():
            asyncio.run_coroutine_threadsafe(
                broadcast(payload),
                main_loop
            )

    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        print("[MQTT] JSON invalido:", exc)
    except Exception as exc:
        print("[MQTT] Error:", exc)


@asynccontextmanager
async def lifespan(app: FastAPI):
    global main_loop
    main_loop = asyncio.get_running_loop()

    mqtt_client.on_connect = on_connect
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.on_message = on_message

    try:
        mqtt_client.connect(MQTT_HOST, MQTT_PORT, 60)
        mqtt_client.loop_start()
        print(f"[MQTT] Broker: {MQTT_HOST}:{MQTT_PORT}")
    except Exception as exc:
        print("[MQTT] No se pudo conectar:", exc)

    yield

    mqtt_client.loop_stop()
    mqtt_client.disconnect()


app = FastAPI(
    title="Dashboard IoT - Tanque",
    lifespan=lifespan
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/")
async def root():
    return {
        "project": "Monitoreo de Nivel de Agua y Turbidez",
        "status": "running",
        "mqtt_connected": mqtt_connected,
        "websocket": "/ws/telemetria",
        "mqtt_topic": MQTT_TOPIC,
    }


@app.get("/health")
async def health():
    return {
        "status": "ok",
        "mqtt_connected": mqtt_connected,
        "mqtt_topic": MQTT_TOPIC,
    }


@app.websocket("/ws/telemetria")
async def websocket_telemetria(websocket: WebSocket):
    await websocket.accept()
    clients.add(websocket)
    print(f"[WS] Cliente conectado. Total: {len(clients)}")

    try:
        await websocket.send_text(json.dumps({
            "type": "status",
            "message": "WebSocket conectado"
        }))

        while True:
            await websocket.receive_text()

    except WebSocketDisconnect:
        pass
    except Exception as exc:
        print("[WS] Error:", exc)
    finally:
        clients.discard(websocket)
        print(f"[WS] Cliente desconectado. Total: {len(clients)}")
