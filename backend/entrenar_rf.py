"""Random Forest para clasificar el estado del agua. Ejecutar desde backend/: python entrenar_rf.py"""
import joblib
import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.model_selection import train_test_split

from eda import cargar, limpiar

ETIQUETAS = {
    "AGUA_CLARA": 0,
    "AGUA_POCO_TURBIA": 1,     
    "AGUA_MEDIA_TURBIA": 1,
    "AGUA_TURBIA": 2,
}
NOMBRES = ["Normal", "Advertencia", "Crítico"]
FEATURES = ["turbidity_voltage_mv", "distance_cm", "latencia_ms", "delta_t_ms"]

# ---------- 1. Variables y división ----------
df = limpiar(cargar())
df["estado_sistema"] = df["diagnostico"].map(ETIQUETAS)
df = df.dropna(subset=FEATURES + ["estado_sistema"])
df["estado_sistema"] = df["estado_sistema"].astype(int)

print("Registros por clase:\n", df["estado_sistema"].value_counts().sort_index(), "\n")

X = df[FEATURES].to_numpy()
y = df["estado_sistema"].to_numpy()
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.2, random_state=42, stratify=y
)

# ---------- 2. Entrenamiento ----------
rf_model = RandomForestClassifier(
    n_estimators=100, max_depth=10, criterion="gini", random_state=42
)
rf_model.fit(X_train, y_train)

# ---------- 3. Evaluación ----------
y_pred = rf_model.predict(X_test)
etiquetas = [0, 1, 2]

print("--- REPORTE DE CLASIFICACIÓN ---")
print(classification_report(y_test, y_pred, labels=etiquetas, target_names=NOMBRES, zero_division=0))
print(f"Accuracy: {accuracy_score(y_test, y_pred):.4f}")

(
    pd.DataFrame(
        classification_report(
            y_test, y_pred, labels=etiquetas, target_names=NOMBRES,
            zero_division=0, output_dict=True,
        )
    )
    .T.round(3)
    .to_csv("metricas_rf.csv")
)

cm = confusion_matrix(y_test, y_pred, labels=etiquetas)
plt.figure(figsize=(6, 5))
sns.heatmap(cm, annot=True, fmt="d", cmap="Blues", xticklabels=NOMBRES, yticklabels=NOMBRES)
plt.xlabel("Predicción del modelo")
plt.ylabel("Valor real")
plt.title("Matriz de confusión - Random Forest IoT")
plt.tight_layout()
plt.savefig("matriz_confusion.png", dpi=150)

# ---------- 4. Importancia de características ----------
importancia = pd.DataFrame(
    {"Característica": FEATURES, "Importancia": rf_model.feature_importances_}
).sort_values("Importancia", ascending=False)
print("\n", importancia)
importancia.to_csv("importancia_features.csv", index=False)

plt.figure(figsize=(7, 4))
sns.barplot(data=importancia, x="Importancia", y="Característica", color="#238ca3")
plt.title("Importancia de características")
plt.tight_layout()
plt.savefig("importancia_features.png", dpi=150)

# ---------- 5. Serialización y prueba de inferencia ----------
joblib.dump(rf_model, "modelo_random_forest_iot.pkl")

modelo = joblib.load("modelo_random_forest_iot.pkl")
nueva_lectura = [[df["turbidity_voltage_mv"].median(), 8.0, 100.0, 1000.0]]
print(f"\nPredicción de prueba {nueva_lectura[0]}: {NOMBRES[int(modelo.predict(nueva_lectura)[0])]}")

plt.show()