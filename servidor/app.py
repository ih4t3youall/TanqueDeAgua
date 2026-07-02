#!/usr/bin/env python3
# ============================================================================
#  Servidor del medidor de tanque de agua  -  Python + Flask
# ----------------------------------------------------------------------------
#  - Recibe el estado del ESP32 (nivel, distancia, estado de bomba) por HTTP.
#  - Le devuelve la configuración actual (modo AUTO/MANUAL, comando manual,
#    umbrales) que vos definís desde la página web.
#  - Sirve una página web de monitoreo y control.
#
#  Cómo correrlo:
#     pip install -r requirements.txt
#     python app.py
#  Queda escuchando en http://0.0.0.0:5000  (entrá desde el navegador a
#  http://IP-DE-TU-SERVIDOR:5000).
#
#  Nota: el estado se guarda en memoria y, además, se persiste en config.json
#  para que los umbrales/modo sobrevivan a un reinicio del servidor.
# ============================================================================

import json
import time
import os
from flask import Flask, request, jsonify, render_template

app = Flask(__name__)

CONFIG_FILE = os.path.join(os.path.dirname(__file__), "config.json")

# Estado reportado por el ESP32 (volátil)
state = {
    "level_pct": 0,
    "distance_cm": 0,
    "pump_on": False,
    "rssi": 0,
    "last_seen": 0,      # timestamp del último reporte
}

# Configuración controlada desde la web (se persiste en disco)
config = {
    "modo": "AUTO",               # "AUTO" o "MANUAL"
    "manual_pump": False,         # en MANUAL: True = encender bomba
    "nivel_alto_corte": 95.0,     # % -> cortar
    "nivel_bajo_arranque": 60.0,  # % -> arrancar
}

ONLINE_TIMEOUT_S = 15  # si no reporta en 15 s, lo marcamos offline


def cargar_config():
    """Carga config.json si existe."""
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                config.update(json.load(f))
        except (json.JSONDecodeError, OSError):
            pass


def guardar_config():
    """Guarda la configuración en disco."""
    try:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2)
    except OSError:
        pass


# ---------------------------------------------------------------------------
#  API que usa el ESP32
# ---------------------------------------------------------------------------
@app.route("/api/status", methods=["POST"])
def api_status():
    """El ESP32 reporta su estado y recibe la configuración de vuelta."""
    data = request.get_json(force=True, silent=True) or {}

    state["level_pct"]   = data.get("level_pct", state["level_pct"])
    state["distance_cm"] = data.get("distance_cm", state["distance_cm"])
    state["pump_on"]     = bool(data.get("pump_on", state["pump_on"]))
    state["rssi"]        = data.get("rssi", state["rssi"])
    state["last_seen"]   = time.time()

    # Responder con la configuración actual
    return jsonify(config)


# ---------------------------------------------------------------------------
#  API que usa la página web
# ---------------------------------------------------------------------------
@app.route("/api/state")
def api_state():
    """Estado completo para refrescar el dashboard."""
    online = (time.time() - state["last_seen"]) < ONLINE_TIMEOUT_S
    segundos = int(time.time() - state["last_seen"]) if state["last_seen"] else None
    return jsonify({
        **state,
        "online": online,
        "segundos_desde_reporte": segundos,
        "config": config,
    })


@app.route("/api/control", methods=["POST"])
def api_control():
    """La web cambia modo, comando manual o umbrales."""
    data = request.get_json(force=True, silent=True) or {}

    if "modo" in data and data["modo"] in ("AUTO", "MANUAL"):
        config["modo"] = data["modo"]
    if "manual_pump" in data:
        config["manual_pump"] = bool(data["manual_pump"])
    if "nivel_alto_corte" in data:
        config["nivel_alto_corte"] = float(data["nivel_alto_corte"])
    if "nivel_bajo_arranque" in data:
        config["nivel_bajo_arranque"] = float(data["nivel_bajo_arranque"])

    guardar_config()
    return jsonify(config)


@app.route("/")
def index():
    return render_template("index.html")


if __name__ == "__main__":
    cargar_config()
    # host=0.0.0.0 -> accesible desde otras máquinas de la red
    app.run(host="0.0.0.0", port=5000, debug=False)
