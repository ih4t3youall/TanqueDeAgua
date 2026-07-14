#!/usr/bin/env bash
# ============================================================================
#  Levanta el servidor del medidor de tanque.
#  - Verifica que Python esté instalado (python3 en Mac, python en Linux)
#    y si falta LO INSTALA (apt en Linux, brew en Mac).
#  - Crea el venv si no existe (instala python3-venv si hace falta,
#    y recrea el venv si quedó roto/incompleto).
#  - Instala/actualiza las dependencias solo si hace falta.
#  - Arranca el server (waitress).
#
#  Uso:
#     ./run.sh              -> puerto 5000 (o el que ya tengas en PORT)
#     PORT=8090 ./run.sh    -> otro puerto
#     PYTHON=python3.12 ./run.sh  -> forzar un intérprete puntual
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")"

VENV="venv"
OS="$(uname -s)"

# Corre un comando con sudo solo si no somos root (en hostings a veces ya sos root)
como_root() {
    if [ "$(id -u)" = "0" ]; then "$@"; else sudo "$@"; fi
}

# ---------------------------------------------------------------------------
# 0) Verificar que Python esté instalado; si falta, instalarlo.
#    - macOS: el comando es "python3" (se instala con brew)
#    - Linux: el comando es "python" (se instala con apt; en muchos Linux
#      el comando termina siendo "python3", así que se aceptan los dos)
# ---------------------------------------------------------------------------
buscar_python() {
    case "$OS" in
        Linux) CANDIDATOS="python python3" ;;
        *)     CANDIDATOS="python3 python" ;;
    esac
    for c in $CANDIDATOS; do
        if command -v "$c" >/dev/null 2>&1; then
            echo "$c"
            return 0
        fi
    done
    return 1
}

if [ -z "${PYTHON:-}" ]; then
    if ! PYTHON="$(buscar_python)"; then
        echo ">> Python no está instalado. Lo instalo ahora ..."
        case "$OS" in
            Darwin)
                if ! command -v brew >/dev/null 2>&1; then
                    echo "ERROR: falta Homebrew (https://brew.sh). Instalalo y volvé a correr ./run.sh" >&2
                    exit 1
                fi
                brew install python3
                ;;
            Linux)
                if command -v apt-get >/dev/null 2>&1; then
                    como_root apt-get update -qq
                    como_root apt-get install -y python3 python3-venv
                else
                    echo "ERROR: no encontré apt. Instalá Python con el gestor de tu distro y reintentá." >&2
                    exit 1
                fi
                ;;
            *)
                echo "ERROR: sistema '$OS' no contemplado. Instalá Python a mano." >&2
                exit 1
                ;;
        esac
        if ! PYTHON="$(buscar_python)"; then
            echo "ERROR: instalé Python pero sigo sin encontrarlo en el PATH." >&2
            exit 1
        fi
    fi
fi
echo ">> Usando $PYTHON ($($PYTHON --version 2>&1))"

# ---------------------------------------------------------------------------
# 1) Crear el venv si no existe o quedó incompleto.
#    Ojo: en Ubuntu, si falta el paquete python3-venv, "python -m venv" falla
#    A MEDIAS y deja un venv sin pip ni activate. Por eso validamos que el
#    pip del venv realmente funcione; si no, instalamos lo que falte,
#    borramos el venv roto y lo recreamos.
# ---------------------------------------------------------------------------
crear_venv() {
    rm -rf "$VENV"
    "$PYTHON" -m venv "$VENV" >/dev/null 2>&1 && "$VENV/bin/pip" --version >/dev/null 2>&1
}

if ! "$VENV/bin/pip" --version >/dev/null 2>&1; then
    if [ -d "$VENV" ]; then
        echo ">> El venv existente está roto/incompleto; lo recreo ..."
    else
        echo ">> Creando ambiente virtual en $VENV/ ..."
    fi
    if ! crear_venv; then
        # En Debian/Ubuntu casi siempre es porque falta pythonX.Y-venv
        if [ "$OS" = "Linux" ] && command -v apt-get >/dev/null 2>&1; then
            VER="$($PYTHON -c 'import sys; print("%d.%d" % sys.version_info[:2])' 2>/dev/null || true)"
            echo ">> Falta el paquete venv de Python. Instalo python${VER:-3}-venv ..."
            como_root apt-get install -y "python${VER:-3}-venv"
            if ! crear_venv; then
                rm -rf "$VENV"
                echo "ERROR: instalé python${VER:-3}-venv pero el venv sigue sin poder crearse." >&2
                exit 1
            fi
        else
            rm -rf "$VENV"
            echo "ERROR: no se pudo crear el ambiente virtual." >&2
            exit 1
        fi
    fi
fi

# ---------------------------------------------------------------------------
# 2) Instalar dependencias solo si requirements.txt cambió desde la última vez
# ---------------------------------------------------------------------------
STAMP="$VENV/.requirements.instaladas"
if [ ! -f "$STAMP" ] || [ requirements.txt -nt "$STAMP" ]; then
    echo ">> Instalando dependencias ..."
    "$VENV/bin/pip" install --quiet --upgrade pip
    "$VENV/bin/pip" install --quiet -r requirements.txt
    touch "$STAMP"
else
    echo ">> Dependencias al día."
fi

# ---------------------------------------------------------------------------
# 3) Levantar el servidor (usando el python del venv, sin necesidad de activate)
# ---------------------------------------------------------------------------
echo ">> Iniciando servidor (puerto ${PORT:-5000}) ..."
exec "$VENV/bin/python" app.py
