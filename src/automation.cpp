#include "automation.h"
#include <time.h>
#include "app_state.h"
#include "devices.h"
#include "weather.h"
#include "telegram.h"
#include "display.h"
#include "web_server.h"
#include "events.h"

// ============== Existing weatherControlLogic ==============
void weatherControlLogic(int currentMinute) {

  bool weatherActive = WEATHER_CONFIG_DOC["active"] | false;
  if (!weatherActive) return;

  // BUGFIX: lastManualControlAction baslangicta 0 - "0" kontrolu olmadan her
  // acilistan sonra hic manuel kontrol yapilmadigi halde otomasyon 10dk atlaniyordu
  if (lastManualControlAction != 0 && millis() - lastManualControlAction < DEBOUNCE_DELAY_MS) {
    Serial.printf("Kontrol: Manuel kontrol nedeniyle hava kontrolu ATLANDI. Kalan süre: %lu ms\n",
                  DEBOUNCE_DELAY_MS - (millis() - lastManualControlAction));
    return;
  }

  // DÜZELTİLDİ: Mutex alındıktan sonra erken return öncesinde mutex serbest bırakılıyor
  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) != pdTRUE) return;

  String sunriseStr = CURRENT_WEATHER_DOC["sunrise_time"] | "00:00";
  String sunsetStr  = CURRENT_WEATHER_DOC["sunset_time"]  | "23:59";

  int sunriseMin = sunriseStr.substring(0, 2).toInt() * 60 + sunriseStr.substring(3, 5).toInt();
  int sunsetMin  = sunsetStr.substring(0, 2).toInt() * 60 + sunsetStr.substring(3, 5).toInt();

  bool isDaytime = (sunriseMin < sunsetMin)
                   ? (currentMinute >= sunriseMin && currentMinute < sunsetMin)
                   : (currentMinute >= sunriseMin || currentMinute < sunsetMin);

  int lightnessScore = CURRENT_WEATHER_DOC["lightness_score"] | 0;
  int threshold       = WEATHER_CONFIG_DOC["threshold"]   | 3;
  float radiation     = CURRENT_WEATHER_DOC["radiation"] | -1.0f;
  float lightOn       = WEATHER_CONFIG_DOC["light_threshold"] | 60;
  String deviceName   = WEATHER_CONFIG_DOC["device_name"] | "";
  int relayId          = WEATHER_CONFIG_DOC["relay_id"]    | 0;
  String action        = WEATHER_CONFIG_DOC["action"]      | "ON";

  xSemaphoreGive(weatherMutex);  // Her durumda serbest bırak

  // Gece: otomasyon kontrolu birakir (gece aydinlatmayi gunbatimi/zamanlayici
  // yonetir); ertesi sabah geceden kalan bir durumu "geri alma"ya kalkmasin
  if (!isDaytime) { lastWeatherDesired = ""; return; }
  if (deviceName == "" || relayId < 1 || relayId > MAX_RELAYS) return;

  String ip = "";
  for (JsonObject device : doc.as<JsonArray>()) {
    if ((device["name"] | String("")) == deviceName) {
      ip = device["ip"] | String("");
      break;
    }
  }
  if (ip == "") return;

  // Karanlik mi? Gercek isik (gunes isinimi W/m2) varsa onunla, yoksa eski
  // bulut skoruyla. Eskiden %100 bulut/yagmur kodu = "karanlik" sayiliyordu;
  // disarisi aydinlik olsa bile (olcum: yagmurlu 11:30'da 88 W/m2) lamba yaniyordu.
  // Histerezis: esigin altinda "karanlik", esigin %30 ustunde "aydinlik", arada
  // durum korunur - isik esik civarinda dalgalanirken lamba acip kapanmasin.
  bool dark, bright;
  if (radiation >= 0) {
    dark = radiation < lightOn;
    bright = radiation > lightOn * 1.3f;
  } else {
    dark = lightnessScore >= threshold;
    bright = !dark;
  }

  if (dark) {
    if (lastWeatherDesired == action) return;  // bu donem icin zaten gonderildi
    bool ok = sendControlSignal(ip, relayId, action == "ON" ? "on" : "off", "bulut");
    addEvent(deviceName, relayId, action == "ON", "bulut", ok);
    lastWeatherDesired = action;
    lastWeatherControlAction = millis();
    setForceRefresh();
    return;
  }

  // Aydinlandi: beklemeden geri al - ama sadece BIZIM yaptigimizi, ve bu arada
  // web'den elle mudahale edildiyse dokunma. Histerezis bandindaysa bekle.
  if (!bright || lastWeatherDesired != action) return;

  if (lastManualControlAction <= lastWeatherControlAction) {
    bool revertOn = (action != "ON");
    bool ok = sendControlSignal(ip, relayId, revertOn ? "on" : "off", "bulut-geri");
    addEvent(deviceName, relayId, revertOn, "bulut-geri", ok);
    setForceRefresh();
  }
  lastWeatherDesired = "";
}

