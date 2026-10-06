const WS_URL = "ws://localhost:8000/ws/telemetria";

const distanceElement = document.getElementById("distance");
const nodeElement = document.getElementById("nodeId");
const sequenceElement = document.getElementById("sequence");
const statusElement = document.getElementById("sensorStatus");
const detailElement = document.getElementById("sensorDetail");
const lastUpdateElement = document.getElementById("lastUpdate");
const badgeElement = document.getElementById("connectionBadge");
const tableBody = document.getElementById("telemetryBody");
const clearBtn = document.getElementById("clearBtn");

let socket = null;

function setConnection(connected) {
  badgeElement.classList.toggle("online", connected);
  badgeElement.classList.toggle("offline", !connected);
  badgeElement.textContent = connected
    ? "● Conectado"
    : "● Desconectado";
}

function addRow(data) {
  const row = document.createElement("tr");

  const time = new Date().toLocaleTimeString();

  row.innerHTML = `
    <td>${time}</td>
    <td>${Number(data.distance_cm).toFixed(1)} cm</td>
    <td>${data.sequence_id}</td>
  `;

  tableBody.prepend(row);

  while (tableBody.children.length > 10) {
    tableBody.removeChild(tableBody.lastChild);
  }
}

function handleData(data) {
  if (data.type === "status") {
    return;
  }

  if (typeof data.distance_cm !== "number") {
    return;
  }

  distanceElement.textContent = data.distance_cm.toFixed(1);
  nodeElement.textContent = data.node_id ?? "--";
  sequenceElement.textContent = data.sequence_id ?? "--";

  statusElement.textContent = "Activo";
  detailElement.textContent = "Lectura recibida correctamente";

  lastUpdateElement.textContent =
    "Última lectura: " + new Date().toLocaleTimeString();

  addRow(data);
}

function connect() {
  socket = new WebSocket(WS_URL);

  socket.onopen = () => {
    setConnection(true);
    console.log("WebSocket conectado");
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
    console.log("WebSocket desconectado. Reintentando...");
    setTimeout(connect, 2000);
  };

  socket.onerror = (error) => {
    console.error("WebSocket error:", error);
    socket.close();
  };
}

clearBtn.addEventListener("click", () => {
  tableBody.innerHTML = "";
});

connect();
