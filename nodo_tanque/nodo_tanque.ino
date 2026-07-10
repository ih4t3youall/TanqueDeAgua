/*
  ============================================================================
  NODO TANQUE (MAESTRO)  -  ESP32
  Medidor de nivel de tanque de agua con control de bomba por radio
  ----------------------------------------------------------------------------
  Qué hace este nodo:
    1. Mide el nivel del agua con un sensor ultrasónico waterproof JSN-SR04T.
    2. Decide si la bomba debe estar ENCENDIDA o APAGADA (lógica con histéresis).
    3. Envía la orden por radio (NRF24L01) al NODO BOMBA cada segundo.
    4. Se conecta al WiFi y reporta el estado a tu servidor (Flask).
       En la misma llamada recibe la configuración (modo AUTO/MANUAL,
       comando manual y umbrales) que ponés desde la página web.

  Importante: la lógica de control vive ACÁ, no en el servidor. Si el servidor
  o el WiFi se caen, el nodo sigue controlando la bomba en modo AUTO con la
  última configuración conocida. La web es para monitorear y mandar órdenes,
  no es indispensable para que el sistema funcione.

  Librerías necesarias (Gestor de librerías del IDE de Arduino):
    - RF24        (by TMRh20)
    - ArduinoJson (by Benoit Blanchon)
    - Soporte de placas ESP32 (Espressif)
  ============================================================================
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <RF24.h>

// ===========================================================================
// 1) CONFIGURACIÓN QUE TENÉS QUE EDITAR
// ===========================================================================

// --- WiFi ---
const char* WIFI_SSID = "KameHouse";
const char* WIFI_PASS = "bambinull";

// --- Servidor (cambiar por la IP y puerto de tu servidor Flask) ---
const char* SERVER_URL = "http://187.127.22.210:5000/api/status";

// --- Token de autenticación con el servidor (debe coincidir con
//     "device_token" en servidor/config.json) ---
const char* API_TOKEN = "kame-tank-7f3a9c2e51d84b06";

// --- Geometría del tanque (calibración) ---
// El sensor mide la DISTANCIA desde la tapa hasta la superficie del agua.
// Tanque lleno  -> distancia chica.  Tanque vacío -> distancia grande.
// Medí estos dos valores una vez con un metro y cargalos acá (en cm):
float DIST_TANQUE_LLENO_CM = 15.0;   // distancia medida con el tanque lleno
float DIST_TANQUE_VACIO_CM = 150.0;  // distancia medida con el tanque vacío

// --- Umbrales por defecto (se pueden cambiar después desde la web) ---
// Histéresis: corta cuando llega arriba, vuelve a arrancar cuando baja.
float nivelAltoCorte    = 80.0;  // % de llenado -> CORTAR la bomba (deja de cargar)
float nivelBajoArranque = 30.0;  // % de llenado -> ARRANCAR la bomba (empieza a cargar)

// --- MODO PRUEBA ---
// En 1: IGNORA el sensor y alterna CARGAR (ON) / CORTAR (OFF) cada 5 s,
//       para probar la radio y el relé con un patrón limpio.
// Poné 0 para volver al funcionamiento normal por sensor.
#define MODO_PRUEBA 0
const unsigned long PRUEBA_MS = 5000;  // cada 5 s cambia de estado

// ===========================================================================
// 2) PINES
// ===========================================================================

// Sensor JSN-SR04T (modo trigger/echo, igual que un HC-SR04)
#define PIN_TRIG  26
#define PIN_ECHO  25   // ¡Usar divisor! El ECHO sale a 5V y el ESP32 es 3.3V (vos usaste 100/180)

// Radio NRF24L01 (bus SPI del ESP32: SCK=18, MISO=19, MOSI=23)
#define PIN_CE    4
#define PIN_CSN   5
RF24 radio(PIN_CE, PIN_CSN);

// Dirección del "tubo" de comunicación. Debe ser IDÉNTICA en el nodo bomba.
const byte pipeAddress[6] = "TANK1";

// ===========================================================================
// 3) ESTADO INTERNO
// ===========================================================================

String modo        = "AUTO";   // "AUTO" o "MANUAL"
bool   manualPump  = false;    // en MANUAL: true = encender bomba
bool   desiredPump = false;    // estado deseado actual de la bomba

float  ultimoNivelPct  = 0;
float  ultimaDistanciaCM = 0;

unsigned long tSensor  = 0;
unsigned long tRadio   = 0;
unsigned long tServer  = 0;
unsigned long tPrueba  = 0;      // temporizador del MODO PRUEBA
bool          pruebaEstado = false;

const unsigned long INTERVALO_SENSOR_MS = 1000;  // medir cada 1 s
const unsigned long INTERVALO_RADIO_MS  = 1000;  // mandar orden cada 1 s
const unsigned long INTERVALO_SERVER_MS = 10000; // hablar con el server cada 10 s

// --- Protocolo de radio (debe coincidir EXACTO con el nodo bomba) ---
#define CMD_MAGIC      0x7E   // firma: valida que el paquete es nuestro
#define CMD_BOMBA_ON   0xA5   // código explícito de ENCENDER
#define CMD_BOMBA_OFF  0x5A   // código explícito de APAGAR

// Mensaje que viaja por radio (debe coincidir con el del nodo bomba).
// __attribute__((packed)) fuerza el mismo tamaño (6 bytes) en ESP32 y en el Nano,
// si no, el ESP32 le agrega "relleno" y la comunicación falla.
struct __attribute__((packed)) RadioMsg {
  uint8_t  magic;     // CMD_MAGIC
  uint8_t  command;   // CMD_BOMBA_ON o CMD_BOMBA_OFF
  uint32_t seq;       // número de secuencia (para diagnóstico)
};
RadioMsg msg = {CMD_MAGIC, CMD_BOMBA_OFF, 0};

// ===========================================================================
// 4) SETUP
// ===========================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  // --- Radio ---
  if (!radio.begin()) {
    Serial.println("ERROR: no se detecta el modulo NRF24. Revisar cableado.");
  }
  radio.setPALevel(RF24_PA_LOW);      // LOW = más estable en banco. Subir a HIGH solo con fuente sólida + capacitor.
  radio.setDataRate(RF24_250KBPS);    // menor velocidad = más alcance/robustez
  radio.setChannel(108);              // canal poco usado por WiFi
  radio.setRetries(5, 15);
  radio.openWritingPipe(pipeAddress); // este nodo TRANSMITE
  radio.stopListening();

  // --- WiFi ---
  conectarWiFi();

  Serial.println("Nodo TANQUE iniciado.");
}

// ===========================================================================
// 5) LOOP PRINCIPAL
// ===========================================================================
void loop() {
  unsigned long ahora = millis();

  // --- a) Medir el nivel ---
  if (ahora - tSensor >= INTERVALO_SENSOR_MS) {
    tSensor = ahora;
    ultimaDistanciaCM = medirDistanciaCM();
    if (ultimaDistanciaCM > 0) {
      ultimoNivelPct = distanciaANivelPct(ultimaDistanciaCM);
    }
    decidirBomba();
    Serial.printf("Dist: %.1f cm | Nivel: %.0f%% | Modo: %s | Bomba: %s\n",
                  ultimaDistanciaCM, ultimoNivelPct, modo.c_str(),
                  desiredPump ? "ON" : "OFF");
  }

  // --- MODO PRUEBA: alterna CARGAR/CORTAR cada 5 s, ignorando el sensor ---
#if MODO_PRUEBA
  if (ahora - tPrueba >= PRUEBA_MS) {
    tPrueba = ahora;
    pruebaEstado = !pruebaEstado;
    desiredPump = pruebaEstado;
    Serial.printf("MODO PRUEBA -> %s\n", desiredPump ? "CARGAR (ON)" : "CORTAR (OFF)");
  }
#endif

  // --- b) Enviar la orden por radio ---
  if (ahora - tRadio >= INTERVALO_RADIO_MS) {
    tRadio = ahora;
    enviarOrdenRadio();
  }

  // --- c) Hablar con el servidor ---
  if (ahora - tServer >= INTERVALO_SERVER_MS) {
    tServer = ahora;
    comunicarServidor();
  }
}

// ===========================================================================
// 6) MEDICIÓN DEL SENSOR (con filtrado por mediana, para ignorar salpicaduras)
// ===========================================================================
float medirDistanciaCM() {
  const int N = 7;
  float lecturas[N];
  int validas = 0;

  for (int i = 0; i < N; i++) {
    digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(3);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    long dur = pulseIn(PIN_ECHO, HIGH, 30000UL); // timeout 30 ms (~5 m)
    if (dur > 0) {
      lecturas[validas++] = (dur * 0.0343) / 2.0; // cm
    }
    delay(40);
  }

  if (validas == 0) return -1; // sin lectura válida

  // Ordenar (burbuja, son pocas lecturas) y devolver la mediana
  for (int i = 0; i < validas - 1; i++) {
    for (int j = 0; j < validas - 1 - i; j++) {
      if (lecturas[j] > lecturas[j + 1]) {
        float t = lecturas[j]; lecturas[j] = lecturas[j + 1]; lecturas[j + 1] = t;
      }
    }
  }
  return lecturas[validas / 2];
}

// Convierte la distancia medida en porcentaje de llenado (0–100 %)
float distanciaANivelPct(float dist) {
  float pct = (DIST_TANQUE_VACIO_CM - dist) /
              (DIST_TANQUE_VACIO_CM - DIST_TANQUE_LLENO_CM) * 100.0;
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

// ===========================================================================
// 7) LÓGICA DE DECISIÓN (histéresis)
// ===========================================================================
void decidirBomba() {
#if MODO_PRUEBA
  return;  // en MODO PRUEBA el estado lo maneja el toggle de 5 s (ignora el sensor)
#endif
  if (modo == "MANUAL") {
    desiredPump = manualPump;            // la web manda directo
    return;
  }
  // Modo AUTO con histéresis:
  if (ultimoNivelPct >= nivelAltoCorte) {
    desiredPump = false;                 // lleno -> cortar
  } else if (ultimoNivelPct <= nivelBajoArranque) {
    desiredPump = true;                  // bajo -> arrancar
  }
  // Entre los dos umbrales: mantiene el estado anterior (evita traqueteo).
}

// ===========================================================================
// 8) ENVÍO POR RADIO
// ===========================================================================
void enviarOrdenRadio() {
  msg.magic   = CMD_MAGIC;
  msg.command = desiredPump ? CMD_BOMBA_ON : CMD_BOMBA_OFF;
  msg.seq++;
  bool ok = radio.write(&msg, sizeof(msg));
  if (!ok) {
    Serial.println("Aviso: el nodo bomba no confirmo la recepcion (reintenta).");
  }
}

// ===========================================================================
// 9) COMUNICACIÓN CON EL SERVIDOR
// ===========================================================================
void comunicarServidor() {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Token", API_TOKEN);
  http.setTimeout(4000);

  // Armar el JSON con el estado actual
  StaticJsonDocument<256> body;
  body["level_pct"]   = round(ultimoNivelPct);
  body["distance_cm"] = round(ultimaDistanciaCM);
  body["pump_on"]     = desiredPump;
  body["modo"]        = modo;
  body["rssi"]        = WiFi.RSSI();

  String payload;
  serializeJson(body, payload);

  int code = http.POST(payload);
  if (code == 200) {
    String resp = http.getString();
    aplicarConfigDelServidor(resp);
  } else {
    Serial.printf("Server no responde (HTTP %d). Sigo en modo local.\n", code);
  }
  http.end();
}

// El servidor responde con la configuración actual (modo, comando manual, umbrales)
void aplicarConfigDelServidor(const String& json) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, json)) return; // si no parsea, mantengo lo que tengo

  if (doc.containsKey("modo"))                modo             = String((const char*)doc["modo"]);
  if (doc.containsKey("manual_pump"))         manualPump       = doc["manual_pump"];
  if (doc.containsKey("nivel_alto_corte"))    nivelAltoCorte   = doc["nivel_alto_corte"];
  if (doc.containsKey("nivel_bajo_arranque")) nivelBajoArranque= doc["nivel_bajo_arranque"];
}

// ===========================================================================
// 10) WIFI
// ===========================================================================
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("Conectando a WiFi '%s' ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("No se pudo conectar al WiFi. Sigo controlando en modo local.");
  }
}
