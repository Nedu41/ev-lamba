// LC Technology ESP-01 4 kanal role modulu testi (ESP-01 <-> role MCU seri protokol).
// Cerceve: A0, kanal(1-4), durum(1=ac/0=kapat), toplam (ilk 3 baytin toplami, 8 bit).
// Kaynaklar baud icin 115200 (ESPHome) ve 9600 (eski kartlar) veriyor -> ikisini sirayla dener.
// Her turda role 1..4 sirayla 1 sn acilip kapanir; HANGI baud'da role tiklarsa onu not edin.
#include <Arduino.h>

static void role(uint8_t ch, bool on) {
  uint8_t f[4] = {0xA0, ch, (uint8_t)(on ? 1 : 0), 0};
  f[3] = f[0] + f[1] + f[2];
  Serial.write(f, 4);
  Serial.flush();
}

void setup() {}

void loop() {
  const uint32_t bauds[2] = {115200, 9600};
  for (int b = 0; b < 2; b++) {
    Serial.begin(bauds[b]);
    delay(300);
    // b=0 (115200): her role 1 sn acik; b=1 (9600): her role 2 sn acik (ayirt etmek icin)
    uint16_t sure = b == 0 ? 1000 : 2000;
    for (uint8_t ch = 1; ch <= 4; ch++) {
      role(ch, true);
      delay(sure);
      role(ch, false);
      delay(400);
    }
    Serial.end();
    delay(2000);
  }
  delay(4000);  // turlar arasi bosluk
}
