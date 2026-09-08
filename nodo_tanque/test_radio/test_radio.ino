/*
  TEST DE RADIO (banco) - ESP32 nodo tanque
  Lee registros del nRF24L01 por SPI cada 2 s y los imprime en crudo.
  Sirve para saber si el módulo responde, sin WiFi ni servidor.

  Cómo leerlo:
    - SETUP_AW = 0x03 y CONFIG = 0x08 (o 0x0E)  -> el módulo RESPONDE. Cableado OK.
    - Todos 0x00  -> MISO no llega (cable MISO/GPIO19), o el módulo no tiene 3.3V.
    - Todos 0xFF  -> MISO flotando: CSN no baja (cable CSN/GPIO5) o módulo desconectado.
    - Valores raros que cambian -> ruido: cables largos, mala masa, o SCK/MOSI cruzados.
  Cuando termines, volvés a cargar nodo_tanque.ino.
*/
#include <SPI.h>
#include <RF24.h>

#define PIN_CE   4
#define PIN_CSN  5
RF24 radio(PIN_CE, PIN_CSN);

uint8_t leerReg(uint8_t reg) {
  digitalWrite(PIN_CSN, LOW);
  SPI.transfer(reg & 0x1F);          // comando R_REGISTER
  uint8_t v = SPI.transfer(0xFF);
  digitalWrite(PIN_CSN, HIGH);
  return v;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_CSN, OUTPUT); digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_CE, OUTPUT);  digitalWrite(PIN_CE, LOW);
  Serial.println("== Test de radio nRF24 (ESP32) ==");
}

// Alterna el pin MOSI entre GPIO 23 (VSPI por defecto) y GPIO 22 (el que usa el nodo).
// Si con el cable en D22 responde y en D23 no, el pin 23 del ESP32 esta danado.
const int PINES_MOSI[] = {23, 22};
int idx = 0;

void loop() {
  int mosi = PINES_MOSI[idx]; idx = (idx + 1) % 2;
  SPI.end();
  SPI.begin(18, 19, mosi, PIN_CSN);      // SCK, MISO, MOSI, SS
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  Serial.printf("[MOSI en GPIO %d] ", mosi);
  uint8_t cfg = leerReg(0x00), aw = leerReg(0x03), ch = leerReg(0x05), st = leerReg(0x07);
  Serial.printf("CONFIG=0x%02X  SETUP_AW=0x%02X  RF_CH=0x%02X  STATUS=0x%02X  -> ",
                cfg, aw, ch, st);
  if (aw == 0x03 && (cfg & 0x08))       Serial.println("MODULO RESPONDE, cableado OK");
  else if (cfg == 0 && aw == 0 && st == 0)      Serial.println("todo 0x00: MISO no llega o modulo sin 3.3V");
  else if (cfg == 0xFF && aw == 0xFF)   Serial.println("todo 0xFF: CSN no baja o modulo desconectado");
  else                                  Serial.println("valores raros: revisar SCK/MOSI/masa");

  SPI.endTransaction();
  delay(1500);
}
