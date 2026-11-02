
"use strict";

// ---------- Configuración WebSocket ----------
const WS_URL = `ws://${window.location.hostname}:8000/ws/telemetria`;

const DISTANCIA_VACIO = 15;
const DISTANCIA_LLENO = 1;
const MAX_PUNTOS = 60;
const RECONNECT_DELAY = 2000;

// ---------- Elementos HTML ----------
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

// ---------- Estado de conexión ----------
let socket = null;
let reconnectTimer = null;
let reconnectAttempts = 0;
let intentionalClose = false;

// ---------- Diagnósticos de turbidez ----------
const DIAGNOSTICOS = {
  AGUA_CLARA: { texto: "Agua clara", color: "#27866d" },
  Normal: { texto: "Agua clara", color: "#27866d" },
  AGUA_POCO_TURBIA: { texto: "Poco turbia", color: "#82983d" },
  Advertencia: { texto: "Poco turbia", color: "#82983d" },
  AGUA_MEDIA_TURBIA: { texto: "Turbidez media", color: "#c58a2d" },
  AGUA_TURBIA: { texto: "Agua turbia", color: "#c75449" },
  Critico: { texto: "Agua turbia (Crítico)", color: "#c75449" },
  SIN_CALIBRAR: { texto: "Sin calibrar", color: "#c58a2d" },
  RECALIBRAR: { texto: "Recalibrar sensor", color: "#c58a2d" },
  FUERA_DE_RANGO: { texto: "Fuera de rango", color: "#c75449" }
};

// ---------- Gráfica ----------
const chart =
  window.Chart && $("chart")
    ? new Chart($("chart"), {
        type: "line",
        data: {
          labels: [],
          datasets: [
            {
              label: "Distancia (cm)",
              data: [],
              borderColor: "#238ca3",
              yAxisID: "y",
              tension: 0.3,
              pointRadius: 0
            },
            {
              label: "Turbidez (mV)",
              data: [],
              borderColor: "#c58a2d",
              yAxisID: "y1",
              tension: 0.3,
              pointRadius: 0
            }
          ]
        },
        options: {
          animation: false,
          maintainAspectRatio: false,
          scales: {
            y: {
              title: { display: true, text: "cm" }
            },
            y1: {
              position: "right",
              grid: { drawOnChartArea: false },
              title: { display: true, text: "mV" }
            }
          }
        }
      })
    : null;

// ---------- Utilidades ----------
function fmt(n, digits = 1) {
  const value = Number(n);
  return Number.isFinite(value) ? value.toFixed(digits) : "--";
}

function timeString(d = new Date()) {
  return [d.getHours(), d.getMinutes(), d.getSeconds()]
    .map((n) => String(n).padStart(2, "0"))
    .join(":");
}

function setConnection(connected) {
  if (!badgeElement) return;

  badgeElement.classList.toggle("online", connected);
  badgeElement.classList.toggle("offline", !connected);
  badgeElement.textContent = connected
    ? "● Conectado"
    : "● Desconectado";
}

// ---------- Tanque ----------
function actualizarTanque(distanciaCm) {
  if (!tankFill || !tankLevel) return;

  const distancia = Number(distanciaCm);
  const rango = DISTANCIA_VACIO - DISTANCIA_LLENO;

  if (!Number.isFinite(distancia) || rango <= 0) return;

  let nivel =
    ((DISTANCIA_VACIO - distancia) / rango) * 100;

  nivel = Math.max(0, Math.min(100, nivel));

  tankFill.style.height = nivel + "%";

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
    nivel < 20
      ? "#c75449"
      : nivel < 50
        ? "#b17b25"
        : "#238ca3";
}

// ---------- Turbidez ----------
function actualizarTurbidez(data) {
  const codigo =
    data.diagnostico ||
    data.diagnostico_turbidez ||
    "AGUA_CLARA";

  const diagnostico = DIAGNOSTICOS[codigo];

  if (turbidityStatusElement) {
    turbidityStatusElement.textContent =
      diagnostico?.texto ?? codigo;

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
      diagnostico?.texto ?? codigo ?? "Sin diagnóstico válido";

    qualityCurrent.style.color = diagnostico?.color ?? "";
  }

  document.querySelectorAll(".turbidity-segment").forEach((segment) => {
    const activo =
      diagnostico &&
      segment.dataset.diagnosis === codigo;

    segment.classList.toggle("active", Boolean(activo));
  });
}

