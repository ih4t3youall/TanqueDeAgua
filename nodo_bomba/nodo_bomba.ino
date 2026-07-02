/*
  ============================================================================
  NODO BOMBA (ESCLAVO)  -  Arduino Nano
  ----------------------------------------------------------------------------
  Qué hace este nodo:
    - Escucha por radio (NRF24L01 +PA+LNA) la orden que manda el NODO TANQUE.
    - Acciona UN relé que está en serie con la línea "ON" de la bomba.

  Lógica de la bomba (según armado actual):
    - La bomba queda SIEMPRE en ON. Esa línea pasa por el relé en COM + NC.
    - Relé en reposo  -> NC cerrado -> bomba ANDANDO.
    - Relé activado   -> abre       -> bomba CORTADA.
    - BOMBA_ON  (maestro: "encender")     -> relé reposo   -> bomba ANDANDO.
    - BOMBA_OFF (maestro: "apagar/lleno") -> relé activado -> bomba CORTADA.
    - El botón ROJO del contactor sigue funcionando para apagar a mano.

  PROTOCOLO: el paquete trae una firma (magic) + un código explícito
  BOMBA_ON / BOMBA_OFF. Si no coincide, se IGNORA (no toca la bomba). Así
  la bomba nunca cambia por "recibir señal", solo por un comando válido.

  FAIL-SAFE híbrido: mantiene el estado ante microcortes; solo corta la bomba
  si pasan ~2 min sin un comando VÁLIDO (evita rebalse si la radio muere).

  Alimentación del NRF24: módulo AMS1117 5V->3.3V (IN=5V del Nano, OUT=VCC del
  nRF24, GND común). No usar el pin 3.3V del Nano. No hace falta capacitor extra.

  Librería necesaria: RF24 (by TMRh20).
  ============================================================================
*/

#include <SPI.h>
#include <RF24.h>

// ===========================================================================
// PINES
// ===========================================================================
// Radio NRF24L01 (SPI del Nano: SCK=13, MISO=12, MOSI=11)
#define PIN_CE   9
#define PIN_CSN  10
RF24 radio(PIN_CE, PIN_CSN);

// Debe ser IDÉNTICA a la del nodo tanque
const byte pipeAddress[6] = "TANK1";

// Relé (un solo canal, en serie con la línea ON de la bomba)
#define PIN_RELAY  3
#define PIN_LED    4    // LED de estado opcional (220R a GND). Encendido = bomba ON.

// La mayoría de los módulos de relé son ACTIVOS EN BAJO.
// Si tu relé actúa al revés (clickea cuando debería estar en reposo), intercambiá estos dos.
#define RELAY_ON   LOW   // nivel que ACTIVA el relé (corta la bomba)
#define RELAY_OFF  HIGH  // nivel que LO DEJA EN REPOSO (bomba andando)

// --- Protocolo de radio (debe coincidir EXACTO con el nodo tanque) ---
#define CMD_MAGIC      0x7E   // firma: valida que el paquete es nuestro
#define CMD_BOMBA_ON   0xA5   // código explícito de ENCENDER
#define CMD_BOMBA_OFF  0x5A   // código explícito de APAGAR

// FAIL-SAFE híbrido: solo corta si pasan FAILSAFE_MS sin un comando VÁLIDO.
const unsigned long FAILSAFE_MS = 120000UL;  // 2 minutos

// ===========================================================================
// ESTADO
// ===========================================================================
// __attribute__((packed)) -> mismo tamaño (6 bytes) que en el ESP32. Debe coincidir.
struct __attribute__((packed)) RadioMsg {
  uint8_t  magic;     // debe ser CMD_MAGIC
  uint8_t  command;   // CMD_BOMBA_ON o CMD_BOMBA_OFF
  uint32_t seq;
};
RadioMsg msg;

bool pumpOn = true;            // estado actual de la bomba (arranca en ON)
unsigned long lastMsg = 0;     // momento del último mensaje recibido
bool huboSenal = false;        // ¿recibimos al menos un mensaje válido?

// Aplica el estado de la bomba al relé y al LED
void aplicarBomba(bool on) {
  digitalWrite(PIN_RELAY, on ? RELAY_OFF : RELAY_ON); // ON = relé en reposo
  digitalWrite(PIN_LED, on ? HIGH : LOW);
  pumpOn = on;
}

// ===========================================================================
// SETUP
// ===========================================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED, OUTPUT);
  // Dejamos el relé en reposo (bomba ON) ANTES de configurar la salida
  digitalWrite(PIN_RELAY, RELAY_OFF);
  pinMode(PIN_RELAY, OUTPUT);
  aplicarBomba(true);           // arranca con la bomba en ON (estado por defecto)

  if (!radio.begin()) {
    Serial.println("ERROR: no se detecta el NRF24. Revisar cableado / 3.3V del AMS1117.");
  }
  radio.setPALevel(RF24_PA_LOW);      // LOW = más estable en banco. Subir a HIGH solo con fuente sólida + capacitor.
  radio.setDataRate(RF24_250KBPS);    // menor velocidad = más alcance/robustez
  radio.setChannel(108);              // canal poco usado por WiFi
  radio.setRetries(5, 15);
  radio.openReadingPipe(1, pipeAddress); // este nodo RECIBE
  radio.startListening();

  lastMsg = millis();
  Serial.println("Nodo BOMBA iniciado (esclavo). Bomba en ON por defecto.");
}

// ===========================================================================
// LOOP
// ===========================================================================
void loop() {
  // ¿Llegó un paquete?
  if (radio.available()) {
    radio.read(&msg, sizeof(msg));

    // SOLO actuamos si trae la firma correcta Y un código explícito.
    // Cualquier otra cosa (ruido, paquete ajeno) se ignora: no toca la bomba.
    if (msg.magic == CMD_MAGIC) {
      if (msg.command == CMD_BOMBA_ON) {
        lastMsg = millis();  huboSenal = true;
        if (!pumpOn) { aplicarBomba(true);  Serial.println("BOMBA -> ON"); }
      } else if (msg.command == CMD_BOMBA_OFF) {
        lastMsg = millis();  huboSenal = true;
        if (pumpOn)  { aplicarBomba(false); Serial.println("BOMBA -> CORTADA"); }
      }
      // otros valores de command -> ignorar
    }
    // magic incorrecto -> ignorar
  }

  // FAIL-SAFE híbrido: solo corta si pasa MUCHO tiempo sin un comando válido.
  if (huboSenal && (millis() - lastMsg > FAILSAFE_MS)) {
    if (pumpOn) {
      aplicarBomba(false);
      Serial.println("FAILSAFE: 2 min sin senal valida, corto la bomba por seguridad.");
    }
  }
}
