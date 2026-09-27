#include "weather.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "app_state.h"
#include "storage.h"
#include "telegram.h"  // urlEncode

// =================== Nominatim geocode ===================
bool geocodeAddress(const String& address, String& outLat, String& outLon, String& displayName) {
  if (address.length() < 2) return false;
  HTTPClient http;
  String q = urlEncode(address);
  String url = "https://nominatim.openstreetmap.org/search?format=json&limit=1&q=" + q;
  http.begin(url);
  http.setTimeout(3000);
  http.setConnectTimeout(2000);
  http.addHeader("User-Agent", "MerkeziHUB/1.0");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  DynamicJsonDocument j(4096);
  DeserializationError err = deserializeJson(j, payload);
  if (err) {
    Serial.printf("geocodeAddress: JSON parse error: %s\n", err.c_str());
    return false;
  }
  if (!j.is<JsonArray>() || j.size() == 0) return false;
  JsonObject first = j[0];
  if (first.containsKey("lat") && first.containsKey("lon")) {
    outLat = String((const char*)first["lat"].as<const char*>());
    outLon = String((const char*)first["lon"].as<const char*>());
    displayName = first["display_name"] | address;
    Serial.printf("geocodeAddress: found %s => %s,%s\n", displayName.c_str(), outLat.c_str(), outLon.c_str());
    return true;
  }
  return false;
}

