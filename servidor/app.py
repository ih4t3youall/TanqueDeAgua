#!/usr/bin/env python3
# ============================================================================
#  Servidor del medidor de tanque de agua  -  Python + Flask
# ----------------------------------------------------------------------------
#  Qué hace:
#    - LOGIN con usuario y contraseña para la web (sesión por token en cookie).
#      La contraseña se guarda HASHEADA en config.json (nunca en texto plano).
#    - El ESP32 (nodo tanque) se autentica con un TOKEN fijo en el header
#      "X-API-Token". Sin token válido, el reporte se rechaza (401).
#    - El nodo tanque reporta cada 10 segundos.
#    - Modo AUTO: carga por debajo del 30 % y corta al llegar al 80 %.
#    - Modo MANUAL: la bomba se enciende/apaga desde la web.
#
#  Seguridad (pensado para correr en un hosting expuesto a internet):
#    - Contraseña web hasheada (scrypt via werkzeug). Para cambiarla:
#         python app.py --set-password
#    - Límite de intentos: 5 fallos de login (o de token) por IP cada 10 min.
#    - Headers de seguridad (CSP, nosniff, no-frame) en todas las respuestas.
#    - Body máximo de 16 KB; los datos del nodo se validan y acotan.
#    - Si hay un reverse proxy con HTTPS adelante, correr con TRUST_PROXY=1
#      para que la IP real y la cookie "secure" funcionen bien.
#
#  Cómo correrlo:
#     pip install -r requirements.txt
#     python app.py
#  Queda escuchando en http://0.0.0.0:5000 (con waitress si está instalado)
# ============================================================================

import getpass
import json
import os
import secrets
import sys
import time
from functools import wraps

from flask import (Flask, jsonify, make_response, redirect, render_template,
                   request)
from werkzeug.security import check_password_hash, generate_password_hash

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 16 * 1024  # nadie necesita mandar más de 16 KB

# Detrás de un reverse proxy (nginx/hosting con HTTPS), correr con TRUST_PROXY=1
# para que request.remote_addr sea la IP real del cliente y no la del proxy.
if os.environ.get("TRUST_PROXY") == "1":
    from werkzeug.middleware.proxy_fix import ProxyFix
    app.wsgi_app = ProxyFix(app.wsgi_app, x_for=1, x_proto=1)

CONFIG_FILE = os.path.join(os.path.dirname(__file__), "config.json")

# ---------------------------------------------------------------------------
#  Estado y configuración
# ---------------------------------------------------------------------------

# Estado reportado por el ESP32 (volátil, vive en memoria)
state = {
    "level_pct": 0,
    "distance_cm": 0,
    "pump_on": False,
    "rssi": 0,
    "last_seen": 0,      # timestamp del último reporte
}

# Configuración (se persiste en config.json)
config = {
    # --- seguridad ---
    "web_user": "admin",
    "web_password_hash": "",  # se genera al primer arranque o con --set-password
    "device_token": "kame-tank-7f3a9c2e51d84b06",
    # --- control de la bomba ---
    "modo": "AUTO",               # "AUTO" o "MANUAL"
    "manual_pump": False,         # en MANUAL: True = bomba encendida
    "nivel_alto_corte": 80.0,     # % -> deja de cargar
    "nivel_bajo_arranque": 30.0,  # % -> empieza a cargar
}

ONLINE_TIMEOUT_S = 25   # el nodo reporta cada 10 s; 25 s sin noticias = offline
SESSION_TTL_S = 12 * 3600  # las sesiones web duran 12 horas

# Sesiones web activas: token -> vencimiento (en memoria; un reinicio
# del servidor cierra todas las sesiones, hay que volver a loguearse)
sessions = {}


def cargar_config():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                config.update(json.load(f))
        except (json.JSONDecodeError, OSError):
            pass

    # Migración: si quedó una contraseña en texto plano de una versión
    # anterior, se reemplaza por su hash y se borra del archivo.
    password_plana = config.pop("web_password", None)
    if password_plana and not config.get("web_password_hash"):
        config["web_password_hash"] = generate_password_hash(password_plana)

    # Primer arranque sin contraseña: se genera una al azar y se muestra
    # UNA sola vez por consola (cambiarla después con --set-password).
    if not config.get("web_password_hash"):
        password_nueva = secrets.token_urlsafe(12)
        config["web_password_hash"] = generate_password_hash(password_nueva)
        print("=" * 60)
        print("  Se generó una contraseña web nueva (guardala ahora):")
        print(f"     usuario:    {config['web_user']}")
        print(f"     contraseña: {password_nueva}")
        print("  Podés cambiarla con:  python app.py --set-password")
        print("=" * 60)

    guardar_config()


def guardar_config():
    try:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2)
        os.chmod(CONFIG_FILE, 0o600)  # solo el dueño puede leer el token/hash
    except OSError:
        pass


