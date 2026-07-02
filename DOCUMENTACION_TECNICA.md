# Documentación técnica — Cómo funciona el sistema

## Idea general

Dos nodos electrónicos comunicados por radio, más un servidor web para
monitorear y controlar desde el celular o la PC.

```
   ┌─────────────────────────┐         radio NRF24         ┌────────────────────────┐
   │   NODO TANQUE (maestro)  │  ───────────────────────▶   │  NODO BOMBA (esclavo)  │
   │   ESP32                  │     "encender / apagar"     │  Arduino Nano          │
   │   • mide el nivel        │                             │  • 2 relés             │
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

Cada segundo:

1. **Mide el nivel** con el sensor ultrasónico waterproof JSN-SR04T montado en la
   tapa. El sensor da la *distancia* hasta la superficie del agua; el código la
   convierte a *porcentaje de llenado* usando dos valores que calibrás una vez
   (distancia con tanque lleno y con tanque vacío).

   Para que las **salpicaduras** del llenado no generen lecturas falsas, toma 7
   lecturas seguidas y se queda con la **mediana** (el valor del medio), que
   descarta los picos raros.

2. **Decide** si la bomba debe estar encendida o apagada, con **histéresis**:
   - Si el nivel llega al umbral ALTO (ej. 95 %) → ordena **cortar**.
   - Si el nivel baja al umbral BAJO (ej. 60 %) → ordena **arrancar**.
   - Entre los dos umbrales no hace nada (mantiene el estado).

   La histéresis es clave: sin ella, justo en el punto de corte la bomba prendería
   y apagaría muchas veces por segundo ("traqueteo") y se arruinaría.

3. **Envía la orden por radio** al nodo bomba (un mensaje simple: 1 = encender,
   0 = apagar). La reenvía cada segundo, así que el nodo bomba siempre tiene una
   orden fresca.

4. **Habla con el servidor** por WiFi (cada 3 s): le manda el estado (nivel,
   distancia, bomba, señal WiFi) y recibe de vuelta la configuración que pusiste
   en la web (modo AUTO/MANUAL, comando manual, umbrales).

### Modo AUTO vs MANUAL

- **AUTO**: el ESP32 maneja la bomba solo con la histéresis. Es el modo normal.
- **MANUAL**: vos mandás encender/apagar desde la web y el ESP32 obedece, ignorando
  los umbrales. Útil para pruebas o para forzar el llenado.

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
{ "level_pct": 82, "distance_cm": 40, "pump_on": true, "modo": "AUTO", "rssi": -67 }
```

y el servidor responde con la configuración:

```json
{ "modo": "AUTO", "manual_pump": false,
  "nivel_alto_corte": 95.0, "nivel_bajo_arranque": 60.0 }
```

## Decisiones de diseño y por qué

- **ESP32 en el tanque (no Nano):** porque necesitábamos WiFi, y el Nano no tiene.
  El ESP32 además sobra de potencia para sensor + radio + WiFi.
- **Radio entre nodos (no todo por WiFi):** así el control de la bomba no depende
  de que el WiFi/servidor estén funcionando. Es más robusto.
- **Lógica en el tanque (no en el servidor):** mismo motivo. El servidor es para
  ver y mandar órdenes, no es crítico para el funcionamiento.
- **NRF24 PA/LNA + antena en el techo:** para cubrir los ~20 m con pared de por medio.
- **Sensor waterproof + filtro de mediana:** para aguantar la humedad y las
  salpicaduras del llenado por arriba.

## Posibles mejoras a futuro

- **Realimentación real de la bomba:** hoy el nodo bomba *asume* el estado (lo que
  ordenó). Se podría leer un contacto auxiliar del contactor o un sensor de corriente
  para saber si la bomba realmente está girando.
- **Protección contra marcha en seco** de la bomba sumergible (sensor de nivel mínimo
  en el pozo/cisterna de origen).
- **Avisos** (Telegram/mail) cuando el tanque está lleno, vacío o se pierde la señal.
- **Histórico** de niveles en el servidor para ver el consumo por día.
