#!/usr/bin/env bash
# ============================================================================
#  Levanta el servidor del medidor de tanque.
#  - Verifica que Python esté instalado (python3 en Mac, python en Linux)
#  - Crea el venv si no existe (y lo recrea si quedó roto/incompleto)
#  - Instala/actualiza las dependencias solo si hace falta
#  - Arranca el server (waitress)
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

# 0) Verificar que Python esté instalado (según el sistema operativo)
#    - macOS: el comando es "python3"
#    - Linux: el comando es "python"
#    Si el esperado no está, se prueba el otro antes de rendirse.
if [ -z "${PYTHON:-}" ]; then
    case "$OS" in
        Darwin) PREFERIDO="python3"; ALTERNATIVO="python" ;;
        Linux)  PREFERIDO="python";  ALTERNATIVO="python3" ;;
        *)      PREFERIDO="python3"; ALTERNATIVO="python" ;;
    esac
    if command -v "$PREFERIDO" >/dev/null 2>&1; then
        PYTHON="$PREFERIDO"
    elif command -v "$ALTERNATIVO" >/dev/null 2>&1; then
        PYTHON="$ALTERNATIVO"
    else
        echo "ERROR: no se encontró Python instalado (probé '$PREFERIDO' y '$ALTERNATIVO')." >&2
        case "$OS" in
            Darwin) echo "Instalalo con:  brew install python3" >&2 ;;
            Linux)  echo "Instalalo con:  sudo apt install python3 python3-venv  (Debian/Ubuntu)" >&2 ;;
        esac
        exit 1
    fi
fi
echo ">> Usando $PYTHON ($($PYTHON --version 2>&1))"

# 1) Crear el venv si no existe o quedó incompleto.
#    Ojo: en Ubuntu, si falta el paquete python3-venv, "python -m venv" falla
#    A MEDIAS y deja un venv sin pip ni activate. Por eso validamos que el
#    pip del venv realmente funcione, y si no, lo borramos y recreamos.
if ! "$VENV/bin/pip" --version >/dev/null 2>&1; then
    if [ -d "$VENV" ]; then
        echo ">> El venv existente está roto/incompleto; lo recreo ..."
        rm -rf "$VENV"
    else
        echo ">> Creando ambiente virtual en $VENV/ ..."
    fi
    if ! "$PYTHON" -m venv "$VENV" || ! "$VENV/bin/pip" --version >/dev/null 2>&1; then
        rm -rf "$VENV"
        echo "ERROR: no se pudo crear el ambiente virtual." >&2
        if [ "$OS" = "Linux" ]; then
            VER="$($PYTHON -c 'import sys; print("%d.%d" % sys.version_info[:2])' 2>/dev/null || true)"
            echo "En Debian/Ubuntu suele faltar el paquete venv. Probá:" >&2
            echo "    sudo apt install python${VER:-3}-venv" >&2
            echo "y volvé a correr ./run.sh" >&2
        fi
        exit 1
    fi
fi

# 2) Instalar dependencias solo si requirements.txt cambió desde la última vez
STAMP="$VENV/.requirements.instaladas"
if [ ! -f "$STAMP" ] || [ requirements.txt -nt "$STAMP" ]; then
    echo ">> Instalando dependencias ..."
    "$VENV/bin/pip" install --quiet --upgrade pip
    "$VENV/bin/pip" install --quiet -r requirements.txt
    touch "$STAMP"
else
    echo ">> Dependencias al día."
fi

# 3) Levantar el servidor (usando el python del venv, sin necesidad de activate)
echo ">> Iniciando servidor (puerto ${PORT:-5000}) ..."
exec "$VENV/bin/python" app.py