# ---------------------------------------------------------------------------
#  Límite de intentos (anti fuerza bruta)
# ---------------------------------------------------------------------------
#  Por cada IP se recuerdan los intentos fallidos de los últimos 10 minutos.
#  Al quinto fallo, esa IP queda bloqueada hasta que la ventana se limpie.

MAX_FALLOS = 5
VENTANA_FALLOS_S = 600
fallos = {}  # clave ("login:IP" o "token:IP") -> [timestamps de fallos]


def _limpiar_fallos(clave):
    ahora = time.time()
    lista = [t for t in fallos.get(clave, []) if ahora - t < VENTANA_FALLOS_S]
    if lista:
        fallos[clave] = lista
    else:
        fallos.pop(clave, None)
    return lista


def bloqueado(clave):
    return len(_limpiar_fallos(clave)) >= MAX_FALLOS


def registrar_fallo(clave):
    fallos.setdefault(clave, []).append(time.time())


def ip_cliente():
    return request.remote_addr or "?"


# ---------------------------------------------------------------------------
#  Headers de seguridad en todas las respuestas
# ---------------------------------------------------------------------------

@app.after_request
def headers_seguridad(resp):
    resp.headers.setdefault("X-Content-Type-Options", "nosniff")
    resp.headers.setdefault("X-Frame-Options", "DENY")
    resp.headers.setdefault("Referrer-Policy", "no-referrer")
    # Los templates usan CSS y JS inline (autocontenidos, sin CDNs)
    resp.headers.setdefault(
        "Content-Security-Policy",
        "default-src 'self'; style-src 'self' 'unsafe-inline'; "
        "script-src 'self' 'unsafe-inline'; frame-ancestors 'none'")
    if request.is_secure:
        resp.headers.setdefault("Strict-Transport-Security", "max-age=31536000")
    return resp


# ---------------------------------------------------------------------------
#  Autenticación
# ---------------------------------------------------------------------------

def crear_sesion():
    token = secrets.token_hex(32)
    sessions[token] = time.time() + SESSION_TTL_S
    return token


def sesion_valida(token):
    if not token or token not in sessions:
        return False
    if time.time() > sessions[token]:
        del sessions[token]
        return False
    return True


def requiere_login_pagina(f):
    """Protege páginas HTML: sin sesión, redirige al login."""
    @wraps(f)
    def wrapper(*args, **kwargs):
        if not sesion_valida(request.cookies.get("session")):
            return redirect("/login")
        return f(*args, **kwargs)
    return wrapper


def requiere_login_api(f):
    """Protege la API de la web: sin sesión, responde 401."""
    @wraps(f)
    def wrapper(*args, **kwargs):
        if not sesion_valida(request.cookies.get("session")):
            return jsonify({"error": "no autorizado"}), 401
        return f(*args, **kwargs)
    return wrapper


def requiere_token_dispositivo(f):
    """Protege la API del ESP32: exige el header X-API-Token correcto."""
    @wraps(f)
    def wrapper(*args, **kwargs):
        clave = "token:" + ip_cliente()
        if bloqueado(clave):
            return jsonify({"error": "demasiados intentos"}), 429
        token = request.headers.get("X-API-Token", "")
        if not secrets.compare_digest(token, config["device_token"]):
            registrar_fallo(clave)
            return jsonify({"error": "token invalido"}), 401
        return f(*args, **kwargs)
    return wrapper


# ---------------------------------------------------------------------------
#  Login / logout
# ---------------------------------------------------------------------------

@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "GET":
        if sesion_valida(request.cookies.get("session")):
            return redirect("/")
        return render_template("login.html", error=None)

    clave = "login:" + ip_cliente()
    if bloqueado(clave):
        return render_template(
            "login.html",
            error="Demasiados intentos fallidos. Esperá 10 minutos."), 429

    usuario = request.form.get("usuario", "")
    password = request.form.get("password", "")
    usuario_ok = secrets.compare_digest(usuario, config["web_user"])
    password_ok = check_password_hash(config["web_password_hash"], password)
    if usuario_ok and password_ok:
        resp = make_response(redirect("/"))
        resp.set_cookie("session", crear_sesion(),
                        httponly=True, samesite="Lax",
                        secure=request.is_secure,
                        max_age=SESSION_TTL_S)
        return resp
    registrar_fallo(clave)
    return render_template("login.html", error="Usuario o contraseña incorrectos"), 401


@app.route("/logout")
def logout():
    token = request.cookies.get("session")
    sessions.pop(token, None)
    resp = make_response(redirect("/login"))
    resp.delete_cookie("session")
    return resp


# ---------------------------------------------------------------------------
#  API que usa el ESP32 (autenticada por token)
# ---------------------------------------------------------------------------

def _numero(valor, minimo, maximo, previo):
    """Convierte a float y lo acota al rango; si no es un número, deja el previo."""
    try:
        v = float(valor)
    except (TypeError, ValueError):
        return previo
    if v != v:  # NaN
        return previo
    return max(minimo, min(maximo, v))


