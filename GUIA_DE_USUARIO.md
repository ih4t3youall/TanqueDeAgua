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

### Sensor JSN-SR04T
| Sensor | ESP32 |
|---|---|
| VCC | 5V (VIN) |
| GND | GND |
| TRIG | GPIO 26 |
| ECHO | GPIO 25 **a través del divisor** |

> ⚠️ El pin ECHO sale a 5V y el ESP32 trabaja a 3.3V. Poné un **divisor de tensión**:
> del ECHO una resistencia de **1 kΩ** al GPIO 25, y de ese mismo punto una de
> **2 kΩ** a GND. Así el ESP32 recibe ~3.3V y no se daña.

### Radio NRF24L01 (usá la base con regulador 3.3V)
| NRF24 | ESP32 |
|---|---|
| VCC | 3.3V (o 5V si usás la base con regulador) |
| GND | GND |
| CE | GPIO 4 |
| CSN | GPIO 5 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |

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

## 5. Calibrar el sensor (importante)

1. Con el **tanque vacío**, mirá el Monitor Serie (115200 baudios) del ESP32 y
   anotá la "Dist" en cm. Cargá ese valor en `DIST_TANQUE_VACIO_CM`.
2. Con el **tanque lleno**, anotá la "Dist" y cargála en `DIST_TANQUE_LLENO_CM`.
3. Volvé a cargar el código. Ahora el porcentaje va a ser correcto.

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

- **Nivel actual** del tanque (dibujo + porcentaje + distancia).
- **Estado de la bomba** (encendida/apagada) y si el tanque está **en línea**.
- **Modo:**
  - **AUTO** — el sistema solo: corta al llegar arriba, arranca al bajar.
  - **MANUAL** — vos encendés/apagás con los botones de la web.
- **Umbrales** — el % en que corta y el % en que vuelve a arrancar (modo AUTO).
  Cambialos y tocá "Guardar umbrales".

## Solución de problemas

| Síntoma | Posible causa / solución |
|---|---|
| El NRF24 no transmite / "no se detecta" | Alimentación inestable: usá la base con regulador 3.3V o poné un capacitor 10–100 µF entre VCC y GND. Revisá cableado SPI. |
| La bomba no arranca con el relé | Verificá si los relés son activos en bajo (ajustá `RELAY_ON`/`RELAY_OFF`). Confirmá paralelo en verde / serie en rojo. |
| El porcentaje está mal | Recalibrá `DIST_TANQUE_LLENO_CM` y `DIST_TANQUE_VACIO_CM`. |
| Lecturas saltan mucho | Confirmá el sensor bien fijo en la tapa, apuntando recto al agua, sin obstáculos. |
| La web dice "Sin conexión con el tanque" | El ESP32 perdió WiFi o no llega al servidor. Revisá `WIFI_SSID/PASS` y `SERVER_URL`, y que el servidor esté corriendo. |
| La bomba se apaga sola seguido | El nodo bomba no recibe la radio (fail-safe). Mejorá la antena/posición o bajá la distancia. |
| Alcance de radio insuficiente | Usá módulos PA/LNA, antena en el techo fuera del housing, y `RF24_250KBPS` (ya configurado). Última opción: cambiar a LoRa. |