// =================== Open-Meteo fetch ===================
bool fetchOpenMeteo(const String& lat, const String& lon, JsonObject& out, DynamicJsonDocument& scratch) {
  if (lat.length() < 2 || lon.length() < 2) return false;

  HTTPClient http;
  // DEGISTI: model acikca ecmwf_ifs025'e sabitlendi. SuDepoSis_v2 projesinde
  // sahada 4 model karsilastirildi (2026-09-01): varsayilan "best_match" bu
  // bolgede gercekte yagan yagmuru hep %0-3 gosterip kacirdi, ecmwf_ifs025
  // (Avrupa Orta Vadeli Tahmin Merkezi) ayni gunler icin %35-46 verip telefon
  // hava uygulamalariyla birebir ortustu - bkz. SuDepoSis_v2/esp32_master/
  // include/config.h WEATHER_FORECAST_MODEL yorumu.
  // forecast_days=1: sadece bugunun o anki saati kullaniliyor. Eskiden 7 gun
  // (168 saat x 4 alan) isteniyordu - ekran sprite'lari heap'i azaltinca
  // deserializeJson "NoMemory" ile basarisiz oldu (2026-09-27 seri log).
  // Saatlik ruzgar alanlari da kaldirildi: ruzgar current_weather'dan geliyor.
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&hourly=cloudcover,relativehumidity_2m" + "&daily=sunrise,sunset" + "&current_weather=true&timezone=auto&forecast_days=1&models=ecmwf_ifs025";

  Serial.printf("DEBUG: Open-Meteo URL: %s\n", url.c_str());

  http.begin(url);
  http.setTimeout(3000);
  http.setConnectTimeout(2000);
  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    Serial.printf("fetchOpenMeteo: HTTP code %d\n", code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // DEBUG: Ham JSON'u görelim
  Serial.println("DEBUG: Open-Meteo RAW JSON (ilk 500 karakter):");
  Serial.println(payload.substring(0, 500));

  scratch.clear();
  DeserializationError err = deserializeJson(scratch, payload);

  if (err) {
    Serial.printf("fetchOpenMeteo: JSON parse error %s\n", err.c_str());
    return false;
  }

  // current_weather bölümünü debug et
  if (scratch.containsKey("current_weather")) {
    JsonObject cw = scratch["current_weather"].as<JsonObject>();
    float temp = cw["temperature"] | 0.0f;
    float windSpeed = cw["windspeed"] | 0.0f;
    int windDir = cw["winddirection"] | 0;
    int weathercode = cw["weathercode"] | 0;

    Serial.printf("DEBUG: current_weather -> temp: %.1f, wind_speed: %.1f, wind_dir: %d, weathercode: %d\n",
                  temp, windSpeed, windDir, weathercode);

    out["temp"] = temp;
    out["wind_speed"] = windSpeed;
    out["wind_dir"] = windDir;
    out["weathercode"] = weathercode;
    out["is_day"] = cw["is_day"] | 1;  // ekran: gunes/ay, gunduz/gece gokyuzu
  } else {
    Serial.println("DEBUG: current_weather anahtarı yok!");
    out["temp"] = 0.0f;
    out["wind_speed"] = 0.0f;
    out["wind_dir"] = 0.0f;
  }

  // Saatlik dizilerde su anki saatin indeksi (bulut VE nem ayni saatten)
  int hourIdx = 0;
  JsonArray timeArr = scratch["hourly"]["time"].as<JsonArray>();
  if (!timeArr.isNull()) {
    time_t nowt = time(nullptr);
    struct tm tmnow;
    localtime_r(&nowt, &tmnow);
    char target[20];
    snprintf(target, sizeof(target), "%04d-%02d-%02dT%02d:00",
             tmnow.tm_year + 1900, tmnow.tm_mon + 1, tmnow.tm_mday, tmnow.tm_hour);
    for (size_t i = 0; i < timeArr.size(); i++) {
      const char* t = timeArr[i];
      if (t && strcmp(t, target) == 0) { hourIdx = i; break; }
    }
  }

  JsonArray clouds = scratch["hourly"]["cloudcover"].as<JsonArray>();
  out["clouds"] = (hourIdx < (int)clouds.size()) ? clouds[hourIdx].as<int>() : 0;

  // BUGFIX: nem eskiden hep rh[0] (gece 00:00) okunuyordu, su anki saat degil
  JsonArray rh = scratch["hourly"]["relativehumidity_2m"].as<JsonArray>();
  out["humidity"] = (hourIdx < (int)rh.size()) ? rh[hourIdx].as<int>() : 0;

  // Gün doğumu ve batımı
  if (scratch.containsKey("daily") && scratch["daily"].containsKey("sunrise") && scratch["daily"].containsKey("sunset")) {
    JsonArray sunriseArr = scratch["daily"]["sunrise"].as<JsonArray>();
    JsonArray sunsetArr = scratch["daily"]["sunset"].as<JsonArray>();

    if (sunriseArr.size() > 0) {
      const char* sunriseStr = sunriseArr[0];
      String sunrise = String(sunriseStr);
      if (sunrise.length() >= 16) {
        out["sunrise_time"] = sunrise.substring(11, 16);  // Saat:dakika kısmını al
      }
    }

    if (sunsetArr.size() > 0) {
      const char* sunsetStr = sunsetArr[0];
      String sunset = String(sunsetStr);
      if (sunset.length() >= 16) {
        out["sunset_time"] = sunset.substring(11, 16);  // Saat:dakika kısmını al
      }
    }
  }

  return true;
}

// =================== Wind direction helper ===================
String getWindDirection(int degrees) {
  if (degrees >= 337.5 || degrees < 22.5) return "Kuzey";
  if (degrees >= 22.5 && degrees < 67.5) return "KuzeyDoğu";
  if (degrees >= 67.5 && degrees < 112.5) return "Doğu";
  if (degrees >= 112.5 && degrees < 157.5) return "GüneyDoğu";
  if (degrees >= 157.5 && degrees < 202.5) return "Güney";
  if (degrees >= 202.5 && degrees < 247.5) return "GüneyBatı";
  if (degrees >= 247.5 && degrees < 292.5) return "Batı";
  if (degrees >= 292.5 && degrees < 337.5) return "KuzeyBatı";
  return "-";
}


// =================== updateWeatherFromConfig ===================
void updateWeatherFromConfig() {
  Serial.println("DEBUG: updateWeatherFromConfig başladı");

  String city, district, neighbourhood, lat, lon, displayName;

  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
    city = (const char*)(WEATHER_CONFIG_DOC["city"] | "");
    district = (const char*)(WEATHER_CONFIG_DOC["district"] | "");
    neighbourhood = (const char*)(WEATHER_CONFIG_DOC["neighbourhood"] | "");
    lat = (const char*)(WEATHER_CONFIG_DOC["latitude"] | "");
    lon = (const char*)(WEATHER_CONFIG_DOC["longitude"] | "");
    displayName = (const char*)(WEATHER_CONFIG_DOC["city_name"] | "");
    xSemaphoreGive(weatherMutex);
  } else {
    Serial.println("ERROR: Mutex alınamadı");
    return;
  }

  // Eğer adres alanları doluysa ve koordinatlar boşsa, geocode yap
  if ((city.length() > 0 || district.length() > 0 || neighbourhood.length() > 0) && (lat.length() < 2 || lon.length() < 2)) {
    String address = neighbourhood + (neighbourhood.length() > 0 && district.length() > 0 ? ", " : "") + district + (district.length() > 0 && city.length() > 0 ? ", " : "") + city;
    String outLat, outLon, disp;
    if (geocodeAddress(address, outLat, outLon, disp)) {
      if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
        WEATHER_CONFIG_DOC["latitude"] = outLat;
        WEATHER_CONFIG_DOC["longitude"] = outLon;
        WEATHER_CONFIG_DOC["city_name"] = disp;
        saveWeatherConfig();
        xSemaphoreGive(weatherMutex);
      }
      lat = outLat;
      lon = outLon;
      displayName = disp;
    } else {
      if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
        if (city.length() > 0 || district.length() > 0) {
          WEATHER_CONFIG_DOC["city_name"] = city + (district.length() > 0 ? ", " + district : "");
          saveWeatherConfig();
        }
        xSemaphoreGive(weatherMutex);
      }
    }
  }

  // Hava durumu verilerini çek
  DynamicJsonDocument scratch(12288);
  DynamicJsonDocument outDoc(2048);
  JsonObject out = outDoc.to<JsonObject>();

  bool fetchSuccess = fetchOpenMeteo(lat, lon, out, scratch);
  if (!fetchSuccess) {
    Serial.println("ERROR: Open-Meteo fetch başarısız");
    if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
      CURRENT_WEATHER_DOC.clear();
      CURRENT_WEATHER_DOC["description"] = "FETCH_ERROR";
      CURRENT_WEATHER_DOC["city_name"] = displayName.length() > 0 ? displayName : (city.length() > 0 ? city : "Bilinmeyen");
      xSemaphoreGive(weatherMutex);
    }
    weatherDisplayDirty = true;
    return;
  }

  // Hava durumu verilerini işle ve mutex içinde kaydet
  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
    CURRENT_WEATHER_DOC.clear();
    CURRENT_WEATHER_DOC["temp"]       = out["temp"] | 0.0f;
    CURRENT_WEATHER_DOC["wind_speed"] = out["wind_speed"] | 0.0f;
    CURRENT_WEATHER_DOC["wind_dir"]   = out["wind_dir"] | 0;
    CURRENT_WEATHER_DOC["clouds"]     = out["clouds"] | 0;
    CURRENT_WEATHER_DOC["humidity"]   = out["humidity"] | 0;

    int windDirDegrees = out["wind_dir"] | 0;
    CURRENT_WEATHER_DOC["wind_direction"] = getWindDirection(windDirDegrees);

    if (displayName.length() > 0) {
      CURRENT_WEATHER_DOC["city_name"] = displayName;
    } else if (city.length() > 0) {
      CURRENT_WEATHER_DOC["city_name"] = city;
    } else {
      CURRENT_WEATHER_DOC["city_name"] = "İstanbul";
    }

    CURRENT_WEATHER_DOC["description"]  = "OK";
    CURRENT_WEATHER_DOC["sunrise_time"] = out["sunrise_time"] | "";
    CURRENT_WEATHER_DOC["sunset_time"]  = out["sunset_time"] | "";

    int clouds = CURRENT_WEATHER_DOC["clouds"] | 0;
    int lightnessScore = 0;
    if      (clouds <= 25) lightnessScore = 1;
    else if (clouds <= 99) lightnessScore = 2;
    else                   lightnessScore = 3;
    int weathercode = out["weathercode"] | 0;
    if ((weathercode >= 51 && weathercode <= 99) || (weathercode >= 200 && weathercode < 700)) {
      lightnessScore = 3;
    }
    CURRENT_WEATHER_DOC["lightness_score"] = lightnessScore;
    CURRENT_WEATHER_DOC["weathercode"] = weathercode;   // WMO kodu - ekran animasyonu
    CURRENT_WEATHER_DOC["is_day"] = out["is_day"] | 1;

    Serial.printf("DEBUG: Hava durumu güncellendi - Şehir: %s, Sıcaklık: %.1f°C, Rüzgar: %.1f Km/s, Skor: %d\n",
                  CURRENT_WEATHER_DOC["city_name"].as<String>().c_str(),
                  CURRENT_WEATHER_DOC["temp"].as<float>(),
                  CURRENT_WEATHER_DOC["wind_speed"].as<float>(), lightnessScore);

    xSemaphoreGive(weatherMutex);
  }

  weatherDisplayDirty = true;  // cizimi displayTask yapar (tek SPI sahibi)
}

// ============== NTP ==============
void forceNtpSync() {
  Serial.println("DEBUG: NTP Senkronizasyonu zorlaniyor...");
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov", "0.tr.pool.ntp.org");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    Serial.printf("DEBUG: NTP Guncellendi: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    Serial.println("DEBUG: NTP Guncelleme Basarisiz.");
  }
}
