/*
  ============================================================================
  NODO TANQUE (MAESTRO)  -  ESP32
  Control de bomba por radio, comandado desde la web
  ----------------------------------------------------------------------------
  Qué hace este nodo:
    1. Se conecta al WiFi y reporta el estado a tu servidor (Flask).
       En la misma llamada recibe la configuración (modo AUTO/MANUAL y
       comando manual) que ponés desde la página web.
    2. Decide si la bomba debe estar ENCENDIDA o APAGADA.
    3. Envía la orden por radio (NRF24L01) al NODO BOMBA cada segundo.

  Este nodo YA NO MIDE el nivel del tanque (se quitó el sensor ultrasónico).
  Por eso:
    - En modo MANUAL la bomba obedece el botón de la web (encender / apagar).
    - En modo AUTO, como no hay sensor para saber cuándo está lleno, la bomba
      queda APAGADA por seguridad (nunca rebalsa).
    - Los umbrales que llegan del servidor se guardan pero no se usan.

  Si el servidor o el WiFi se caen, el nodo sigue mandando por radio la última
  orden conocida. El nodo bomba además corta sola si deja de recibir radio.

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

// --- Umbrales (llegan desde la web; hoy NO se usan porque no hay sensor) ---
float nivelAltoCorte    = 80.0;
float nivelBajoArranque = 30.0;

// --- MODO PRUEBA ---
// En 1: IGNORA la web y alterna CARGAR (ON) / CORTAR (OFF) cada 5 s,
//       para probar la radio y el relé con un patrón limpio.
// Poné 0 para volver al funcionamiento normal.
#define MODO_PRUEBA 0
const unsigned long PRUEBA_MS = 5000;  // cada 5 s cambia de estado

// ===========================================================================
// 2) PINES
// ===========================================================================

// Radio NRF24L01 por SPI: SCK=18, MISO=19, MOSI=22, CE=4, CSN=5.
// El MOSI va en el GPIO 22 (no en el 23 por defecto del VSPI): se pasa
// explícito a SPI.begin() en iniciarRadio().
#define PIN_SCK   18
#define PIN_MISO  19
#define PIN_MOSI  22
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

unsigned long tRadio   = 0;
unsigned long tServer  = 0;
unsigned long tPrueba  = 0;      // temporizador del MODO PRUEBA
bool          pruebaEstado = false;

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

bool          radioOk     = false;  // ¿el NRF24 respondió al inicializar?
unsigned long tRadioRetry = 0;      // último intento de re-inicializar la radio

// --- Seguimiento de los envíos por radio (lo ve la web) ---
// El número de comando va de 1 a 100 y vuelve a empezar, así no crece sin fin.
const uint32_t SEQ_MAX = 100;
bool          ultimoAckOk   = false; // ¿el último envío tuvo acuse de recibo de la bomba?
uint32_t      ultimoAckSeq  = 0;     // número del último comando que la bomba confirmó
unsigned long tUltimoAck    = 0;     // millis() del último acuse (0 = nunca)
uint16_t      histEnvios    = 0;     // bits: resultado de los últimos 10 envíos (1 = entregado)

// ===========================================================================
// 4) SETUP
// ===========================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  // --- Radio ---
  iniciarRadio();

  // --- WiFi ---
  conectarWiFi();

  Serial.println("Nodo TANQUE iniciado (sin sensor de nivel).");
}

// ===========================================================================
// 5) LOOP PRINCIPAL
// ===========================================================================
void loop() {
  unsigned long ahora = millis();

  // --- MODO PRUEBA: alterna CARGAR/CORTAR cada 5 s, ignorando la web ---
#if MODO_PRUEBA
  if (ahora - tPrueba >= PRUEBA_MS) {
    tPrueba = ahora;
    pruebaEstado = !pruebaEstado;
    desiredPump = pruebaEstado;
    Serial.printf("MODO PRUEBA -> %s\n", desiredPump ? "CARGAR (ON)" : "CORTAR (OFF)");
  }
#endif

  // --- a) Enviar la orden por radio ---
  if (ahora - tRadio >= INTERVALO_RADIO_MS) {
    tRadio = ahora;
    decidirBomba();
    enviarOrdenRadio();
    Serial.printf("Modo: %s | Bomba: %s | Radio: %s | WiFi: %s\n",
                  modo.c_str(),
                  desiredPump ? "ON" : "OFF",
                  radioOk ? "OK" : "SIN MODULO",
                  WiFi.status() == WL_CONNECTED ? "OK" : "sin conexion");
  }

  // --- b) Hablar con el servidor ---
  if (ahora - tServer >= INTERVALO_SERVER_MS) {
    tServer = ahora;
    comunicarServidor();
  }
}

// ===========================================================================
// 6) LÓGICA DE DECISIÓN
// ===========================================================================
void decidirBomba() {
#if MODO_PRUEBA
  return;  // en MODO PRUEBA el estado lo maneja el toggle de 5 s
#endif
  if (modo == "MANUAL") {
    desiredPump = manualPump;   // la web manda directo
  } else {
    desiredPump = false;        // AUTO sin sensor: no sabemos cuándo está lleno -> APAGADA
  }
}

// ===========================================================================
// 7) RADIO
// ===========================================================================
// Inicializa el NRF24. Devuelve true si el módulo responde por SPI.
bool iniciarRadio() {
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  radioOk = radio.begin(&SPI) && radio.isChipConnected();
  if (!radioOk) {
    Serial.println("ERROR: no se detecta el modulo NRF24. Revisar cableado (SPI) y 3.3V.");
    return false;
  }
  radio.setPALevel(RF24_PA_LOW);      // LOW = más estable en banco. Subir a HIGH solo con fuente sólida + capacitor.
  radio.setDataRate(RF24_250KBPS);    // menor velocidad = más alcance/robustez
  radio.setChannel(108);              // canal poco usado por WiFi
  radio.setRetries(5, 15);
  radio.openWritingPipe(pipeAddress); // este nodo TRANSMITE
  radio.stopListening();
  Serial.println("Radio NRF24 OK.");
  return true;
}

void enviarOrdenRadio() {
  // Sin módulo detectado NO llamamos a radio.write(): con el SPI mal cableado
  // la librería se queda esperando para siempre y el nodo se cuelga.
  // Cada 5 s reintentamos inicializarla por si se conectó/arregló en caliente.
  if (!radioOk || !radio.isChipConnected()) {
    radioOk = false;
    if (millis() - tRadioRetry >= 5000) {
      tRadioRetry = millis();
      iniciarRadio();
    }
    if (!radioOk) return;
  }

  msg.magic   = CMD_MAGIC;
  msg.command = desiredPump ? CMD_BOMBA_ON : CMD_BOMBA_OFF;
  msg.seq     = (msg.seq % SEQ_MAX) + 1;         // 1..100 y vuelve a 1
  bool ok = radio.write(&msg, sizeof(msg));      // true = la radio de la bomba acusó recibo
  ultimoAckOk = ok;
  histEnvios  = ((histEnvios << 1) | (ok ? 1 : 0)) & 0x03FF;   // últimos 10 envíos
  if (ok) {
    ultimoAckSeq = msg.seq;
    tUltimoAck   = millis();
  } else {
    Serial.printf("Aviso: comando #%lu sin acuse de la bomba (reintenta).\n", (unsigned long)msg.seq);
  }
}

// Cuántos de los últimos 10 envíos fueron entregados (0..10)
int entregadosUltimos10() {
  int n = 0;
  for (uint16_t b = histEnvios; b; b >>= 1) n += (b & 1);
  return n;
}

// ===========================================================================
// 8) COMUNICACIÓN CON EL SERVIDOR
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
  http.setConnectTimeout(6000);  // tiempo máximo para abrir la conexión TCP
  http.setTimeout(6000);         // tiempo máximo esperando la respuesta

  // Armar el JSON con el estado actual (sin nivel ni distancia: no hay sensor)
  StaticJsonDocument<256> body;
  body["pump_on"] = desiredPump;
  body["modo"]    = modo;
  body["rssi"]    = WiFi.RSSI();
  // Enlace de radio con la bomba (para el indicador de la web)
  body["radio_ok"]        = radioOk;            // el NRF24 del tanque responde
  body["radio_seq"]       = msg.seq;            // último comando enviado (1..100)
  body["radio_ack_ok"]    = ultimoAckOk;        // ¿ese último envío tuvo acuse?
  body["radio_ack_seq"]   = ultimoAckSeq;       // último comando con acuse
  body["radio_ack_hace_s"] = tUltimoAck ? (long)((millis() - tUltimoAck) / 1000) : -1;
  body["radio_ult10"]     = entregadosUltimos10();

  String payload;
  serializeJson(body, payload);

  int code = http.POST(payload);
  if (code == 200) {
    String resp = http.getString();
    aplicarConfigDelServidor(resp);
  } else {
    // code negativo = error de conexión (no se llegó al servidor);
    // errorToString explica cuál (refused, timeout, lost, etc.)
    Serial.printf("Server no responde (HTTP %d: %s). Sigo en modo local.\n",
                  code, http.errorToString(code).c_str());
    Serial.printf("  Diagnostico -> WiFi: %s | IP: %s | Gateway: %s | RSSI: %d dBm\n",
                  WiFi.status() == WL_CONNECTED ? "conectado" : "DESCONECTADO",
                  WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(),
                  WiFi.RSSI());
  }
  http.end();
}

// El servidor responde con la configuración actual (modo, comando manual, umbrales)
void aplicarConfigDelServidor(const String& json) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, json)) return; // si no parsea, mantengo lo que tengo

  if (doc.containsKey("modo"))                modo             = String((const char*)doc["modo"]);
  if (doc.containsKey("manual_pump"))         manualPump       = doc["manual_pump"];

  // Si la orden cambió, volvemos a reportar enseguida (en ~1,5 s en vez de 10 s)
  // para que la web vea rápido que el nodo tomó el comando y si la bomba lo acusó.
  bool nuevoDeseado = (modo == "MANUAL") ? manualPump : false;
  if (nuevoDeseado != desiredPump) {
    tServer = millis() - INTERVALO_SERVER_MS + 1500;
  }
  if (doc.containsKey("nivel_alto_corte"))    nivelAltoCorte   = doc["nivel_alto_corte"];
  if (doc.containsKey("nivel_bajo_arranque")) nivelBajoArranque= doc["nivel_bajo_arranque"];
}

// ===========================================================================
// 9) WIFI
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
