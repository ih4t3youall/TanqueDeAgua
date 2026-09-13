# Maestro — cableado (ESP32 + nRF24L01 + AMS1117 + relé de luces)

Diagrama con gráficos: `diagramas/conexiones_nodo_tanque.html` (abrilo en el navegador).

Este nodo **ya no mide el nivel del tanque**: se sacó el sensor JSN-SR04T y su
divisor de tensión. Lo que queda es el ESP32, la radio, el regulador y un
**relé de un canal para las luces** (se maneja desde la web, a mano o por horario).

## 1) nRF24L01 → ESP32 (SPI por hardware, respetá los pines exactos)

| Pin nRF24 | Va a...              | Notas |
|-----------|----------------------|-------|
| GND       | GND (común)          | |
| VCC       | **3.3V del AMS1117** | NO al pin 3V3 del ESP32 |
| CE        | GPIO 4               | `PIN_CE` en el código |
| CSN       | GPIO 5               | `PIN_CSN` en el código |
| SCK       | GPIO 18              | |
| MOSI      | GPIO 22              | (no el 23 por defecto: se pasa explícito en el código) |
| MISO      | GPIO 19              | |
| IRQ       | — (libre)            | no se usa |

## 2) Alimentación del nRF24 con el AMS1117

El módulo +PA+LNA pide picos de corriente que el regulador 3V3 del ESP32 no
aguanta: se resetea y aparece "no se detecta el modulo NRF24".

- **IN** ← VIN (5V) del ESP32 (los 5V del USB)
- **GND** ← GND común
- **OUT (3.3V)** → VCC del nRF24

Capacitor 10–100 µF entre VCC y GND del nRF24, pegado al módulo (pata larga al
VCC). Si el AMS1117 es el módulo con capacitores incluidos, es opcional.

## 3) Relé de luces → ESP32

| Pin del módulo de relé | Va a...        | Notas |
|------------------------|----------------|-------|
| VCC                    | VIN (5V)       | los 5V del USB; no usar el 3V3 |
| GND                    | GND (común)    | |
| IN                     | **GPIO 25**    | `PIN_RELE_LUCES` en el código |

Contactos del relé: **COM** y **NO** en serie con la fase de las luces (como si
fuera la llave de luz). En reposo (nodo apagado o reiniciando) las luces quedan
apagadas. Si al cargar el código quedan al revés, el módulo es activo en alto:
intercambiá `LUCES_ON` / `LUCES_OFF` en el `.ino`.

> ⚡ El lado de 220 V del relé es tensión de red: cortá la alimentación antes de
> cablear y si tenés dudas que lo revise un electricista. Para cargas grandes
> (muchos tubos, reflectores) conviene que el relé maneje un contactor.

## 4) Pines libres

GPIO 26 quedó libre (el 25 lo usa el relé de luces).

## 5) Parámetros de radio (deben coincidir con el nodo bomba)

Dirección `TANK1`, canal 108, 250 kbps, `RF24_PA_LOW` para pruebas en banco.

## Librerías necesarias (Arduino IDE)

RF24 (TMRh20), ArduinoJson (Benoit Blanchon) y el soporte de placas ESP32.
