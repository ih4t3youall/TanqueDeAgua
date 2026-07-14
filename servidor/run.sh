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
PYTHON="${PYTHON:-python3}"

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
