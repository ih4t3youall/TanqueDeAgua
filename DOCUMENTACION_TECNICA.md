# Documentación técnica — Cómo funciona el sistema

## Idea general

Dos nodos electrónicos comunicados por radio, más un servidor web para
monitorear y controlar desde el celular o la PC.

```
   ┌─────────────────────────┐         radio NRF24         ┌────────────────────────┐
   │   NODO TANQUE (maestro)  │  ───────────────────────▶   │  NODO BOMBA (esclavo)  │
   │   ESP32                  │     "encender / apagar"     │  Arduino Nano          │
   │   • recibe órdenes web   │                             │  • 2 relés             │
   │   • decide qué hacer     │                             │  • acciona la botonera │
   │   • WiFi al servidor     │                             │  • fail-safe           │
   └───────────┬─────────────┘                             └────────────────────────┘
               │ WiFi (HTTP)
               ▼
   ┌─────────────────────────┐
   │  SERVIDOR (Python/Flask) │  ◀──── navegador (celular/PC): ver nivel y controlar
   └─────────────────────────┘
```

El reparto de roles es a propósito: **toda la inteligencia está en el nodo del
tanque**. El nodo de la bomba no piensa, solo obedece. Y el servidor es opcional
para que el sistema funcione: si se cae el WiFi o el servidor, el tanque sigue
controlando la bomba solo.

## Nodo TANQUE (ESP32) — el cerebro

> Desde 2026-09 el nodo tanque **no mide el nivel**: se quitó el sensor
> ultrasónico JSN-SR04T. Las órdenes vienen de la web.

Cada segundo:

1. **Decide** si la bomba debe estar encendida o apagada:
   - **MANUAL**: obedece el comando que pusiste en la web.
   - **AUTO**: sin sensor no puede saber cuándo está lleno, así que deja la
     bomba **apagada** por seguridad. Los umbrales que manda el servidor se
     guardan pero no se usan.

2. **Envía la orden por radio** al nodo bomba. La reenvía cada segundo, así que
   el nodo bomba siempre tiene una orden fresca.

3. **Habla con el servidor** por WiFi (cada 10 s): le manda el estado (bomba,
   modo, señal WiFi) y recibe de vuelta la configuración que pusiste en la web
   (modo AUTO/MANUAL y comando manual).

### Modo AUTO vs MANUAL

- **MANUAL**: vos mandás encender/apagar desde la web y el ESP32 obedece. Es el
  modo de uso actual.
- **AUTO**: reservado para cuando vuelva a haber un sensor de nivel; hoy deja la
  bomba apagada.

## Nodo BOMBA (Arduino Nano) — el músculo

Escucha la radio. Cuando llega la orden:

- **Encender**: da un pulso de ~0,6 s al relé conectado en **paralelo con el botón
  verde** → es como apretar "marcha". El contactor engancha y la bomba arranca.
- **Apagar**: da un pulso al relé conectado en **serie con el circuito del botón
  rojo** (contacto normal cerrado que se abre un instante) → es como apretar
  "paro". El contactor se suelta y la bomba para.

Nunca tocamos la corriente de potencia de la bomba: solo el circuito de mando del
contactor, que maneja muy poca corriente. Por eso es seguro y barato.

### Fail-safe (a prueba de fallas)

El nodo bomba espera recibir un mensaje del tanque al menos cada pocos segundos.
Si pasan **5 segundos sin señal** (se quedó sin batería el tanque, falló la radio,
una pared de más, etc.), **apaga la bomba por seguridad**. Así, ante cualquier
falla de comunicación, el peor caso es que la bomba quede apagada — nunca que el
tanque se desborde.

Además, al encender el nodo bomba, lo primero que hace es mandar un pulso de "paro"
para arrancar siempre en estado seguro (bomba apagada).

## Servidor (Python + Flask)

Es un programa chico que corre en tu servidor. Hace tres cosas:

- Recibe los reportes del ESP32 (`POST /api/status`) y le devuelve la configuración.
- Sirve la página web de control.
- Guarda la configuración (modo y umbrales) en `config.json` para que sobreviva
  a un reinicio.

