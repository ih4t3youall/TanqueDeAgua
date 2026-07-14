#!/usr/bin/env bash
# ============================================================================
#  Levanta el servidor del medidor de tanque.
#  - Crea el venv si no existe
#  - Instala/actualiza las dependencias solo si hace falta
#  - Arranca el server (waitress)
#
#  Uso:
#     ./run.sh              -> puerto 5000 (o el que ya tengas en PORT)
#     PORT=8090 ./run.sh    -> otro puerto
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")"

VENV="venv"

# 0) Verificar que Python esté instalado (según el sistema operativo)
#    - macOS: el comando es "python3"
#    - Linux: el comando es "python"
#    Si el esperado no está, se prueba el otro antes de rendirse.
#    Se puede forzar uno puntual con:  PYTHON=python3.12 ./run.sh
if [ -z "${PYTHON:-}" ]; then
    case "$(uname -s)" in
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
        case "$(uname -s)" in
            Darwin) echo "Instalalo con:  brew install python3" >&2 ;;
            Linux)  echo "Instalalo con:  sudo apt install python3 python3-venv  (Debian/Ubuntu)" >&2 ;;
        esac
        exit 1
    fi
fi
echo ">> Usando $PYTHON ($($PYTHON --version 2>&1))"

# 1) Crear el ambiente virtual si no existe
if [ ! -x "$VENV/bin/python" ]; then
    echo ">> Creando ambiente virtual en $VENV/ ..."
    "$PYTHON" -m venv "$VENV"
fi

# shellcheck disable=SC1091
source "$VENV/bin/activate"

# 2) Instalar dependencias solo si requirements.txt cambió desde la última vez
STAMP="$VENV/.requirements.instaladas"
if [ ! -f "$STAMP" ] || [ requirements.txt -nt "$STAMP" ]; then
    echo ">> Instalando dependencias ..."
    pip install --quiet --upgrade pip
    pip install --quiet -r requirements.txt
    touch "$STAMP"
else
    echo ">> Dependencias al día."
fi

# 3) Levantar el servidor
echo ">> Iniciando servidor (puerto ${PORT:-5000}) ..."
exec python app.py
