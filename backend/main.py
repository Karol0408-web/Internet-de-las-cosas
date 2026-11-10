import asyncio
import json
import time
from contextlib import asynccontextmanager

import paho.mqtt.client as mqtt
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import select

from db import Base, Session, Telemetria, engine

MQTT_HOST = "127.0.0.1"
MQTT_PORT = 1883
MQTT_TOPIC = "iot/tanque/telemetria"
CONTROL_TOPIC = "iot/tanque/control"
COMANDOS = {"relay", "interval"}

clients: set[WebSocket] = set()
mqtt_connected = False
mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
main_loop: asyncio.AbstractEventLoop | None = None

metrics = {"n": 0, "seq_min": None, "seq_max": None, "lat_sum": 0.0, "lat_n": 0}


# ---------- Metricas de red ----------
def reset_metrics():
    metrics.update(n=0, seq_min=None, seq_max=None, lat_sum=0.0, lat_n=0)


def actualizar_metricas(seq: int, lat: float | None) -> dict:
    # Si la secuencia retrocede, el ESP32 se reinicio: nueva medicion
    if metrics["seq_max"] is not None and seq < metrics["seq_max"]:
        reset_metrics()

    metrics["n"] += 1
    metrics["seq_min"] = seq if metrics["seq_min"] is None else min(metrics["seq_min"], seq)
    metrics["seq_max"] = seq if metrics["seq_max"] is None else max(metrics["seq_max"], seq)

    if lat is not None:
        metrics["lat_sum"] += lat
        metrics["lat_n"] += 1

    esperados = metrics["seq_max"] - metrics["seq_min"] + 1
    return {
        "L_prom": round(metrics["lat_sum"] / metrics["lat_n"], 1) if metrics["lat_n"] else None,
        "P": round((1 - metrics["n"] / esperados) * 100, 2),
        "recibidos": metrics["n"],
        "esperados": esperados,
    }


# ---------- WebSocket ----------
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


async def manejar_comando(raw: str):
    try:
        cmd = json.loads(raw)
    except json.JSONDecodeError:
        return

    nombre = cmd.get("cmd")

    if nombre == "reset_metrics":
        reset_metrics()
    elif nombre in COMANDOS and isinstance(cmd.get("value"), int):
        if nombre == "interval":
            reset_metrics()  # cada frecuencia es un experimento nuevo
        mqtt_client.publish(CONTROL_TOPIC, json.dumps(cmd))
        print("[CMD] Enviado al ESP32:", cmd)


# ---------- Procesamiento ----------
async def procesar(p: dict, t_rx: float):
    try:
        ts = p.get("timestamp_ms")
        # Solo hay latencia valida si el ESP32 envio hora epoch (NTP)
        lat = round(t_rx - ts, 1) if isinstance(ts, (int, float)) and ts > 1e12 else None
        m = actualizar_metricas(int(p["sequence_id"]), lat)

        async with Session() as s:
            s.add(Telemetria(
                node_id=p.get("node_id", "--"),
                sequence_id=int(p["sequence_id"]),
                variables_json={**p.get("data", {}), "interval_ms": p.get("interval_ms")},
                diagnostico=p.get("diagnostico_turbidez"),
                latencia_ms=lat,
            ))
            await s.commit()

        await broadcast({**p, "latencia_ms": lat, "metricas": m})
    except Exception as exc:
        print("[PROC] Error:", exc)


# ---------- MQTT ----------
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
    t_rx = time.time() * 1000

    try:
        payload = json.loads(msg.payload.decode("utf-8"))

        if not isinstance(payload, dict) or "sequence_id" not in payload:
            print("[MQTT] Mensaje ignorado")
            return

        if main_loop and main_loop.is_running():
            asyncio.run_coroutine_threadsafe(procesar(payload, t_rx), main_loop)

    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        print("[MQTT] JSON invalido:", exc)


@asynccontextmanager
async def lifespan(app: FastAPI):
    global main_loop
    main_loop = asyncio.get_running_loop()

    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)

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
        "mqtt_connected": mqtt_connected,
        "websocket": "/ws/telemetria",
        "mqtt_topic": MQTT_TOPIC,
        "control_topic": CONTROL_TOPIC,
    }


@app.get("/health")
async def health():
    return {"status": "ok", "mqtt_connected": mqtt_connected}


@app.get("/historial")
async def historial(limit: int = 50):
    async with Session() as s:
        rows = (await s.scalars(
            select(Telemetria).order_by(Telemetria.id.desc()).limit(limit)
        )).all()

    return [
        {
            "id": r.id,
            "sequence_id": r.sequence_id,
            "variables_json": r.variables_json,
            "diagnostico": r.diagnostico,
            "clase_predicha_rf": r.clase_predicha_rf,
            "latencia_ms": r.latencia_ms,
            "fecha_registro": r.fecha_registro.isoformat(),
        }
        for r in rows
    ]


@app.websocket("/ws/telemetria")
async def websocket_telemetria(websocket: WebSocket):
    await websocket.accept()
    clients.add(websocket)
    print(f"[WS] Cliente conectado. Total: {len(clients)}")

    try:
        await websocket.send_text(json.dumps({"type": "status", "message": "WebSocket conectado"}))

        while True:
            await manejar_comando(await websocket.receive_text())

    except WebSocketDisconnect:
        pass
    except Exception as exc:
        print("[WS] Error:", exc)
    finally:
        clients.discard(websocket)
        print(f"[WS] Cliente desconectado. Total: {len(clients)}")