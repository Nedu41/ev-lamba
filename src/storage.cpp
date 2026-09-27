#include "storage.h"
#include <LittleFS.h>
#include "app_state.h"

// ============== LittleFS operations ==============
void loadDevices() {
  if (LittleFS.exists(DEVICES_FILE)) {
    File f = LittleFS.open(DEVICES_FILE, "r");
    if (f) {
      DEVICE_LIST_JSON = f.readString();
      f.close();
      Serial.println("Cihaz listesi yüklendi.");
      deserializeJson(doc, DEVICE_LIST_JSON);
      return;
    }
  }
  DEVICE_LIST_JSON = R"([{"name":"Salon","ip":"192.168.1.100"},{"name":"Mutfak","ip":"192.168.1.101"}])";
  deserializeJson(doc, DEVICE_LIST_JSON);
  Serial.println("Varsayılan cihaz listesi yüklendi.");
}

void saveDevices(String json) {
  File f = LittleFS.open(DEVICES_FILE, "w");
  if (f) {
    f.print(json);
    f.close();
    DEVICE_LIST_JSON = json;
    deserializeJson(doc, DEVICE_LIST_JSON);
    Serial.println("Cihazlar kaydedildi.");
  } else {
    Serial.println("HATA: devices.json yazılamadı.");
  }
}

// YENİ: Schedule config
void loadScheduleConfig() {
  scheduleMutex = xSemaphoreCreateMutex();
  if (LittleFS.exists(SCHEDULE_FILE)) {
    File f = LittleFS.open(SCHEDULE_FILE, "r");
    if (f) {
      DeserializationError err = deserializeJson(SCHEDULE_CONFIG_DOC, f);
      f.close();
      if (!err) {
        Serial.println("Schedule ayarları yüklendi.");

        // DÜZELTİLDİ: RAM dizileri (sunriseAction/sunsetAction)
        // restart sonrası sıfırlanıyor — JSON'dan geri yükle
        int devIdx = 0;
        for (JsonObject device : doc.as<JsonArray>()) {
          if (devIdx >= MAX_DEVICES) break;
          String name = device["name"] | "";
          if (name == "") { devIdx++; continue; }
          for (int r = 0; r < MAX_RELAYS; r++) {
            String rKey = "r" + String(r + 1);
            String srKey = rKey + "_sunrise_act";
            String ssKey = rKey + "_sunset_act";
            sunriseAction[devIdx][r] = SCHEDULE_CONFIG_DOC[name][srKey] | String("");
            sunsetAction[devIdx][r]  = SCHEDULE_CONFIG_DOC[name][ssKey] | String("");
          }
          devIdx++;
        }
        return;
      }
    }
  }
  SCHEDULE_CONFIG_DOC.clear();
  saveScheduleConfig();
  Serial.println("Varsayılan schedule oluşturuldu.");
}

void saveScheduleConfig() {
  File f = LittleFS.open(SCHEDULE_FILE, "w");
  if (f) {
    serializeJson(SCHEDULE_CONFIG_DOC, f);
    f.close();
    Serial.println("Schedule ayarları kaydedildi.");
  }
}

void saveWeatherConfig() {
  File f = LittleFS.open(WEATHER_CONFIG_FILE, "w");
  if (f) {
    serializeJson(WEATHER_CONFIG_DOC, f);
    f.close();
    Serial.println("WEATHER CONFIG -> Başarıyla LittleFS'e kaydedildi.");
  } else {
    Serial.println("HATA: LittleFS'e WEATHER CONFIG yazılamadı.");
  }
}

void loadWeatherConfig() {
  bool needSave = false;
  if (LittleFS.exists(WEATHER_CONFIG_FILE)) {
    File f = LittleFS.open(WEATHER_CONFIG_FILE, "r");
    if (f) {
      DeserializationError err = deserializeJson(WEATHER_CONFIG_DOC, f);
      f.close();
      if (err) {
        Serial.println("Config JSON bozuk, varsayılanlar yükleniyor.");
        WEATHER_CONFIG_DOC.clear();
        needSave = true;
      }
    }
  } else {
    Serial.println("Weather Config dosyası bulunamadı, varsayılanlar ayarlanıyor.");
    needSave = true;
  }

  if (!WEATHER_CONFIG_DOC.containsKey("active")) WEATHER_CONFIG_DOC["active"] = false;
  if (!WEATHER_CONFIG_DOC.containsKey("device_name")) WEATHER_CONFIG_DOC["device_name"] = "Yok";
  if (!WEATHER_CONFIG_DOC.containsKey("relay_id")) WEATHER_CONFIG_DOC["relay_id"] = 0;
  if (!WEATHER_CONFIG_DOC.containsKey("threshold")) WEATHER_CONFIG_DOC["threshold"] = 3;
  if (!WEATHER_CONFIG_DOC.containsKey("action")) WEATHER_CONFIG_DOC["action"] = "ON";

  // YENİ EKLEME: Rüzgar Hızı Eşiği (Km/s)
  if (!WEATHER_CONFIG_DOC.containsKey("wind_threshold")) WEATHER_CONFIG_DOC["wind_threshold"] = 15.0f;

  if (!WEATHER_CONFIG_DOC.containsKey("city")) WEATHER_CONFIG_DOC["city"] = "";
  if (!WEATHER_CONFIG_DOC.containsKey("district")) WEATHER_CONFIG_DOC["district"] = "";
  if (!WEATHER_CONFIG_DOC.containsKey("neighbourhood")) WEATHER_CONFIG_DOC["neighbourhood"] = "";
  if (!WEATHER_CONFIG_DOC.containsKey("city_name")) WEATHER_CONFIG_DOC["city_name"] = "";

  const char* lat = WEATHER_CONFIG_DOC["latitude"] | "";
  const char* lon = WEATHER_CONFIG_DOC["longitude"] | "";
  if (strlen(lat) < 2 || strlen(lon) < 2) {
    WEATHER_CONFIG_DOC["latitude"] = "41.0082";
    WEATHER_CONFIG_DOC["longitude"] = "28.9784";
    needSave = true;
  }

  if (needSave) {
    saveWeatherConfig();
    Serial.println("Varsayılan hava ayarları kaydedildi.");
  }
}
