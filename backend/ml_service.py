import joblib
import numpy as np

NOMBRES = ["Normal", "Advertencia", "Crítico"]
modelo = joblib.load("modelo_random_forest_iot.pkl")


def clasificar(turbidity_mv, distance_cm, latencia_ms, delta_t_ms) -> str | None:
    """Orden del vector igual al de entrenamiento: [turbidez mV, distancia, latencia, delta_t]."""
    vector = [turbidity_mv, distance_cm, latencia_ms, delta_t_ms]
    if any(v is None for v in vector):
        return None
    entrada = np.array(vector, dtype=float).reshape(1, -1)
    return NOMBRES[int(modelo.predict(entrada)[0])]