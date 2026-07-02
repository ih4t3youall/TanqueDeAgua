# Esclavo — cableado (Arduino Nano + nRF24L01 + relé)

## 1) nRF24L01 → Nano (es SPI, respetá los pines exactos)

El módulo tiene 8 pines en 2 filas. Mirándolo con la antena hacia arriba:

| Pin nRF24 | Va a... | Notas |
|-----------|---------|-------|
| GND       | GND (común) | |
| VCC       | **3.3V del AMS1117** | NO al pin 3.3V del Nano |
| CE        | Nano D9   | |
| CSN       | Nano D10  | |
| SCK       | Nano D13  | |
| MOSI      | Nano D11  | |
| MISO      | Nano D12  | |
| IRQ       | — (libre) | no se usa |

### Alimentación del nRF24 con el AMS1117 (tu caso)
Tus nRF24 son **+PA+LNA con antena externa**: chupan picos de corriente que el
regulador 3.3V del Nano no aguanta. Por eso usás el **módulo AMS1117 5V→3.3V**,
que da hasta 800mA y ya trae capacitores → **no hace falta agregar capacitor**.

Conexión del AMS1117:
- **IN (entrada)** ← 5V del Nano
- **GND** ← GND común (mismo GND que el Nano)
- **OUT (salida 3.3V)** → VCC del nRF24

⚠️ No conectes el VCC del nRF24 al pin 3.3V del Nano: usá SOLO la salida del AMS1117.

## 2) Módulo relé → Nano

| Pin relé | Va a Nano |
|----------|-----------|
| VCC      | 5V        |
| GND      | GND       |
| IN       | D3        |

## 3) LED de estado (opcional)
- D4 → resistencia 220Ω → pata larga del LED → pata corta → GND
- LED encendido = bomba ON. Apagado = bomba cortada.

## 4) Relé → línea de la bomba (esto va DESPUÉS de probar en banco)
- La bomba queda siempre en ON.
- Esa línea "ON" pasa por el relé en **COM** y **NC** (normal cerrado):
  - Relé en reposo → NC cerrado → bomba ON ✅
  - Arduino activa el relé (tanque lleno) → abre → bomba se corta ✅
- El botón rojo del contactor sigue funcionando para apagar a mano.

> Nota de seguridad: con NC, si el Arduino se queda sin luz la bomba sigue ON.
> El código tiene un *failsafe* que corta la bomba si pierde la radio 60s.

## Librería necesaria (Arduino IDE)
`Sketch → Include Library → Manage Libraries` → buscar **RF24** (de TMRh20) → Install.
