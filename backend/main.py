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
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
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
    print(f"[MQTT] Conectado. reason_code={reason_code}")
    client.subscribe(MQTT_TOPIC)
    print(f"[MQTT] Suscrito a {MQTT_TOPIC}")


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
        print("[MQTT]", payload)

        if main_loop:
            asyncio.run_coroutine_threadsafe(
                broadcast(payload),
                main_loop
            )
    except Exception as exc:
        print("[MQTT] Error:", exc)


@asynccontextmanager
async def lifespan(app: FastAPI):
    global main_loop
    main_loop = asyncio.get_running_loop()

    mqtt_client.on_connect = on_connect
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


app = FastAPI(title="Dashboard IoT - Tanque", lifespan=lifespan)

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
        "websocket": "/ws/telemetria",
        "mqtt_topic": MQTT_TOPIC,
    }


@app.get("/health")
async def health():
    return {"status": "ok", "mqtt_topic": MQTT_TOPIC}


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
