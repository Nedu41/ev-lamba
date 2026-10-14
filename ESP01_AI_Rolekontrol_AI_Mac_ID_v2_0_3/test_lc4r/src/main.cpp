// LC Technology ESP-01 4 kanal role modulu testi v2 (ESP-01 <-> role MCU seri protokol).
// Cerceve: A0, kanal(1-4), durum(1=ac/0=kapat), toplam (ilk 3 baytin toplami, 8 bit).
// Kaynaklar (Tasmota/ESPHome sayfalari):
//  - STC15 (eski kart): 115200 veya 9600 baud, sadece cerceve.
//  - Nuvoton N76E003 (yeni kart): once MCU "AT+RST\r\n" yollar, ESP "WIFI CONNECTED / WIFI GOT IP /
//    AT+CIPMUX=1 / AT+CIPSERVER=1,8080 / AT+CIPSTO=360" ile cevap verince dinlemeye baslar (115200).
// Tur A (115200 + el sikisma): her role 1 sn acik.  Tur B (9600): her role 2 sn acik.
// Her komut iki kez gonderilir (bazi kartlar ilkini kacirir). HANGI turda role tiklarsa onu not edin.
#include <Arduino.h>

static const char EL_SIKISMA[] =
    "WIFI CONNECTED\nWIFI GOT IP\nAT+CIPMUX=1\nAT+CIPSERVER=1,8080\nAT+CIPSTO=360";

static String rx;

static void dinle() {  // "AT+RST" gelirse el sikisma cevabini ver
  while (Serial.available()) {
    char c = Serial.read();
    rx += c;
    if (rx.length() > 64) rx.remove(0, rx.length() - 32);
    if (rx.endsWith("AT+RST\r\n") || rx.endsWith("AT+RST\n")) {
      rx = "";
      Serial.print(EL_SIKISMA);
      Serial.flush();
    }
  }
}

static void bekle(uint32_t ms) {
  uint32_t t = millis();
  while (millis() - t < ms) { dinle(); delay(2); }
}

static void role(uint8_t ch, bool on) {
  uint8_t f[4] = {0xA0, ch, (uint8_t)(on ? 1 : 0), 0};
  f[3] = f[0] + f[1] + f[2];
  for (int i = 0; i < 2; i++) {
    Serial.write(f, 4);
    Serial.flush();
    bekle(60);
  }
}

static void tur(uint32_t baud, bool elSikisma, uint16_t sure) {
  Serial.begin(baud);
  rx = "";
  bekle(500);
  if (elSikisma) {  // MCU zaten AT+RST yollamis olabilir: istege bagli ayrica cevap ver
    Serial.print(EL_SIKISMA);
    Serial.flush();
    bekle(2000);
  }
  for (uint8_t ch = 1; ch <= 4; ch++) {
    role(ch, true);
    bekle(sure);
    role(ch, false);
    bekle(500);
  }
  Serial.end();
  delay(1500);
}

void setup() {}

void loop() {
  tur(115200, true, 1000);  // Tur A
  tur(9600, false, 2000);   // Tur B
  delay(4000);
}
