# Maestro — cableado (ESP32 + nRF24L01 + AMS1117)

Diagrama con gráficos: `diagramas/conexiones_nodo_tanque.html` (abrilo en el navegador).

Este nodo **ya no mide el nivel del tanque**: se sacó el sensor JSN-SR04T y su
divisor de tensión. Lo que queda es el ESP32, la radio y el regulador.

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

## 3) Pines libres

GPIO 25 y GPIO 26 quedaron libres (antes eran ECHO y TRIG del sensor).

## 4) Parámetros de radio (deben coincidir con el nodo bomba)

Dirección `TANK1`, canal 108, 250 kbps, `RF24_PA_LOW` para pruebas en banco.

## Librerías necesarias (Arduino IDE)

RF24 (TMRh20), ArduinoJson (Benoit Blanchon) y el soporte de placas ESP32.