// ---------- Gráfica ----------
function actualizarGrafica(data) {
  if (!chart) return;

  chart.data.labels.push(timeString());
  chart.data.datasets[0].data.push(
    data.distance_cm ?? null
  );
  chart.data.datasets[1].data.push(
    data.turbidity_voltage_mv ?? data.var1 ?? null
  );

  if (chart.data.labels.length > MAX_PUNTOS) {
    chart.data.labels.shift();

    chart.data.datasets.forEach((dataset) => {
      dataset.data.shift();
    });
  }

  chart.update("none");
}

// ---------- Métricas ----------
function actualizarMetricas(raw) {
  const m = raw.metricas;
  if (!m) return;

  if ($("metricL")) {
    $("metricL").textContent = raw.latencia_ms ?? "--";
  }

  if ($("metricLavg")) {
    $("metricLavg").textContent = m.L_prom ?? "--";
  }

  if ($("metricP")) {
    $("metricP").textContent = m.P ?? "--";
  }

  if ($("metricRx")) {
    $("metricRx").textContent =
      `${m.recibidos ?? 0} / ${m.esperados ?? 0} paquetes`;
  }
}

// ---------- Estado de los controles ----------
function actualizarControl(data) {
  if ($("relayState")) {
    const estadoRelay =
      data.relay !== undefined
        ? data.relay
        : data.relay_state;

    if (estadoRelay !== undefined) {
      $("relayState").textContent =
        estadoRelay ? "Encendido" : "Apagado";
    }
  }

  if ($("intervalState") && data.interval_ms) {
    $("intervalState").textContent =
      data.interval_ms + " ms";
  }

  document.querySelectorAll("[data-interval]").forEach((button) => {
    button.classList.toggle(
      "active",
      Number(button.dataset.interval) === data.interval_ms
    );
  });
}

// ---------- Envío de comandos ----------
function enviar(cmd) {
  if (socket?.readyState === WebSocket.OPEN) {
    try {
      socket.send(JSON.stringify(cmd));
    } catch (error) {
      console.error("[WS] No se pudo enviar el comando:", error);
    }
  } else {
    console.warn(
      "[WS] No se envió el comando: conexión no disponible."
    );
  }
}

document.querySelectorAll("[data-cmd]").forEach((button) => {
  button.addEventListener("click", () => {
    try {
      enviar(JSON.parse(button.dataset.cmd));
    } catch (error) {
      console.error(
        "[CMD] Error al interpretar el comando del botón:",
        error
      );
    }
  });
});

// ---------- Estado del sensor ----------
function setSensorOK(ok, mensaje) {
  if (statusElement) {
    statusElement.textContent = ok ? "Activo" : "Sin datos";
    statusElement.style.color = ok ? "#27866d" : "#c75449";
  }

  if (detailElement) {
    detailElement.textContent =
      mensaje ??
      (ok
        ? "Lectura recibida correctamente"
        : "Esperando lecturas");
  }
}

// ---------- Tabla de telemetría ----------
function addRow(data) {
  if (!tableBody) return;

  const row = document.createElement("tr");

  const diagCodigo =
    data.diagnostico || data.diagnostico_turbidez;

  const values = [
    timeString(),
    typeof data.distance_cm === "number"
      ? fmt(data.distance_cm) + " cm"
      : "--",
    DIAGNOSTICOS[diagCodigo]?.texto ?? diagCodigo ?? "--",
    data.sequence_id ?? "--"
  ];

  values.forEach((value) => {
    const cell = document.createElement("td");
    cell.textContent = String(value);
    row.appendChild(cell);
  });

  tableBody.prepend(row);

  while (tableBody.children.length > 10) {
    tableBody.removeChild(tableBody.lastElementChild);
  }
}