@app.route("/api/status", methods=["POST"])
@requiere_token_dispositivo
def api_status():
    """El ESP32 reporta su estado cada 10 s y recibe la configuración."""
    data = request.get_json(force=True, silent=True) or {}

    state["level_pct"]   = _numero(data.get("level_pct"), 0, 100, state["level_pct"])
    state["distance_cm"] = _numero(data.get("distance_cm"), 0, 1000, state["distance_cm"])
    state["pump_on"]     = bool(data.get("pump_on", state["pump_on"]))
    state["rssi"]        = _numero(data.get("rssi"), -120, 0, state["rssi"])
    state["last_seen"]   = time.time()

    # Solo lo que el nodo necesita (no le mandamos las credenciales web)
    return jsonify({
        "modo": config["modo"],
        "manual_pump": config["manual_pump"],
        "nivel_alto_corte": config["nivel_alto_corte"],
        "nivel_bajo_arranque": config["nivel_bajo_arranque"],
    })


# ---------------------------------------------------------------------------
#  API que usa la página web (autenticada por sesión)
# ---------------------------------------------------------------------------

@app.route("/api/state")
@requiere_login_api
def api_state():
    """Estado completo para refrescar el dashboard."""
    online = (time.time() - state["last_seen"]) < ONLINE_TIMEOUT_S
    segundos = int(time.time() - state["last_seen"]) if state["last_seen"] else None
    return jsonify({
        **state,
        "online": online,
        "segundos_desde_reporte": segundos,
        "config": {
            "modo": config["modo"],
            "manual_pump": config["manual_pump"],
            "nivel_alto_corte": config["nivel_alto_corte"],
            "nivel_bajo_arranque": config["nivel_bajo_arranque"],
        },
    })


@app.route("/api/control", methods=["POST"])
@requiere_login_api
def api_control():
    """La web cambia el modo (AUTO/MANUAL), la bomba manual o los umbrales."""
    data = request.get_json(force=True, silent=True) or {}

    if "modo" in data and data["modo"] in ("AUTO", "MANUAL"):
        config["modo"] = data["modo"]
        # Al pasar a MANUAL, arrancamos con la bomba apagada por las dudas
        if config["modo"] == "MANUAL" and "manual_pump" not in data:
            config["manual_pump"] = False
    if "manual_pump" in data:
        config["manual_pump"] = bool(data["manual_pump"])

    # Umbrales: se validan juntos (el bajo tiene que quedar por debajo del alto)
    if "nivel_alto_corte" in data or "nivel_bajo_arranque" in data:
        try:
            alto = float(data.get("nivel_alto_corte", config["nivel_alto_corte"]))
            bajo = float(data.get("nivel_bajo_arranque", config["nivel_bajo_arranque"]))
        except (TypeError, ValueError):
            return jsonify({"error": "umbrales invalidos"}), 400
        if not (0 <= bajo < alto <= 100):
            return jsonify({"error": "los umbrales deben cumplir 0 <= bajo < alto <= 100"}), 400
        config["nivel_alto_corte"] = alto
        config["nivel_bajo_arranque"] = bajo

    guardar_config()
    return jsonify({
        "modo": config["modo"],
        "manual_pump": config["manual_pump"],
        "nivel_alto_corte": config["nivel_alto_corte"],
        "nivel_bajo_arranque": config["nivel_bajo_arranque"],
    })


@app.route("/")
@requiere_login_pagina
def index():
    return render_template("index.html")


def cambiar_password():
    """Modo CLI: python app.py --set-password"""
    cargar_config()
    password = getpass.getpass("Nueva contraseña web (mínimo 8 caracteres): ")
    if len(password) < 8:
        print("Muy corta. No se cambió nada.")
        sys.exit(1)
    if password != getpass.getpass("Repetila: "):
        print("No coinciden. No se cambió nada.")
        sys.exit(1)
    config["web_password_hash"] = generate_password_hash(password)
    guardar_config()
    print("Contraseña actualizada.")


if __name__ == "__main__":
    if "--set-password" in sys.argv:
        cambiar_password()
        sys.exit(0)

    cargar_config()  # también crea/migra config.json si hace falta
    # Puerto configurable: PORT=8000 python app.py
    # (en macOS el 5000 suele estar ocupado por AirPlay Receiver)
    puerto = int(os.environ.get("PORT", 5000))
    try:
        # Servidor de producción (multihilo, sin el warning del dev server)
        from waitress import serve
        print(f"Sirviendo con waitress en http://0.0.0.0:{puerto}")
        serve(app, host="0.0.0.0", port=puerto, threads=8)
    except ImportError:
        print("waitress no está instalado; usando el servidor de desarrollo de Flask")
        app.run(host="0.0.0.0", port=puerto, debug=False)
