const WS_URL = "ws://localhost:8000/ws/telemetria";

/* ====== CONFIGURACIÓN DEL TANQUE ======*/
const DISTANCIA_VACIO = 50;
const DISTANCIA_LLENO = 3;

/* ====== REFERENCIAS DOM ====== */
const distanceElement   = document.getElementById("distance");
const nodeElement       = document.getElementById("nodeId");
const sequenceElement   = document.getElementById("sequence");
const statusElement     = document.getElementById("sensorStatus");
const detailElement     = document.getElementById("sensorDetail");
const lastUpdateElement = document.getElementById("lastUpdate");
const badgeElement      = document.getElementById("connectionBadge");
const tableBody         = document.getElementById("telemetryBody");
const clearBtn          = document.getElementById("clearBtn");
const tankFill          = document.getElementById("tankFill");
const tankLevel         = document.getElementById("tankLevel");

let socket = null;

/* ====== UTILIDADES ====== */
function setConnection(connected) {
  badgeElement.classList.toggle("online", connected);
  badgeElement.classList.toggle("offline", !connected);
  badgeElement.textContent = connected ? "● Conectado" : "● Desconectado";
}

function fmt(n, digits = 1) {
  return Number(n).toFixed(digits);
}

function pad(n) {
  return String(n).padStart(2, "0");
}

function timeString(d = new Date()) {
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

/* ====== TANQUE ====== */
function actualizarTanque(distanciaCm) {
  const rango = DISTANCIA_VACIO - DISTANCIA_LLENO;
  if (rango <= 0) return;

  let nivel = ((DISTANCIA_VACIO - distanciaCm) / rango) * 100;
  nivel = Math.max(0, Math.min(100, nivel));

  // Altura de la columna de agua
  tankFill.style.height = nivel + "%";

  // Color dinámico según nivel
  let color, glow;
  if (nivel < 20) {
    color = "linear-gradient(180deg, #f43f5e, #9f1239)";
    glow  = "0 -8px 24px rgba(244, 63, 94, 0.6)";
  } else if (nivel < 50) {
    color = "linear-gradient(180deg, #fbbf24, #b45309)";
    glow  = "0 -8px 24px rgba(251, 191, 36, 0.55)";
  } else {
    color = "linear-gradient(180deg, #4cc9f0, #1d7fa8)";
    glow  = "0 -8px 24px rgba(76, 201, 240, 0.6)";
  }

  tankFill.style.background = color;
  tankFill.style.boxShadow = glow;

  // Etiqueta de porcentaje
  tankLevel.textContent = fmt(nivel, 1) + " %";
  tankLevel.style.color =
    nivel < 20 ? "#f43f5e" :
    nivel < 50 ? "#fbbf24" :
                 "#4cc9f0";
}

/* ====== TABLA ====== */
function addRow(data) {
  const row = document.createElement("tr");
  row.innerHTML = `
    <td>${timeString()}</td>
    <td>${fmt(data.distance_cm)} cm</td>
    <td>${data.sequence_id ?? "--"}</td>
  `;
  tableBody.prepend(row);

  while (tableBody.children.length > 10) {
    tableBody.removeChild(tableBody.lastChild);
  }
}

/* ====== ESTADO DEL SENSOR ====== */
function setSensorOK(ok, mensaje) {
  if (ok) {
    statusElement.textContent = "Activo";
    statusElement.style.color = "#4ade80";
    detailElement.textContent = mensaje ?? "Lectura recibida correctamente";
  } else {
    statusElement.textContent = "Sin datos";
    statusElement.style.color = "#f43f5e";
    detailElement.textContent = mensaje ?? "Sin lecturas todavía";
  }
}

/* ====== PROCESAMIENTO DE DATOS ====== */
function handleData(data) {
  if (data.type === "status") return;

  if (typeof data.distance_cm !== "number") {
    setSensorOK(false, "Paquete sin distancia válida");
    return;
  }

  distanceElement.textContent = fmt(data.distance_cm);
  nodeElement.textContent     = data.node_id ?? "--";
  sequenceElement.textContent = data.sequence_id ?? "--";

  setSensorOK(true);
  lastUpdateElement.textContent = "Última lectura: " + timeString();

  actualizarTanque(data.distance_cm);
  addRow(data);
}

/* ====== WEBSOCKET ====== */
function connect() {
  socket = new WebSocket(WS_URL);

  socket.onopen = () => {
    setConnection(true);
    console.log("✅ WebSocket conectado");
  };

  socket.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      handleData(data);
    } catch (error) {
      console.error("JSON inválido:", error);
    }
  };

  socket.onclose = () => {
    setConnection(false);
    setSensorOK(false, "Desconectado del servidor");
    console.log(" WebSocket desconectado. Reintentando en 2s...");
    setTimeout(connect, 2000);
  };

  socket.onerror = (error) => {
    console.error("WebSocket error:", error);
    try { socket.close(); } catch (_) {}
  };
}

/* ====== BOTÓN LIMPIAR ====== */
clearBtn.addEventListener("click", () => {
  tableBody.innerHTML = "";
});

/* ====== INICIO ====== */
setSensorOK(false);
actualizarTanque(DISTANCIA_VACIO); // arranca en 0%
connect();