// ---------- Procesamiento de datos entrantes ----------
function handleData(raw) {
  if (!raw || typeof raw !== "object") return;

  if (raw.type === "status") {
    console.log("[WS]", raw.message);
    return;
  }

  // Combina los datos principales con el objeto data, si existe.
  const data = {
    ...raw,
    ...(raw.data || {})
  };

  if (nodeElement) {
    nodeElement.textContent = data.node_id ?? "--";
  }

  if (sequenceElement) {
    sequenceElement.textContent = data.sequence_id ?? "--";
  }

  if (lastUpdateElement) {
    lastUpdateElement.textContent =
      "Última lectura: " + timeString();
  }

  actualizarTurbidez(data);

  if (typeof data.distance_cm === "number") {
    if (distanceElement) {
      distanceElement.textContent = fmt(data.distance_cm);
    }

    actualizarTanque(data.distance_cm);
    setSensorOK(true);
  } else {
    if (distanceElement) {
      distanceElement.textContent = "--";
    }

    setSensorOK(false, "Paquete sin distancia válida");
  }

  actualizarGrafica(data);
  actualizarMetricas(raw);
  actualizarControl(data);
  addRow(data);
}

// ---------- Reconexión WebSocket ----------
function programarReconexión() {
  if (intentionalClose || reconnectTimer !== null) return;

  // Evita intentar reconectar indefinidamente a intervalos muy cortos.
  const delay = Math.min(
    RECONNECT_DELAY * (2 ** reconnectAttempts),
    15000
  );

  reconnectAttempts++;

  console.log(
    `[WS] Próximo intento de conexión en ${delay / 1000} s`
  );

  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, delay);
}

function connect() {
  if (intentionalClose) return;

  // No crear otra conexión si ya existe una activa o en proceso.
  if (
    socket &&
    (
      socket.readyState === WebSocket.OPEN ||
      socket.readyState === WebSocket.CONNECTING
    )
  ) {
    return;
  }

  console.log("[WS] Intentando conectar a:", WS_URL);

  let ws;

  try {
    ws = new WebSocket(WS_URL);
    socket = ws;
  } catch (error) {
    console.error("[WS] No se pudo crear la conexión:", error);
    setConnection(false);
    programarReconexión();
    return;
  }

  ws.onopen = () => {
    // Ignorar eventos de una conexión anterior.
    if (socket !== ws) return;

    reconnectAttempts = 0;
    setConnection(true);

    console.log("[WS] Conectado exitosamente");
  };

  ws.onmessage = (event) => {
    if (socket !== ws) return;

    try {
      const data = JSON.parse(event.data);
      handleData(data);
    } catch (error) {
      console.error(
        "[WS] Error procesando el mensaje:",
        error
      );
    }
  };

  ws.onerror = (error) => {
    console.error("[WS] Error de comunicación:", error);

    // No cerrar manualmente aquí.
    // El evento onclose se encargará de programar la reconexión.
  };

  ws.onclose = (event) => {
    console.warn(
      `[WS] Conexión cerrada. Código: ${event.code}. Motivo: ${
        event.reason || "No especificado"
      }`
    );

    // Una conexión antigua no debe alterar el estado de la nueva.
    if (socket !== ws) return;

    socket = null;

    setConnection(false);
    setSensorOK(false, "Desconectado del servidor");

    programarReconexión();
  };
}

// ---------- Limpiar tabla ----------
if (clearBtn) {
  clearBtn.addEventListener("click", () => {
    if (tableBody) {
      tableBody.replaceChildren();
    }
  });
}

// ---------- Inicialización ----------
setConnection(false);
setSensorOK(false);
actualizarTanque(DISTANCIA_VACIO);

connect();

// Evita dejar un temporizador de reconexión si se abandona la página.
window.addEventListener("pagehide", () => {
  intentionalClose = true;

  if (reconnectTimer !== null) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }

  if (socket) {
    const currentSocket = socket;
    socket = null;

    if (
      currentSocket.readyState === WebSocket.OPEN ||
      currentSocket.readyState === WebSocket.CONNECTING
    ) {
      currentSocket.close(1000, "Página cerrada");
    }
  }
});