// ============== handleSunSchedule ==============
void handleSunSchedule(int currentMinute) {
  static bool sunriseDone[MAX_DEVICES][MAX_RELAYS] = {{false}};
  static bool sunsetDone[MAX_DEVICES][MAX_RELAYS]  = {{false}};
  static int lastDay = -1;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  int currentDay = timeinfo.tm_wday;

  // Gün değiştiyse bayrakları sıfırla
  if (currentDay != lastDay) {
    for (int i = 0; i < MAX_DEVICES; i++)
      for (int j = 0; j < MAX_RELAYS; j++) {
        sunriseDone[i][j] = false;
        sunsetDone[i][j]  = false;
      }
    lastDay = currentDay;
  }

  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) != pdTRUE) return;
  String sunriseStr = CURRENT_WEATHER_DOC["sunrise_time"] | "06:00";
  String sunsetStr  = CURRENT_WEATHER_DOC["sunset_time"]  | "18:00";
  xSemaphoreGive(weatherMutex);

  int sunriseMin = sunriseStr.substring(0,2).toInt()*60 + sunriseStr.substring(3,5).toInt();
  int sunsetMin  = sunsetStr.substring(0,2).toInt()*60  + sunsetStr.substring(3,5).toInt();

  int devIdx = 0;
  for (JsonObject device : doc.as<JsonArray>()) {
    if (devIdx >= MAX_DEVICES) break;

    String ip   = device["ip"]   | "";
    String name = device["name"] | "";
    if (ip == "") { devIdx++; continue; }

    for (int r = 0; r < MAX_RELAYS; r++) {
      String sunriseAct = sunriseAction[devIdx][r];
      String sunsetAct  = sunsetAction[devIdx][r];
      bool useSunrise = (sunriseAct == "ON" || sunriseAct == "OFF");
      bool useSunset  = (sunsetAct == "ON" || sunsetAct == "OFF");

      if (useSunrise || useSunset) {
        // DÜZELTİLDİ: Tam dakika eşleşmesi (== sunriseMin) kullanılıyor.
        // Görev her 60 sn çalışıyor, currentMinute zaten dakika cinsinden.
        // Eğer cihaz o dakikada kapalıysa veya görev gecikmeli başladıysa
        // 2 dakikalık pencere (±1) ile yakala.
        bool sunriseWindow = !sunriseDone[devIdx][r] &&
                             (currentMinute == sunriseMin ||
                              currentMinute == sunriseMin + 1);
        bool sunsetWindow  = !sunsetDone[devIdx][r] &&
                             (currentMinute == sunsetMin ||
                              currentMinute == sunsetMin + 1);

        if (useSunrise && sunriseWindow) {
          sunriseDone[devIdx][r] = true;
          bool ok = sendControlSignal(ip, r+1, sunriseAct == "ON" ? "on" : "off", "gundogumu");
          addEvent(name, r + 1, sunriseAct == "ON", "gundogumu", ok);
          setForceRefresh();
          Serial.printf("[SunSched] %s R%d %s (Gündoğumu: %s)\n",
                        name.c_str(), r+1, sunriseAct.c_str(), sunriseStr.c_str());
        }
        if (useSunset && sunsetWindow) {
          sunsetDone[devIdx][r] = true;
          bool ok = sendControlSignal(ip, r+1, sunsetAct == "ON" ? "on" : "off", "gunbatimi");
          addEvent(name, r + 1, sunsetAct == "ON", "gunbatimi", ok);
          setForceRefresh();
          Serial.printf("[SunSched] %s R%d %s (Günbatımı: %s)\n",
                        name.c_str(), r+1, sunsetAct.c_str(), sunsetStr.c_str());
        }
      }
      // NOT: saatli baslangic/bitis HUB'da DEGIL - ESP01'in kendi olay tabanli
      // zamanlayicisi (v2.5) yapiyor; HUB da gondersin diye ikinci bir kaynak
      // olmasin. Eskiden HUB, gunbatimi/dogumu secili rolenin saatli kapanisini
      // tamamen yok sayiyordu.
    }
    devIdx++;
  }
}

