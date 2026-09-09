# Guía de usuario — Puesta en marcha

Seguí estos pasos en orden. Cada parte se puede probar por separado antes de
juntar todo.

## 1. Preparar el IDE de Arduino

1. Instalá el **IDE de Arduino** (versión 2.x).
2. Agregá el soporte de **ESP32**: en `Archivo → Preferencias`, en "Gestor de URLs
   adicionales de tarjetas" pegá:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   Después en `Herramientas → Placa → Gestor de tarjetas` buscá **esp32** e instalá.
3. Instalá las librerías (`Herramientas → Gestionar librerías`):
   - **RF24** (by TMRh20)
   - **ArduinoJson** (by Benoit Blanchon)

## 2. Conexiones del NODO TANQUE (ESP32)

> El nodo tanque **ya no lleva sensor de nivel**. Solo tiene el ESP32, la radio
> y el regulador. Diagrama con gráficos: `diagramas/conexiones_nodo_tanque.html`.
> Detalle en `nodo_tanque/CABLEADO_nodo_tanque.md`.

### Radio NRF24L01 (alimentada por el AMS1117 5V→3.3V)
| NRF24 | ESP32 |
|---|---|
| VCC | **OUT del AMS1117** (3.3V). Nunca al pin 3V3 del ESP32 |
| GND | GND común |
| CE | GPIO 4 |
| CSN | GPIO 5 |
| SCK | GPIO 18 |
| MOSI | GPIO 22 |
| MISO | GPIO 19 |

### AMS1117
| AMS1117 | Va a |
|---|---|
| IN | VIN (5V) del ESP32 |
| GND | GND común |
| OUT | VCC del NRF24 |

## 3. Conexiones del NODO BOMBA (Arduino Nano)

### Radio NRF24L01 (usá la base con regulador)
| NRF24 | Nano |
|---|---|
| VCC | 3.3V (o 5V con la base) |
| GND | GND |
| CE | D9 |
| CSN | D10 |
| SCK | D13 |
| MOSI | D11 |
| MISO | D12 |

### Módulo de relés (2 canales)
| Relé | Nano |
|---|---|
| VCC | 5V |
| GND | GND |
| IN1 (START) | D2 |
| IN2 (STOP) | D3 |

### Cómo conectar los relés a tu botonera

> 🔌 **Seguridad primero:** desconectá la alimentación antes de tocar el tablero.
> Si no estás seguro, que lo revise un electricista. Vamos a tocar **solo el
> circuito de mando** (los botones), no la potencia de la bomba.

- **Relé 1 (START)** → en **paralelo** con el **botón VERDE**. Usá los contactos
  **COM** y **NO** (normal abierto) del relé, conectados a los mismos dos cables
  que llegan al botón verde. Cuando el relé cierra, es como apretar el verde.

- **Relé 2 (STOP)** → en **serie** con el **botón ROJO**, usando los contactos
  **COM** y **NC** (normal cerrado). En reposo el circuito está cerrado y la bomba
  puede andar; cuando el relé se activa, abre el circuito un instante = como apretar
  el rojo y la bomba para.

Tus botones físicos siguen funcionando: podés operar la bomba a mano siempre.

## 4. Cargar el código

1. **Nodo bomba:** abrí `nodo_bomba/nodo_bomba.ino`, elegí placa "Arduino Nano",
   el puerto, y cargá.
   - Si tu módulo de relés enciende al revés (se activan en reposo), cambiá en el
     código `RELAY_ON` / `RELAY_OFF`.
2. **Nodo tanque:** abrí `nodo_tanque/nodo_tanque.ino` y **antes de cargar editá**:
   - `WIFI_SSID` y `WIFI_PASS` con tu red.
   - `SERVER_URL` con la IP y puerto de tu servidor (ej. `http://192.168.0.100:5000/api/status`).
   - Elegí placa "ESP32 Dev Module" y cargá.

## 5. Qué hace el nodo tanque sin sensor

- **MANUAL**: la bomba obedece el botón de la web (encender / apagar).
- **AUTO**: como no hay sensor para saber cuándo está lleno, la bomba queda
  **apagada** por seguridad. Para usar la bomba, pasá a MANUAL.
- Los umbrales de la web se guardan pero no se usan.

## 6. Levantar el servidor

En tu servidor/PC con Python 3.9+:

```bash
cd servidor
pip install -r requirements.txt
python app.py
```

Vas a ver `Running on http://0.0.0.0:5000`. Desde cualquier dispositivo de la red,
entrá a `http://IP-DE-TU-SERVIDOR:5000`.

> Para que quede corriendo siempre (aunque cierres la sesión), podés usar `systemd`,
> `pm2`, `screen`/`tmux`, o correrlo dentro de Docker. Para uso real conviene poner
> Flask detrás de `gunicorn` o `waitress`, pero para empezar `python app.py` alcanza.

## 7. Usar el sistema

En la web vas a ver:

- **Nivel actual** del tanque: hoy queda en 0 % porque el nodo ya no tiene sensor.
- **Estado de la bomba**: la etiqueta ENCENDIDA / APAGADA cambia **solo cuando
  el nodo bomba acusó recibo** del comando. Mientras espera, avisa "esperando
  acuse para ENCENDER/APAGAR". Si nunca hubo acuse dice "SIN CONFIRMAR", y si
  pasan más de 2 min sin radio se muestra APAGADA por el fail-safe del nodo bomba.
- Si el tanque está **en línea**.
- **Enlace con la bomba**: si el nodo bomba acusa recibo de cada comando por
  radio, y el estado del último comando (enviado / en tránsito / entregado).
- **Bomba**: botones Encender y Apagar. Queda como la dejes.
- **Encender por tiempo**: ponés los minutos (5 por defecto) y Encender. La
  bomba arranca y se apaga sola cuando pasa el tiempo; la web muestra la
  cuenta regresiva. Apagar a mano lo cancela.

El modo automático y los umbrales no aparecen en la web mientras no haya sensor.

## Solución de problemas

| Síntoma | Posible causa / solución |
|---|---|
| El NRF24 no transmite / "no se detecta" | Alimentación inestable: usá la base con regulador 3.3V o poné un capacitor 10–100 µF entre VCC y GND. Revisá cableado SPI. |
| La bomba no arranca con el relé | Verificá si los relés son activos en bajo (ajustá `RELAY_ON`/`RELAY_OFF`). Confirmá paralelo en verde / serie en rojo. |
| La bomba no enciende desde la web | El nodo tiene que estar en MANUAL; en AUTO queda apagada (no hay sensor). Esperá hasta 10 s, que es cada cuánto consulta al servidor. |
| La web dice "Sin conexión con el tanque" | El ESP32 perdió WiFi o no llega al servidor. Revisá `WIFI_SSID/PASS` y `SERVER_URL`, y que el servidor esté corriendo. |
| La bomba se apaga sola seguido | El nodo bomba no recibe la radio (fail-safe). Mejorá la antena/posición o bajá la distancia. |
| Alcance de radio insuficiente | Usá módulos PA/LNA, antena en el techo fuera del housing, y `RF24_250KBPS` (ya configurado). Última opción: cambiar a LoRa. |
