#include "automation.h"
#include <time.h>
#include "app_state.h"
#include "devices.h"
#include "weather.h"
#include "telegram.h"
#include "display.h"
#include "web_server.h"

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
  String deviceName   = WEATHER_CONFIG_DOC["device_name"] | "";
  int relayId          = WEATHER_CONFIG_DOC["relay_id"]    | 0;
  String action        = WEATHER_CONFIG_DOC["action"]      | "ON";

  xSemaphoreGive(weatherMutex);  // Her durumda serbest bırak

  if (!isDaytime) return;
  if (deviceName == "" || relayId < 1 || relayId > MAX_RELAYS) return;

  // BUGFIX: bu fonksiyon eskiden isDaytime hesaplayip hicbir sey yapmadan donuyordu
  // ("Gunduz mantigi buraya eklenebilir" yorumuyla) - esik/bulut karsilastirmasi hic
  // yapilmiyordu, weather_config'teki threshold/action/device_name/relay_id alanlari
  // olu veriydi. Simdi lightness_score (1=acik,2=parcali,3=kapali) >= threshold oldugunda
  // konfigure edilen action gonderiliyor. Kenar-tetiklemeli: ayni "kapali" donem icinde
  // tekrar tekrar komut gonderilmesin diye lastWeatherDesired ile tek seferlik tetikleniyor;
  // hava acilinca sifirlanip bir sonraki kapanmada tekrar tetiklenebiliyor. Bu ozellik icin
  // "acilinca ne yap" diye ayri bir alan olmadigindan otomatik ters islem gonderilmiyor -
  // normal schedule/manuel kontrol geri almayi ustleniyor.
  bool conditionMet = (lightnessScore >= threshold);

  if (!conditionMet) {
    lastWeatherDesired = "";  // yeni bir kapanma doneminde tekrar tetiklenebilsin
    return;
  }

  if (lastWeatherDesired == action) return;  // bu donem icin zaten gonderildi

  String ip = "";
  for (JsonObject device : doc.as<JsonArray>()) {
    if ((device["name"] | String("")) == deviceName) {
      ip = device["ip"] | String("");
      break;
    }
  }
  if (ip == "") return;

  sendControlSignal(ip, relayId, action == "ON" ? "on" : "off");
  lastWeatherDesired = action;
  lastWeatherControlAction = millis();
  setForceRefresh();
  Serial.printf("[WeatherCtrl] %s R%d %s (lightness_score=%d >= threshold=%d)\n",
                deviceName.c_str(), relayId, action.c_str(), lightnessScore, threshold);
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
          sendControlSignal(ip, r+1, sunriseAct == "ON" ? "on" : "off");
          setForceRefresh();
          Serial.printf("[SunSched] %s R%d %s (Gündoğumu: %s)\n",
                        name.c_str(), r+1, sunriseAct.c_str(), sunriseStr.c_str());
        }
        if (useSunset && sunsetWindow) {
          sunsetDone[devIdx][r] = true;
          sendControlSignal(ip, r+1, sunsetAct == "ON" ? "on" : "off");
          setForceRefresh();
          Serial.printf("[SunSched] %s R%d %s (Günbatımı: %s)\n",
                        name.c_str(), r+1, sunsetAct.c_str(), sunsetStr.c_str());
        }
      } else {
        // Sabit zaman schedule
        if (xSemaphoreTake(scheduleMutex, MUTEX_TIMEOUT) == pdTRUE) {
          String rKey   = "r" + String(r+1);
          String start  = SCHEDULE_CONFIG_DOC[name][rKey + "_start"] | "";
          String end    = SCHEDULE_CONFIG_DOC[name][rKey + "_end"]   | "";
          int daysMask  = SCHEDULE_CONFIG_DOC[name][rKey + "_days"]  | 0;

          if (start != "" && end != "" && (daysMask & (1 << currentDay))) {
            int startMin = start.substring(0,2).toInt()*60 + start.substring(3,5).toInt();
            int endMin   = end.substring(0,2).toInt()*60   + end.substring(3,5).toInt();

            // DÜZELTİLDİ: ±1 dakika tolerans (60 sn görev gecikmesine karşı)
            bool atStart = (currentMinute == startMin || currentMinute == startMin + 1);
            bool atEnd   = (currentMinute == endMin   || currentMinute == endMin + 1);

            if (atStart) {
              sendControlSignal(ip, r+1, "on");
              setForceRefresh();
              Serial.printf("[TimeSched] %s R%d ON (%s)\n", name.c_str(), r+1, start.c_str());
            } else if (atEnd) {
              sendControlSignal(ip, r+1, "off");
              setForceRefresh();
              Serial.printf("[TimeSched] %s R%d OFF (%s)\n", name.c_str(), r+1, end.c_str());
            }
          }
          xSemaphoreGive(scheduleMutex);
        }
      }
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
  unsigned long lastNewsUpdate = 0;
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

    if (firstRun || currentTime - lastNewsUpdate >= NEWS_UPDATE_INTERVAL) {
      fetchNews();
      lastNewsUpdate = currentTime;
    }
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
