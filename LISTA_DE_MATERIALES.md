# Lista de materiales — Medidor de nivel y control de bomba

Sistema de dos nodos comunicados por radio. El nodo del tanque mide el nivel,
decide cuándo arrancar/cortar la bomba, y reporta a un servidor web por WiFi.
El nodo de la bomba es esclavo: solo obedece las órdenes que recibe por radio.

## Nodo TANQUE (maestro)

| Componente | Cant. | Notas |
|---|---|---|
| **ESP32 DevKit v1** (38 pines) | 1 | El "cerebro". Trae WiFi y Bluetooth. Es lo que reemplaza al Arduino común porque el Nano no tiene WiFi. |
| **Sensor ultrasónico waterproof JSN-SR04T** | 1 | Transductor sellado en sonda de metal. Se monta en la tapa del tanque apuntando hacia abajo. |
| **Módulo de radio NRF24L01+ PA/LNA** (con antena de rosca) | 1 | Versión con amplificador y antena externa para alcanzar los ~20 m con pared. |
| **Adaptador/base para NRF24L01 con regulador 3.3V** | 1 | Muy recomendado: estabiliza la alimentación del módulo de radio (es la causa #1 de fallas de NRF24). |
| Resistencias 1 kΩ y 2 kΩ | 1 c/u | Divisor de tensión para el pin ECHO del sensor (baja de 5V a 3.3V para proteger el ESP32). |
| Fuente 5V 1–2A (o cargador USB) | 1 | Alimentación del nodo. |
| Capacitor electrolítico 10–100 µF | 1 | Entre VCC y GND del NRF24 si no usás la base con regulador. |
| Caja / housing impreso 3D | 1 | Con la antena del NRF24 hacia afuera del plástico. |

## Nodo BOMBA (esclavo)

| Componente | Cant. | Notas |
|---|---|---|
| **Arduino Nano** (o Uno) | 1 | Solo recibe la orden por radio y acciona los relés. |
| **Módulo de radio NRF24L01+ PA/LNA** | 1 | Igual que el del tanque, deben ser de la misma versión. |
| **Adaptador/base NRF24L01 con regulador 3.3V** | 1 | Mismo motivo que arriba. |
| **Módulo de relés de 2 canales** | 1 | Un canal emula el botón VERDE (arranque), el otro el ROJO (corte). 5V, optoacoplado. |
| Fuente 5V 1A | 1 | Alimentación del Nano y los relés. |
| Cable para intervenir la botonera | — | Para conectar los relés en paralelo (verde) y en serie (rojo) con tus botones. |
| Caja / housing impreso 3D | 1 | Antena del NRF24 hacia afuera. |

## Servidor (lo ponés vos)

No es hardware: corre en tu servidor/PC con **Python 3**. Necesitás Python 3.9+
y las librerías de `servidor/requirements.txt` (Flask). Ver la guía de usuario.

## Librerías de Arduino (gratis, desde el IDE)

- **RF24** (by TMRh20) — para los módulos NRF24L01.
- **ArduinoJson** (by Benoit Blanchon) — para hablar con el servidor (solo en el ESP32).
- Soporte de placas **ESP32** (gestor de tarjetas de Espressif).

## Notas importantes de seguridad

- **Nunca cortamos la corriente de la bomba directamente.** Los relés solo emulan
  tus botones verde/rojo (circuito de mando del contactor), que maneja muy poca corriente.
- Los relés se conectan **sin quitar tus botones físicos**, así siempre podés operar
  la bomba a mano si el sistema falla.
- El sistema tiene **fail-safe**: si el nodo bomba deja de recibir señal del tanque,
  apaga la bomba sola para evitar que el tanque se desborde.

## Presupuesto aproximado

Es un proyecto económico: ESP32 + Nano + 2 módulos NRF24 + sensor waterproof +
módulo de relés rondan un costo bajo (la mayoría son componentes de pocos dólares
cada uno). Los puntos a no escatimar: la **versión PA/LNA** del NRF24 y la **base
con regulador 3.3V**.
