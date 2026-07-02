/*
  TEST DE RELÉ (banco) - Arduino Nano
  Sirve para probar el relé y el LED SIN la radio ni el maestro.
  Alterna el relé cada 2 s: deberías escuchar el "click" y ver el LED.

  Qué confirma:
   - Que el relé clickea (cableado IN/VCC/GND OK).
   - La polaridad: si "BOMBA ON" coincide con el relé en reposo (sin click)
     o no. Si está al revés, en el sketch real cambiás RELAY_ON/RELAY_OFF.

  Subí este sketch, abrí el Monitor Serie a 9600 y mirá/escuchá el relé.
  Cuando termines la prueba, volvés a cargar nodo_bomba.ino.
*/

#define PIN_RELAY  3
#define PIN_LED    4

// Mismo criterio que el sketch real (la mayoría de los módulos son activos en bajo)
#define RELAY_ON   LOW    // activa el relé (corta la bomba)
#define RELAY_OFF  HIGH   // reposo (bomba andando)

bool bombaOn = true;

void setBomba(bool on) {
  digitalWrite(PIN_RELAY, on ? RELAY_OFF : RELAY_ON);
  digitalWrite(PIN_LED, on ? HIGH : LOW);
  Serial.println(on ? "BOMBA -> ON  (rele en reposo, LED encendido)"
                    : "BOMBA -> CORTADA (rele activado, LED apagado)");
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_OFF);
  pinMode(PIN_RELAY, OUTPUT);
  Serial.println("== Test de rele ==");
  setBomba(true);
}

void loop() {
  delay(2000);
  bombaOn = !bombaOn;
  setBomba(bombaOn);
}
