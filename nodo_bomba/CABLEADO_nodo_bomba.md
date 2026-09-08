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

## 4) Relé → contactor (esto va DESPUÉS de probar en banco)

**Diseño actual (código 2026-07-20): COM + NO sobre la bobina A1–A2 del contactor.**

- El relé abre/cierra el circuito de la **bobina A1–A2** del contactor (COM + **NO**):
  - Relé en reposo → COM–NO abierto → bobina sin energía → **bomba APAGADA** ✅ (fail-safe)
  - Arduino activa el relé → COM–NO cerrado → bobina energizada → **bomba ENCENDIDA** ✅
- Cableado del circuito de mando: fase → botón ROJO (paro) → **COM** del relé →
  **NO** del relé → **A1** de la bobina; **A2** → neutro (como estaba).
- El botón rojo del Olczak sigue funcionando para cortar a mano.

> Nota de seguridad: con COM+NO, si el Arduino se resetea o se queda sin luz, el relé
> queda en reposo y la bomba APAGADA (nunca rebalsa). Además el código corta la bomba
> si pasan ~2 minutos sin un comando de radio válido.

## Librería necesaria (Arduino IDE)
`Sketch → Include Library → Manage Libraries` → buscar **RF24** (de TMRh20) → Install.
