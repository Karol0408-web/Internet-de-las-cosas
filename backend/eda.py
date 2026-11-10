"""EDA de la telemetría IoT (3er parcial). Ejecutar desde backend/: python eda.py"""
import json
import sqlite3

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns

DB = "telemetria.db"
NOMINALES = np.array([100, 500, 1000])
SENSORES = ["distance_cm", "turbidity_voltage_mv", "turbidity_pct"]

sns.set_theme(style="whitegrid")
pd.set_option("display.width", 200)
pd.set_option("display.max_columns", None)


# ---------- 1. Extracción ----------
def cargar(db: str = DB) -> pd.DataFrame:
    with sqlite3.connect(db) as conn:
        df = pd.read_sql_query(
            "SELECT id, sequence_id, variables_json, diagnostico, latencia_ms, fecha_registro "
            "FROM telemetria ORDER BY id ASC",
            conn,
        )
    variables = pd.json_normalize(
        df.pop("variables_json").apply(lambda s: json.loads(s) if s else {}).tolist()
    )
    return pd.concat([df, variables], axis=1)


# ---------- 2. Limpieza ----------
def limpiar(df: pd.DataFrame) -> pd.DataFrame:
    df["fecha_registro"] = pd.to_datetime(df["fecha_registro"])

    for c in SENSORES + ["interval_ms", "latencia_ms"]:
        if c not in df:
            df[c] = np.nan
        df[c] = pd.to_numeric(df[c], errors="coerce")

    print("Nulos por columna:\n", df.isna().sum(), "\n")

    # Si la secuencia retrocede, la ESP32 se reinició: nueva sesión
    df["sesion"] = (df["sequence_id"].diff() < 0).cumsum()

    # Intervalo real de llegada entre lecturas consecutivas
    df["delta_t_ms"] = df["fecha_registro"].diff().dt.total_seconds() * 1000
    df.loc[df["sesion"].diff().fillna(0) != 0, "delta_t_ms"] = np.nan

    # Intervalo configurado: el guardado en la BD; si no existe, el nominal más cercano
    est = df["delta_t_ms"].rolling(9, center=True, min_periods=3).median().bfill().ffill()
    inferido = pd.Series(
        NOMINALES[np.abs(est.to_numpy()[:, None] - NOMINALES).argmin(axis=1)],
        index=df.index,
    )
    df["intervalo_cfg"] = df["interval_ms"].fillna(inferido).astype(int)

    # Tramo = bloque continuo con misma sesión e intervalo (para contar pérdidas)
    cambio = (df["intervalo_cfg"] != df["intervalo_cfg"].shift()) | (
        df["sesion"] != df["sesion"].shift()
    )
    df["tramo"] = cambio.cumsum()
    return df


def limites_iqr(s: pd.Series) -> tuple[float, float]:
    q1, q3 = s.quantile([0.25, 0.75])
    iqr = q3 - q1
    return q1 - 1.5 * iqr, q3 + 1.5 * iqr


# ---------- 3. Estadística descriptiva ----------
def descriptivos(df: pd.DataFrame) -> pd.DataFrame:
    d = df[SENSORES + ["latencia_ms", "delta_t_ms"]]
    q1, q3 = d.quantile(0.25), d.quantile(0.75)
    iqr = q3 - q1
    return pd.DataFrame(
        {
            "media": d.mean(),
            "mediana": d.median(),
            "moda": d.mode().iloc[0],
            "desv_std": d.std(),
            "varianza": d.var(),
            "min": d.min(),
            "max": d.max(),
            "IQR": iqr,
            "outliers_IQR": ((d < q1 - 1.5 * iqr) | (d > q3 + 1.5 * iqr)).sum(),
        }
    ).round(2)


# ---------- 4. Métricas de red por intervalo ----------
def red(df: pd.DataFrame) -> pd.DataFrame:
    tramos = df.groupby("tramo").agg(
        intervalo=("intervalo_cfg", "first"),
        recibidos=("id", "size"),
        esperados=("sequence_id", lambda s: s.max() - s.min() + 1),
    )
    r = tramos.groupby("intervalo")[["recibidos", "esperados"]].sum()
    r["perdida_%"] = (1 - r["recibidos"] / r["esperados"]) * 100

    g = df.groupby("intervalo_cfg")
    r["L_media_ms"] = g["latencia_ms"].mean()
    r["L_mediana_ms"] = g["latencia_ms"].median()
    r["L_p95_ms"] = g["latencia_ms"].quantile(0.95)
    r["delta_t_real_ms"] = g["delta_t_ms"].median()
    r["jitter_ms"] = g["delta_t_ms"].std()
    r["desv_vs_config_%"] = (r["delta_t_real_ms"] / r.index - 1) * 100
    return r.round(2)


# ---------- 5. Gráficas ----------
def graficas(df: pd.DataFrame) -> None:
    fig, ax = plt.subplots(2, 2, figsize=(13, 9))

    ax[0, 0].plot(df["fecha_registro"], df["turbidity_voltage_mv"], lw=0.8)
    ax[0, 0].set(title="Serie temporal: turbidez", xlabel="Tiempo", ylabel="mV")
    ax[0, 0].tick_params(axis="x", rotation=30)

    sns.boxplot(data=df, x="intervalo_cfg", y="latencia_ms", ax=ax[0, 1])
    ax[0, 1].set(title="Boxplot de latencia por intervalo", xlabel="Intervalo (ms)", ylabel="L (ms)")

    sns.histplot(df["turbidity_voltage_mv"].dropna(), kde=True, ax=ax[1, 0])
    ax[1, 0].set(title="Histograma de turbidez", xlabel="mV")

    cols = ["turbidity_voltage_mv", "distance_cm", "latencia_ms", "delta_t_ms", "intervalo_cfg"]
    sns.heatmap(df[cols].corr(), annot=True, fmt=".2f", cmap="coolwarm", ax=ax[1, 1])
    ax[1, 1].set(title="Matriz de correlación")

    fig.tight_layout()
    fig.savefig("eda_panel.png", dpi=150)

    plt.figure(figsize=(8, 5))
    sns.histplot(
        data=df.dropna(subset=["latencia_ms"]),
        x="latencia_ms",
        hue="intervalo_cfg",
        kde=True,
        palette="tab10",
        common_norm=False,
    )
    plt.title("Distribución de latencia por intervalo")
    plt.xlabel("L (ms)")
    plt.tight_layout()
    plt.savefig("eda_latencia.png", dpi=150)


if __name__ == "__main__":
    df = limpiar(cargar())
    print(df.head(), "\n")

    desc = descriptivos(df)
    print("ESTADÍSTICOS DESCRIPTIVOS\n", desc, "\n")

    lat = df["latencia_ms"].dropna()
    _, limite = limites_iqr(lat)
    print(f"Latencias anómalas (> {limite:.1f} ms): {(lat > limite).sum()} de {len(lat)}")
    print(f"El 95 % de las latencias está por debajo de {lat.quantile(0.95):.1f} ms\n")

    metricas = red(df)
    print("MÉTRICAS DE RED POR INTERVALO\n", metricas)

    desc.to_csv("estadisticos.csv")
    metricas.to_csv("metricas_red.csv")
    graficas(df)
    plt.show()