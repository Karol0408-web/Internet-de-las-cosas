
const WS_URL = "ws://localhost:8000/ws/telemetria";

const DISTANCIA_VACIO = 15;
const DISTANCIA_LLENO = 1;

const $ = (id) => document.getElementById(id);

const distanceElement = $("distance");
const nodeElement = $("nodeId");
const sequenceElement = $("sequence");
const statusElement = $("sensorStatus");
const detailElement = $("sensorDetail");
const lastUpdateElement = $("lastUpdate");
const badgeElement = $("connectionBadge");
const tableBody = $("telemetryBody");
const clearBtn = $("clearBtn");
const tankFill = $("tankFill");
const tankLevel = $("tankLevel");
const turbidityStatusElement = $("turbidityStatus");
const turbidityDetailElement = $("turbidityDetail");
const qualityCurrent = $("qualityCurrent");
const qualityDetail = $("qualityDetail");

let socket = null;
let reconnectTimer = null;

const DIAGNOSTICOS = {
  AGUA_CLARA: {
    texto: "Agua clara",
    color: "#27866d"
  },
  AGUA_POCO_TURBIA: {
    texto: "Poco turbia",
    color: "#82983d"
  },
  AGUA_MEDIA_TURBIA: {
    texto: "Turbidez media",
    color: "#c58a2d"
  },
  AGUA_TURBIA: {
    texto: "Agua turbia",
    color: "#c75449"
  },
  SIN_CALIBRAR: {
    texto: "Sin calibrar",
    color: "#c58a2d"
  },
  RECALIBRAR: {
    texto: "Recalibrar sensor",
    color: "#c58a2d"
  },
  FUERA_DE_RANGO: {
    texto: "Fuera de rango",
    color: "#c75449"
  }
};

function fmt(n, digits = 1) {
  return Number(n).toFixed(digits);
}

function timeString(d = new Date()) {
  return [d.getHours(), d.getMinutes(), d.getSeconds()]
    .map(n => String(n).padStart(2, "0"))
    .join(":");
}

function setConnection(connected) {
  badgeElement.classList.toggle("online", connected);
  badgeElement.classList.toggle("offline", !connected);
  badgeElement.textContent = connected
    ? "● Conectado"
    : "● Desconectado";
}

function actualizarTanque(distanciaCm) {
  const rango = DISTANCIA_VACIO - DISTANCIA_LLENO;
  if (!Number.isFinite(distanciaCm) || rango <= 0) return;

  let nivel =
    ((DISTANCIA_VACIO - distanciaCm) / rango) * 100;

  nivel = Math.max(0, Math.min(100, nivel));

  tankFill.style.height = nivel + "%";

  // Paleta discreta según el nivel del tanque.
  if (nivel < 20) {
    tankFill.style.background =
      "linear-gradient(180deg, #e5a59b, #c75449)";
  } else if (nivel < 50) {
    tankFill.style.background =
      "linear-gradient(180deg, #e8c878, #c58a2d)";
  } else {
    tankFill.style.background =
      "linear-gradient(180deg, #8dd9e2, #238ca3 55%, #176476)";
  }

  tankLevel.textContent = fmt(nivel) + " %";
  tankLevel.style.color =
    nivel < 20 ? "#c75449" :
    nivel < 50 ? "#b17b25" :
    "#238ca3";
}

function actualizarTurbidez(data) {
  const codigo = data.diagnostico_turbidez;
  const diagnostico = DIAGNOSTICOS[codigo];

  // Actualiza el indicador de los datos generales.
  if (turbidityStatusElement) {
    turbidityStatusElement.textContent =
      diagnostico?.texto ?? "--";

    turbidityStatusElement.style.color =
      diagnostico?.color ?? "";
  }

  let detalle = "Sin lectura válida";

  if (typeof data.turbidity_pct === "number") {
    detalle = `${data.turbidity_pct} % turbia`;
  }

  if (typeof data.turbidity_voltage_mv === "number") {
    detalle += ` · ${data.turbidity_voltage_mv} mV`;
  }

  if (turbidityDetailElement) {
    turbidityDetailElement.textContent = detalle;
  }

  if (qualityDetail) {
    qualityDetail.textContent = detalle;
  }

  if (qualityCurrent) {
    qualityCurrent.textContent =
      diagnostico?.texto ?? "Sin diagnóstico válido";

    qualityCurrent.style.color =
      diagnostico?.color ?? "";
  }

  // Resalta únicamente el segmento correspondiente
  // al diagnóstico enviado por el ESP32.
  document.querySelectorAll(".turbidity-segment")
    .forEach(segment => {
      const activo =
        diagnostico &&
        segment.dataset.diagnosis === codigo;

      segment.classList.toggle("active", Boolean(activo));
    });
}

function setSensorOK(ok, mensaje) {
  statusElement.textContent = ok ? "Activo" : "Sin datos";

  statusElement.style.color =
    ok ? "#27866d" : "#c75449";

  detailElement.textContent = mensaje ??
    (ok
      ? "Lectura recibida correctamente"
      : "Esperando lecturas");
}

function addRow(data) {
  const row = document.createElement("tr");

  const values = [
    timeString(),
    typeof data.distance_cm === "number"
      ? fmt(data.distance_cm) + " cm"
      : "--",
    DIAGNOSTICOS[data.diagnostico_turbidez]?.texto ?? "--",
    data.sequence_id ?? "--"
  ];

  values.forEach(value => {
    const cell = document.createElement("td");
    cell.textContent = String(value);
    row.appendChild(cell);
  });

  tableBody.prepend(row);

  while (tableBody.children.length > 10) {
    tableBody.removeChild(tableBody.lastElementChild);
  }
}

function handleData(data) {
  if (data.type === "status") return;

  nodeElement.textContent = data.node_id ?? "--";
  sequenceElement.textContent = data.sequence_id ?? "--";

  lastUpdateElement.textContent =
    "Última lectura: " + timeString();

  actualizarTurbidez(data);

  if (typeof data.distance_cm === "number") {
    distanceElement.textContent = fmt(data.distance_cm);
    actualizarTanque(data.distance_cm);
    setSensorOK(true);
  } else {
    distanceElement.textContent = "--";
    setSensorOK(false, "Paquete sin distancia válida");
  }

  addRow(data);
}

function connect() {
  // Evita conexiones duplicadas.
  if (
    socket &&
    (socket.readyState === WebSocket.OPEN ||
     socket.readyState === WebSocket.CONNECTING)
  ) {
    return;
  }

  socket = new WebSocket(WS_URL);

  socket.onopen = () => {
    setConnection(true);
    console.log("WebSocket conectado");
  };

  socket.onmessage = event => {
    try {
      handleData(JSON.parse(event.data));
    } catch (error) {
      console.error("Error procesando el mensaje:", error);
    }
  };

  socket.onclose = () => {
    setConnection(false);
    setSensorOK(false, "Desconectado del servidor");

    if (reconnectTimer === null) {
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connect();
      }, 2000);
    }
  };

  socket.onerror = error => {
    console.error("Error de WebSocket:", error);
    socket.close();
  };
}

clearBtn.addEventListener("click", () => {
  tableBody.replaceChildren();
});

setConnection(false);
setSensorOK(false);
actualizarTanque(DISTANCIA_VACIO);
connect();