La **página web** muestra el nivel con un dibujo del tanque, el estado de la bomba,
si el tanque está en línea y con cuánta señal, y permite cambiar de modo, encender/
apagar a mano (en MANUAL) y ajustar los umbrales. Se actualiza sola cada 2 segundos.

## Comunicación: detalle de los mensajes

**Radio (NRF24L01)** — estructura de 5 bytes que viaja del tanque a la bomba:

```c
struct RadioMsg { uint8_t command;  uint32_t seq; };
//                command: 1=ON, 0=OFF        seq: contador (diagnóstico)
```

**HTTP (ESP32 → servidor)** — el ESP32 hace `POST /api/status` con:

```json
{ "pump_on": true, "modo": "MANUAL", "rssi": -67,
  "radio_ok": true, "radio_seq": 37, "radio_ack_ok": true,
  "radio_ack_seq": 37, "radio_ack_hace_s": 0, "radio_ult10": 9 }
```

(`level_pct` y `distance_cm` ya no se mandan; el servidor los deja en 0.)

Los campos `radio_*` describen el enlace tanque → bomba y alimentan la tarjeta
"Enlace con la bomba" de la web:

- `radio_ok`: el NRF24 del tanque responde por SPI.
- `radio_seq`: número del último comando enviado por radio. Va de **1 a 100 y
  vuelve a 1**, para que no crezca sin fin.
- `radio_ack_ok`: si ese último envío tuvo **acuse de recibo** de la radio de la
  bomba (lo da el chip NRF24 automáticamente: significa que el paquete llegó).
- `radio_ack_seq` y `radio_ack_hace_s`: último comando confirmado y hace cuánto.
- `radio_ult10`: cuántos de los últimos 10 envíos se entregaron (calidad del enlace).

Con eso la web muestra el ciclo completo de un comando: **enviado al servidor →
esperando que el nodo tanque lo tome (hasta 10 s) → en tránsito por radio →
entregado a la bomba con acuse #N**. Cuando el nodo tanque recibe una orden
nueva, vuelve a reportar a los ~1,5 s en vez de esperar 10 s, así el estado se
actualiza rápido.

y el servidor responde con la configuración:

```json
{ "modo": "MANUAL", "manual_pump": true,
  "manual_pump_hasta": 1788985533.4, "restante_s": 277, "temporizador_min": 5,
  "nivel_alto_corte": 80.0, "nivel_bajo_arranque": 30.0 }
```

**Encender por N minutos:** desde la web se puede encender la bomba con un
temporizador (5 min por defecto). El servidor guarda hasta cuándo tiene que
quedar encendida (`manual_pump_hasta`) y, cuando vence, pone `manual_pump` en
`false`. El nodo se entera en su próxima consulta, así que la bomba se apaga
hasta 10 s después del vencimiento. El nodo no lleva el temporizador por su
cuenta: si el servidor se cae con la bomba encendida por tiempo, el nodo
mantiene la última orden (encendida) hasta que el servidor vuelva.

## Decisiones de diseño y por qué

- **ESP32 en el tanque (no Nano):** porque necesitábamos WiFi, y el Nano no tiene.
  El ESP32 además sobra de potencia para sensor + radio + WiFi.
- **Radio entre nodos (no todo por WiFi):** así el control de la bomba no depende
  de que el WiFi/servidor estén funcionando. Es más robusto.
- **Lógica en el tanque (no en el servidor):** mismo motivo. El servidor es para
  ver y mandar órdenes, no es crítico para el funcionamiento.
- **NRF24 PA/LNA + antena en el techo:** para cubrir los ~20 m con pared de por medio.

## Posibles mejoras a futuro

- **Volver a poner un sensor de nivel** para que el modo AUTO funcione solo
  (histéresis: corta arriba, arranca abajo).
- **Realimentación real de la bomba:** hoy el nodo bomba *asume* el estado (lo que
  ordenó). Se podría leer un contacto auxiliar del contactor o un sensor de corriente
  para saber si la bomba realmente está girando.
- **Protección contra marcha en seco** de la bomba sumergible (sensor de nivel mínimo
  en el pozo/cisterna de origen).
- **Avisos** (Telegram/mail) cuando el tanque está lleno, vacío o se pierde la señal.
- **Histórico** de niveles en el servidor para ver el consumo por día.