// ============== YENİ: Rüzgar Uyarısı Mantığı ==============
void windAlertLogic() {
  static float hourlyMaxWind = 0.0f;
  static unsigned long lastAlertTime = 0;
  const unsigned long ALERT_INTERVAL = 3600000UL;  // 1 saat

  float wind = 0.0f;
  float threshold = 0.0f;
  String city = "";

  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
    wind = CURRENT_WEATHER_DOC["wind_speed"] | 0.0f;
    threshold = WEATHER_CONFIG_DOC["wind_threshold"] | 15.0f;
    city = CURRENT_WEATHER_DOC["city_name"] | "Bilinmeyen";
    xSemaphoreGive(weatherMutex);
  }

  unsigned long now = millis();

  // Saat dolduysa rekoru sıfırla
  if (now - lastAlertTime >= ALERT_INTERVAL) {
    hourlyMaxWind = 0.0f;
  }

  // Yeni saatlik rekor + eşik aşımı
  if (wind > threshold && wind > hourlyMaxWind) {
    hourlyMaxWind = wind;

    if (now - lastAlertTime >= ALERT_INTERVAL) {
      String msg =
        "⚠️ *YÜKSEK RÜZGAR UYARISI*\n"
        "📍 Konum: "
        + city + "\n"
                 "🌬️ Rüzgar: *"
        + String(wind, 1) + " Km/s*\n"
                            "⏱️ Saatlik rekor aşıldı";

      sendTelegramAlert(msg);
      lastAlertTime = now;
    }
  }
}

void updateWeatherAndControlTask(void* pvParameters) {
  unsigned long lastWeatherUpdate = 0;
  const unsigned long WEATHER_UPDATE_INTERVAL = 600000;  // 10 dakika
  bool firstRun = true;

  while (1) {
    unsigned long currentTime = millis();

    if (firstRun || weatherRefreshRequested || currentTime - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {
      Serial.println("DEBUG: Hava durumu güncelleme tetiklendi...");
      weatherRefreshRequested = false;
      forceNtpSync();
      updateWeatherFromConfig();
      lastWeatherUpdate = currentTime;
    }
    // NOT: haber cekme (fetchNews) artik displayTask icinde - TLS'in istedigi
    // tek parca buyuk bellegi ekran sprite'larini gecici silip acmak icin
    // sprite'larin SAHIBI olan task'ta yapilmasi gerekiyor (bkz. display.cpp)

    firstRun = false;

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {  // non-blocking check
      weatherControlLogic(timeinfo.tm_hour * 60 + timeinfo.tm_min);
      handleSunSchedule(timeinfo.tm_hour * 60 + timeinfo.tm_min);
    }

    // YENİ: Rüzgar uyarısı mantığını kontrol et
    windAlertLogic();

    // YENİ: hava durumu sadece 10dk'da bir değişse de (weather-only
    // değişiklikler CURRENT_STATUS_DOC'a dokunmuyor), WS istemcilerine
    // en geç 60sn'de bir güncel veri gitsin
    wsBroadcastStatus();

    // Hava durumu ve kontrol döngüsü 1 dakikada bir kontrol edilsin -
    // ama web'den yenileme istenirse (konum degisti / Guncelle butonu) beklemeden
    for (int i = 0; i < 60 && !weatherRefreshRequested; i++) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}
