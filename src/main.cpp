/*
  esp32Wroom_Sunucu_v5_7_0.ino
  - arduino esp32 board v2.0.18
  - Board esp32 dev modul v3.1.0
  - PSram - enable
  - Partition : huge_app
  - DÜZELTMELER v5.7.0:
    * volatile bool forceStatusRefresh -> atomic flag olarak güvenli hale getirildi
    * updateWeatherFromConfig() mutex dışında TFT yazma hatası düzeltildi
    * getDeviceStatusJson() içinde çift HTTPClient memory leak giderildi
    * handleProxy() içinde proxy URL decode hatası düzeltildi
    * handleSunSchedule() içinde devIdx sınır kontrolü eklendi
    * weatherControlLogic() içinde mutex alındıktan sonra return yapılmıyordu - düzeltildi
    * fetchOpenMeteo() hourly cloudcover doğru saat indeksi hesabı eklendi
    * MQTT callback'te sabit pin (0,2) yerine RELAY_x_PIN makroları kullanıldı
    * TFT ekran yazımı mutex dışına alındı (crash önlendi)
    * handleStatusFetch() WEATHER_CONFIG_DOC için de mutex eklendi
    * Bellek tasarrufu: gereksiz DynamicJsonDocument büyüklükleri azaltıldı
    * WiFi yeniden bağlanma döngüsü iyileştirildi (sonsuz döngü önlendi)
    * Telegram mesaj kuyruğu (simple flag) eklendi - HTTP timeout bloğunu önler
    * Tüm API anahtarları #define olarak güvenli yerleştirildi
*/

// Moduller: config.h (sabitler), app_state (paylasilan durum), storage (LittleFS),
// devices (ESP01 HTTP + durum gorevi), weather (Open-Meteo/NTP), automation
// (gundogumu/batimi, bulut, ruzgar, zamanlayici gorevi), display (TFT + haber),
// web_server (Async + WebSocket + proxy worker), mqtt_link, telegram.
// Web arayuzu: include/relay_webpage_multi.h

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include "app_state.h"
#include "storage.h"
#include "devices.h"
#include "weather.h"
#include "automation.h"
#include "display.h"
#include "web_server.h"
#include "mqtt_link.h"
#include "telegram.h"
#include "events.h"
#include "wifi_nets.h"

void setup() {
  Serial.begin(115200);

  displayBoot();
  mqttSetup();

  // Semaphore yarat
  statusMutex = xSemaphoreCreateMutex();
  initEvents();
  weatherMutex = xSemaphoreCreateMutex();
  newsMutex = xSemaphoreCreateMutex();

  // LittleFS başlat
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount HATA! Tekrar formatlanıyor...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println("LittleFS hala Mount edilemedi! Tekrar başlatılıyor.");
      delay(5000);
      ESP.restart();
    }
  }

  loadDevices();
  loadWeatherConfig();
loadScheduleConfig();  // YENİ

  // Once web'den eklenen kayitli aglar (tercih edilen once), tutmazsa
  // WiFi Manager (NVS'deki ev agi, o da olmazsa kurulum portali)
  if (!wifiNetsBoot()) {
    wm.setConfigPortalTimeout(120);
    wm.setDebugOutput(false);
    wm.autoConnect("MerkeziHUB_SETUP");
  }

  displayWifiConnected();

  Serial.println("WiFi Bağlandı. IP: " + WiFi.localIP().toString());

  // YENİ: mDNS (cihaz keşfi için ESPmDNS - header zaten vardı, hiç
  // başlatılmıyordu) + ArduinoOTA (bir daha USB'ye takmadan güncelleme)
  if (MDNS.begin("merkezihub")) {
    Serial.println("mDNS baslatildi: merkezihub.local");
  } else {
    Serial.println("mDNS baslatma HATASI");
  }
  ArduinoOTA.setHostname("merkezihub");
  ArduinoOTA.begin();
  Serial.println("ArduinoOTA baslatildi");

  // NTP zaman senkronizasyonunu zorla
  forceNtpSync();

  // Başlangıçta bir test mesajı gönder (opsiyonel)
  if (TELEGRAM_ENABLED) {
    delay(3000);  // WiFi'nin tam oturmasını bekle
    Serial.println("DEBUG: Başlangıç Telegram test mesajı gönderiliyor...");
    String initMsg = "🚀 *Merkezi HUB Başlatıldı* 🚀\n"
                     "📶 IP: "
                     + WiFi.localIP().toString() + "\n"
                                                   "📡 MAC: "
                     + WiFi.macAddress() + "\n"
                                           "✅ Sistem çalışıyor ve hazır.";
    sendTelegramAlert(initMsg);
  }

  // Görevleri başlat
  xTaskCreatePinnedToCore(updateAllDeviceStatuses, "Status", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(updateWeatherAndControlTask, "Weather", 8192, NULL, 1, NULL, 0);

  setupWebServer();

  // Bu noktadan sonra TFT'ye sadece displayTask erisir
  startDisplayTask();
}

void loop() {
  // *** WiFi kontrolü - DÜZELTİLDİ: Bloklu döngü kaldırıldı ***
  static unsigned long lastWiFiCheck = 0;
  static uint8_t wifiFailCount = 0;
  if (millis() - lastWiFiCheck > 30000) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi bağlantısı kesildi! Yeniden bağlanıyor...");
      WiFi.reconnect();
      wifiFailCount++;
      if (wifiFailCount >= 3) {  // 3 başarısız deneme sonrası restart
        Serial.println("WiFi bağlanamadı (3 deneme), restart...");
        ESP.restart();
      }
    } else {
      wifiFailCount = 0;  // Bağlandıysa sayacı sıfırla
    }
  }

  webLoop();
  ArduinoOTA.handle();
  mqttLoop();

  delay(10);  // 10ms - WiFi stack icin daha iyi
}
