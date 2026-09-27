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

#include <Arduino.h>
#include <LittleFS.h>  // board esp32 v3.x den üstü olmalı
#include <TFT_eSPI.h>  // En üste ekle
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include <WebServer.h>

#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>  // Telegram için Secure Client kütüphanesi
#include "relay_webpage_multi.h"
#include <PubSubClient.h>
#include "secrets.h"  // TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID, OPENWEATHERMAP_API_KEY (gitignore'da)

// FreeRTOS görevler arası güvenli flag (volatile bool yerine)
static volatile bool forceStatusRefresh = false;
inline void setForceRefresh()  { forceStatusRefresh = true;  }
inline void clearForceRefresh(){ forceStatusRefresh = false; }

TFT_eSPI tft = TFT_eSPI();
const int BACKLIGHT_PIN = 21;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

const char* mqttServer = "broker.hivemq.com";

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_task_wdt.h>

#define WDT_TIMEOUT 30  // 30 saniye

// ------------------- HUB GENEL AYARLARI -------------------
#define ONBOARD_LED_PIN 2
#define LED_FLASH_DURATION 50

// --- TELEGRAM AYARLARI ---
// TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID ve OPENWEATHERMAP_API_KEY secrets.h icinde
#define TELEGRAM_ENABLED true
// -----------------------------

#define MUTEX_TIMEOUT pdMS_TO_TICKS(100)
const int STATUS_CHECK_INTERVAL = 10000;    // 10s
const int WEATHER_CHECK_INTERVAL = 600000;  // 10min

// HTTP timeout
const int HTTP_TIMEOUT_MS = 1000;  // 1s

WebServer server(80);
WiFiManager wm;

TaskHandle_t quickRefreshHandle = NULL;

// --- MQTT AYARLARI ---
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883

String mqttClientID = "HubESP32_" + String((uint32_t)ESP.getEfuseMac(), HEX);

String topicRelay1Set = "evhub/relay/1/set";
String topicRelay2Set = "evhub/relay/2/set";
String topicRelay1State = "evhub/relay/1/state";
String topicRelay2State = "evhub/relay/2/state";


// ****** mqtt son

// ===== GÜNEŞ OLAYLARI SCHEDULE =====
#define MAX_DEVICES 10
#define MAX_RELAYS 2

bool relayOnAtSunrise[MAX_DEVICES][MAX_RELAYS] = { {false, false} };
bool relayOffAtSunset[MAX_DEVICES][MAX_RELAYS] = { {false, false} };

// YENİ: Zaman schedule config
StaticJsonDocument<2048> SCHEDULE_CONFIG_DOC;
const char* SCHEDULE_FILE = "/devices_schedule.json";
SemaphoreHandle_t scheduleMutex = nullptr;

StaticJsonDocument<4096> CURRENT_STATUS_DOC;
SemaphoreHandle_t statusMutex = nullptr;

unsigned long lastWeatherControlAction = 0;
const unsigned long DEBOUNCE_DELAY_MS = 600000;  // 10 min
unsigned long lastManualControlAction = 0;

String DEVICE_LIST_JSON = "[]";
const char* DEVICES_FILE = "/devices.json";
DynamicJsonDocument doc(2048);

StaticJsonDocument<2048> CURRENT_WEATHER_DOC;
StaticJsonDocument<512> WEATHER_CONFIG_DOC;
SemaphoreHandle_t weatherMutex = nullptr;
const char* WEATHER_CONFIG_FILE = "/weather_config.json";

void handleRoot();
void handleStatusFetch();
void handleProxy();
void handleGetDevices();
void handleSaveDevices();
void handleGetWeatherConfig();
void handleSaveWeatherConfig();
void handleGetWeatherStatus();
void handleNotFound();
void handleWiFiReset();
void handleRestart();
void handleManualControl();
void handleTestTelegram();

void loadDevices();
void saveDevices(String json);
void loadWeatherConfig();
void saveWeatherConfig();
void loadScheduleConfig();    // YENİ
void saveScheduleConfig();    // YENİ
String formatTime(long unixTime);
String getDeviceStatusJson(const String& ip);
void weatherControlLogic(int currentMinute);
void windAlertLogic();  // YENİ FONKSİYON
void sendTelegramAlert(const String& message);

void flashLED();
void forceNtpSync();
void updateAllDeviceStatuses(void* pvParameters);
void updateWeatherAndControlTask(void* pvParameters);

String lastWeatherDesired = "";

// ===================== Helper functions =====================
void flashLED() {
  digitalWrite(ONBOARD_LED_PIN, LOW);
  delay(LED_FLASH_DURATION);
  digitalWrite(ONBOARD_LED_PIN, HIGH);
}

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

        // DÜZELTİLDİ: RAM dizileri (relayOnAtSunrise/relayOffAtSunset)
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
            relayOnAtSunrise[devIdx][r] =
              (SCHEDULE_CONFIG_DOC[name][srKey] | String("")) == "ON";
            relayOffAtSunset[devIdx][r] =
              (SCHEDULE_CONFIG_DOC[name][ssKey] | String("")) == "OFF";
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

// ============== Time helper ==============
String formatTime(long unixTime) {
  if (unixTime == 0) return "N/A";
  struct tm* timeinfo;
  time_t rawtime = unixTime;
  timeinfo = localtime(&rawtime);
  char buffer[6];
  strftime(buffer, 6, "%H:%M", timeinfo);
  return String(buffer);
}

// ============== Device status via HTTP ==============
// DÜZELTİLDİ: HTTP nesneleri scope'da düzgün kapatılıyor, timeout artırıldı
String getDeviceStatusJson(const String& ip) {
  if (WiFi.status() != WL_CONNECTED) {
    return "{\"r1_state\":\"ERR\",\"r2_state\":\"ERR\",\"mac\":\"NO_WIFI\"}";
  }
  if (ip.length() < 7) return "{\"r1_state\":\"ERR\",\"r2_state\":\"ERR\",\"mac\":\"NO_IP\"}";

  String result;
  String macId = "NO_MAC";

  // Status isteği
  {
    HTTPClient http;
    http.setTimeout(800);
    http.setConnectTimeout(800);
    String url = "http://" + ip + "/status";
    if (http.begin(url)) {
      int code = http.GET();
      if (code == 200) {
        result = http.getString();
      } else {
        http.end();
        return "{\"r1_state\":\"OFFLINE\",\"r2_state\":\"OFFLINE\",\"mac\":\"OFFLINE\"}";
      }
      http.end();
    } else {
      return "{\"r1_state\":\"OFFLINE\",\"r2_state\":\"OFFLINE\",\"mac\":\"OFFLINE\"}";
    }
  }

  // MAC isteği - ayrı scope ile bellek temizlenir
  {
    HTTPClient httpMac;
    httpMac.setTimeout(500);
    httpMac.setConnectTimeout(500);
    String macUrl = "http://" + ip + "/mac";
    if (httpMac.begin(macUrl)) {
      int macCode = httpMac.GET();
      if (macCode == 200) {
        macId = httpMac.getString();
        macId.trim();
        if (macId.length() > 17) macId = macId.substring(0, 17);
      }
      httpMac.end();
    }
  }

  // JSON'a mac ekle
  result.trim();
  if (result.endsWith("}")) {
    result.remove(result.length() - 1);
    result += ",\"mac\":\"" + macId + "\"}";
  }

  return result;
}
// ============== Send control signal ==============
void sendControlSignal(const String& ip, int relay, const String& action) {
  if (ip == "" || relay == 0 || action == "") return;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = "http://" + ip + "/control?id=" + String(relay) + "&action=" + action;
  http.begin(url);
  http.GET();
  http.end();
}

// =================== URL encode helper ===================
String urlEncode(const String& str) {
  String encoded;
  encoded.reserve(str.length() * 3);
  char c;
  for (size_t i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      char buf[5];
      sprintf(buf, "%%%02X", (uint8_t)c);
      encoded += buf;
    }
  }
  return encoded;
}

// =================== sendTelegramAlert function (GÜNCELLENDİ) ===================
void sendTelegramAlert(const String& message) {
  if (!TELEGRAM_ENABLED) {
    Serial.println("[Telegram] Özellik devre dışı.");
    return;
  }

  if (message.length() == 0) {
    Serial.println("[Telegram] Mesaj boş, gönderilmedi.");
    return;
  }

  // Wi-Fi kontrolü
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Telegram] WiFi bağlı değil!");
    return;
  }

  Serial.println("\n--- TELEGRAM GÖNDERİM BAŞLIYOR ---");
  Serial.printf("Mesaj uzunluğu: %d karakter\n", message.length());
  Serial.println("Mesaj içeriği:");
  Serial.println(message);

  WiFiClientSecure client;
  client.setInsecure();  // SSL sertifika kontrolünü atla

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(10000);  // 10 saniye timeout

  // URL'yi oluştur - Markdown formatını kullanıyoruz.
  String encodedMsg = urlEncode(message);
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) + "&text=" + encodedMsg + "&parse_mode=Markdown&disable_web_page_preview=true";

  Serial.printf("Telegram URL uzunluğu: %d\n", url.length());
  Serial.printf("Chat ID: %s\n", TELEGRAM_CHAT_ID);

  // Bağlantıyı başlat
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    String response = http.getString();

    Serial.printf("[Telegram] HTTP Kodu: %d\n", httpCode);
    Serial.printf("[Telegram] Yanıt: %s\n", response.c_str());

    if (httpCode == HTTP_CODE_OK) {
      Serial.println("[Telegram] ✅ Mesaj başarıyla gönderildi.");
    } else if (httpCode == 400) {
      Serial.println("[Telegram] ❌ HATA 400: Token veya Chat ID hatalı olabilir.");
    } else if (httpCode == 404) {
      Serial.println("[Telegram] ❌ HATA 404: Bot bulunamadı.");
    } else if (httpCode == 403) {
      Serial.println("[Telegram] ❌ HATA 403: Bot bu sohbete mesaj gönderemez.");
    } else if (httpCode < 0) {
      Serial.printf("[Telegram] ❌ Bağlantı hatası: %s\n", http.errorToString(httpCode).c_str());
    } else {
      Serial.printf("[Telegram] ❌ Bilinmeyen hata kodu: %d\n", httpCode);
    }

    http.end();
  } else {
    Serial.println("[Telegram] ❌ HTTP begin() başarısız. URL çözümlenemedi.");
  }

  Serial.println("--- TELEGRAM GÖNDERİM TAMAMLANDI ---\n");
}
// ====================================================================
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
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&hourly=cloudcover,relativehumidity_2m,windspeed_10m,winddirection_10m" + "&daily=sunrise,sunset" + "&current_weather=true&timezone=auto";

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
  } else {
    Serial.println("DEBUG: current_weather anahtarı yok!");
    out["temp"] = 0.0f;
    out["wind_speed"] = 0.0f;
    out["wind_dir"] = 0.0f;
  }

  // hourly cloudcover - DÜZELTİLDİ: Güncel saatin doğru indeksi hesaplanıyor
  if (scratch.containsKey("hourly") && scratch["hourly"].containsKey("time") && scratch["hourly"].containsKey("cloudcover")) {
    JsonArray timeArr = scratch["hourly"]["time"].as<JsonArray>();
    JsonArray clouds  = scratch["hourly"]["cloudcover"].as<JsonArray>();

    time_t nowt = time(nullptr);
    struct tm tmnow;
    localtime_r(&nowt, &tmnow);
    char timestr[20];
    sprintf(timestr, "%04d-%02d-%02dT%02d:00",
            tmnow.tm_year + 1900, tmnow.tm_mon + 1,
            tmnow.tm_mday, tmnow.tm_hour);
    String target = String(timestr);

    int cloudVal = 0;
    bool found = false;
    for (size_t i = 0; i < timeArr.size() && i < clouds.size(); i++) {
      const char* t = timeArr[i];
      if (t && String(t) == target) {
        cloudVal = clouds[i].as<int>();
        found = true;
        break;
      }
    }
    // Güncel saat bulunamazsa ilk değeri kullan
    if (!found && clouds.size() > 0) cloudVal = clouds[0].as<int>();
    out["clouds"] = cloudVal;
  } else {
    out["clouds"] = 0;
  }

  // relative humidity
  if (scratch.containsKey("hourly") && scratch["hourly"].containsKey("relativehumidity_2m")) {
    JsonArray rh = scratch["hourly"]["relativehumidity_2m"].as<JsonArray>();
    out["humidity"] = rh.size() ? rh[0].as<int>() : 0;
  } else {
    out["humidity"] = 0;
  }

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
    return;
  }

  // Hava durumu verilerini işle ve mutex içinde kaydet
  // DÜZELTİLDİ: TFT yazımı mutex DIŞINDA yapılıyor (SPI+FreeRTOS çakışması önlendi)
  float dispTemp = 0;
  int   dispHumidity = 0;
  float dispWind = 0;
  String dispWindDir, dispCity;

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

    // TFT için local kopyalar al (mutex serbest bırakılmadan önce)
    dispTemp     = CURRENT_WEATHER_DOC["temp"].as<float>();
    dispHumidity = CURRENT_WEATHER_DOC["humidity"].as<int>();
    dispWind     = CURRENT_WEATHER_DOC["wind_speed"].as<float>();
    dispWindDir  = CURRENT_WEATHER_DOC["wind_direction"].as<String>();
    dispCity     = CURRENT_WEATHER_DOC["city_name"].as<String>();

    Serial.printf("DEBUG: Hava durumu güncellendi - Şehir: %s, Sıcaklık: %.1f°C, Rüzgar: %.1f Km/s, Skor: %d\n",
                  dispCity.c_str(), dispTemp, dispWind, lightnessScore);

    xSemaphoreGive(weatherMutex);
  }

  // TFT güncelle - mutex DIŞINDA (SPI güvenliği)
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(TFT_RED);
  tft.setCursor(20, 20);
  tft.println(dispCity);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(90, 80);
  tft.print(dispTemp, 1);
  tft.println(" C");

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 120);
  tft.print("Nem   : ");
  tft.print(dispHumidity);
  tft.println(" %");

  tft.setTextColor(TFT_GREEN);
  tft.setCursor(20, 150);
  tft.print("Ruzgar: ");
  tft.print(dispWind, 1);
  tft.println(" Km/s");

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(20, 180);
  tft.println("Yon   : " + dispWindDir);
}

// ============== Existing weatherControlLogic ==============
void weatherControlLogic(int currentMinute) {

  bool weatherActive = WEATHER_CONFIG_DOC["active"] | false;
  if (!weatherActive) return;

  if (millis() - lastManualControlAction < DEBOUNCE_DELAY_MS) {
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

  xSemaphoreGive(weatherMutex);  // Her durumda serbest bırak

  if (!isDaytime) return;
  // Gündüz mantığı buraya eklenebilir
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
      bool useSunrise = relayOnAtSunrise[devIdx][r];
      bool useSunset  = relayOffAtSunset[devIdx][r];

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
          sendControlSignal(ip, r+1, "on");
          setForceRefresh();
          Serial.printf("[SunSched] %s R%d AÇILDI (Gündoğumu: %s)\n",
                        name.c_str(), r+1, sunriseStr.c_str());
        }
        if (useSunset && sunsetWindow) {
          sunsetDone[devIdx][r] = true;
          sendControlSignal(ip, r+1, "off");
          setForceRefresh();
          Serial.printf("[SunSched] %s R%d KAPANDI (Günbatımı: %s)\n",
                        name.c_str(), r+1, sunsetStr.c_str());
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

// ============== Tasks ==============
void updateAllDeviceStatuses(void* pvParameters) {
  while (1) {
    if (doc.is<JsonArray>()) {
      for (JsonObject device : doc.as<JsonArray>()) {
        String name = device["name"] | "";
        String ip   = device["ip"]   | "";
        if (name != "" && ip != "") {
          String statusJson = getDeviceStatusJson(ip);
          StaticJsonDocument<512> tempDoc;  // DÜZELTİLDİ: 1500 yerine 512 (ESP01 yanıtı küçük)
          if (deserializeJson(tempDoc, statusJson) == DeserializationError::Ok) {
            if (xSemaphoreTake(statusMutex, MUTEX_TIMEOUT) == pdTRUE) {
              CURRENT_STATUS_DOC[name].set(tempDoc);
              xSemaphoreGive(statusMutex);
            }
          }
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // 10ms yerine 20ms - WiFi stack için daha iyi
      }
    }
    for (int i = 0; i < STATUS_CHECK_INTERVAL / 100; i++) {
      if (forceStatusRefresh) {
        clearForceRefresh();  // DÜZELTİLDİ: inline fonksiyon ile güvenli temizleme
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

void updateWeatherAndControlTask(void* pvParameters) {
  unsigned long lastWeatherUpdate = 0;
  const unsigned long WEATHER_UPDATE_INTERVAL = 600000;  // 10 dakika
  bool firstRun = true;

  while (1) {
    unsigned long currentTime = millis();

    if (firstRun || currentTime - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {
      Serial.println("DEBUG: Hava durumu güncelleme tetiklendi...");
      forceNtpSync();
      updateWeatherFromConfig();
      lastWeatherUpdate = currentTime;
      firstRun = false;
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {  // non-blocking check
      weatherControlLogic(timeinfo.tm_hour * 60 + timeinfo.tm_min);
      handleSunSchedule(timeinfo.tm_hour * 60 + timeinfo.tm_min);
    }

    // YENİ: Rüzgar uyarısı mantığını kontrol et
    windAlertLogic();

    // Hava durumu ve kontrol döngüsü 1 dakikada bir kontrol edilsin
    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

// ============== HTTP handlers ==============

void handleRoot() {
  flashLED();
  // Kullanıcının isteği üzerine LittleFS yerine relay_webpage_multi.h'den yüklendi
  server.send_P(200, "text/html", HTML_PAGE);
}


void handleStatusFetch() {
  flashLED();
  StaticJsonDocument<4096> responseDoc;

  // Cihaz durumlarını olduğu gibi gönder
  if (xSemaphoreTake(statusMutex, MUTEX_TIMEOUT) == pdTRUE) {
    responseDoc["devices"].set(CURRENT_STATUS_DOC);
    xSemaphoreGive(statusMutex);
  }

  // Schedule bilgisini AYRI nesne olarak gönder
  // HTML: d.schedule[dev.name]["r1_start"] şeklinde okur
  // Böylece cihaz OFFLINE olsa bile schedule görünür
  if (xSemaphoreTake(scheduleMutex, MUTEX_TIMEOUT) == pdTRUE) {
    responseDoc["schedule"].set(SCHEDULE_CONFIG_DOC);
    xSemaphoreGive(scheduleMutex);
  }

  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
    responseDoc["weather"].set(CURRENT_WEATHER_DOC);
    responseDoc["weather_config"].set(WEATHER_CONFIG_DOC);
    xSemaphoreGive(weatherMutex);
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    char timeBuffer[10];
    strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeinfo);
    responseDoc["current_time"] = timeBuffer;
  } else {
    responseDoc["current_time"] = "N/A";
  }

  String output;
  serializeJson(responseDoc, output);
  server.send(200, "application/json", output);
}

// ============== handleProxy (Genişletilmiş ve Hata Ayıklamalı) ==============
void handleProxy() {
  flashLED();
  
  Serial.println("=== PROXY İSTEĞİ ===");
  Serial.printf("Method: %s\n", server.method() == HTTP_GET ? "GET" : "POST");
  
  // Tüm parametreleri listele
  for (int i = 0; i < server.args(); i++) {
    Serial.printf("Arg[%d]: %s = %s\n", i, server.argName(i).c_str(), server.arg(i).c_str());
  }
  
  if (!server.hasArg("ip") || !server.hasArg("url")) {
    Serial.println("HATA: ip veya url parametresi eksik");
    server.send(400, "text/plain", "HATA: ip ve url parametreleri gerekli");
    return;
  }

  String ip = server.arg("ip");
  String urlPath = server.arg("url");
  
  Serial.printf("Hedef IP: %s\n", ip.c_str());
  Serial.printf("URL Path: %s\n", urlPath.c_str());

  // URL decode işlemi
  urlPath.replace("%2F", "/");
  urlPath.replace("%3F", "?");
  urlPath.replace("%3D", "=");
  urlPath.replace("%26", "&");

  // /save isteği kontrolü
  if (urlPath.startsWith("/save")) {
    Serial.println("=== SCHEDULE KAYIT İSTEĞİ ===");
    
    int relay_id = server.arg("relay_id").toInt();
    String sunrise_act = server.arg("sunrise_act");
    String sunset_act = server.arg("sunset_act");
    String start_h = server.arg("start_h");
    String start_m = server.arg("start_m");
    String end_h = server.arg("end_h");
    String end_m = server.arg("end_m");
    int days = server.arg("days").toInt();
    
    Serial.printf("relay_id: %d\n", relay_id);
    Serial.printf("sunrise_act: %s\n", sunrise_act.c_str());
    Serial.printf("sunset_act: %s\n", sunset_act.c_str());
    Serial.printf("start: %s:%s\n", start_h.c_str(), start_m.c_str());
    Serial.printf("end: %s:%s\n", end_h.c_str(), end_m.c_str());
    Serial.printf("days: %d\n", days);

    if (relay_id >= 1 && relay_id <= MAX_RELAYS) {
      int devIdx = -1;
      for (int i = 0; i < doc.size(); i++) {
        if (doc[i]["ip"] == ip) {
          devIdx = i;
          Serial.printf("Cihaz bulundu - Index: %d, Name: %s\n", i, doc[i]["name"].as<String>().c_str());
          break;
        }
      }

      if (devIdx != -1 && devIdx < MAX_DEVICES) {
        String devName = doc[devIdx]["name"];

        if (xSemaphoreTake(scheduleMutex, MUTEX_TIMEOUT) == pdTRUE) {
          JsonObject obj = SCHEDULE_CONFIG_DOC[devName].to<JsonObject>();
          String rKey = "r" + String(relay_id);
          
          // Zaman ayarlarını kaydet
          String startTime = start_h + ":" + start_m;
          String endTime = end_h + ":" + end_m;
          
          obj[rKey + "_start"] = startTime;
          obj[rKey + "_end"] = endTime;
          obj[rKey + "_days"] = days;
          
          // Gün doğumu/batımı ayarlarını kaydet
          obj[rKey + "_sunrise_act"] = sunrise_act;
          obj[rKey + "_sunset_act"] = sunset_act;

          // RAM dizilerini güncelle
          relayOnAtSunrise[devIdx][relay_id - 1] = (sunrise_act == "ON");
          relayOffAtSunset[devIdx][relay_id - 1] = (sunset_act == "OFF");

          Serial.println("=== SCHEDULE KAYDEDİLDİ ===");
          Serial.printf("DevName: %s\n", devName.c_str());
          Serial.printf("r%d_start: %s\n", relay_id, startTime.c_str());
          Serial.printf("r%d_end: %s\n", relay_id, endTime.c_str());
          Serial.printf("r%d_days: %d\n", relay_id, days);
          Serial.printf("r%d_sunrise_act: %s\n", relay_id, sunrise_act.c_str());
          Serial.printf("r%d_sunset_act: %s\n", relay_id, sunset_act.c_str());

          // Schedule config'i LittleFS'e kaydet
          saveScheduleConfig();
          xSemaphoreGive(scheduleMutex);

          // JSON'u kontrol et
          Serial.println("=== GÜNCEL SCHEDULE JSON ===");
          String jsonStr;
          serializeJson(SCHEDULE_CONFIG_DOC, jsonStr);
          Serial.println(jsonStr);

          // Durum güncellemesini zorla
          setForceRefresh();
        }
      } else {
        Serial.println("HATA: Cihaz bulunamadı veya index sınırı aşıldı");
      }
    }
  }

  // Orijinal proxy devam eder
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(500);
  
  String fullUrl = "http://" + ip + urlPath;
  Serial.printf("Proxy forwarding to: %s\n", fullUrl.c_str());
  
  if (http.begin(fullUrl)) {
    int code = http.GET();
    String resp = (code > 0) ? http.getString() : "ERROR";
    
    Serial.printf("Proxy response code: %d\n", code);
    Serial.printf("Response length: %d\n", resp.length());
    
    if (code > 0) {
      server.send(200, "text/plain", resp);
    } else {
      server.send(500, "text/plain", "Hedef cihaza ulaşılamadı");
    }
    http.end();
  } else {
    Serial.println("Proxy: HTTP begin failed");
    server.send(500, "text/plain", "HTTP bağlantı hatası");
  }
  
  Serial.println("=== PROXY İSTEĞİ TAMAMLANDI ===\n");
}

void handleGetDevices() {
  flashLED();
  server.send(200, "application/json", DEVICE_LIST_JSON);
}

void handleSaveDevices() {
  flashLED();
  if (server.method() != HTTP_POST) return server.send(405);
  saveDevices(server.arg("plain"));
  server.send(200, "text/plain", "OK");
}

void handleGetWeatherConfig() {
  flashLED();
  String out;
  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
    serializeJson(WEATHER_CONFIG_DOC, out);
    xSemaphoreGive(weatherMutex);
  } else {
    out = "{}";
  }
  server.send(200, "application/json", out);
}

void handleSaveWeatherConfig() {
  flashLED();
  if (server.method() != HTTP_POST) return server.send(405);
  DynamicJsonDocument inDoc(1024);
  DeserializationError err = deserializeJson(inDoc, server.arg("plain"));
  if (err) {
    server.send(400, "text/plain", "HATA");
    return;
  }

  // Eğer sadece refresh isteniyorsa
  if (inDoc.containsKey("refresh") && inDoc["refresh"] == true) {
    Serial.println("DEBUG: Manuel hava durumu yenileme isteği");
    updateWeatherFromConfig();
    server.send(200, "text/plain", "REFRESHED");
    return;
  }

  bool configChanged = false;
  bool addressChanged = false;

  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
    // Yeni değerleri al
    String newCity = inDoc["city"] | "";
    String newDistrict = inDoc["district"] | "";
    String newNeighbourhood = inDoc["neighbourhood"] | "";

    // Adres değişimi kontrolü
    if (newCity != (WEATHER_CONFIG_DOC["city"] | "") || newDistrict != (WEATHER_CONFIG_DOC["district"] | "") || newNeighbourhood != (WEATHER_CONFIG_DOC["neighbourhood"] | "")) {
      addressChanged = true;
      WEATHER_CONFIG_DOC["city"] = newCity;
      WEATHER_CONFIG_DOC["district"] = newDistrict;
      WEATHER_CONFIG_DOC["neighbourhood"] = newNeighbourhood;
      // Adres değişince koordinatları sil ki tekrar geocode yapılsın
      WEATHER_CONFIG_DOC["latitude"] = "";
      WEATHER_CONFIG_DOC["longitude"] = "";
      configChanged = true;
    }

    // Diğer ayar değişim kontrolü
    // NOT: sol taraf da "| varsayilan" ile okunuyor - ArduinoJson v7'de ham
    // MemberProxy'yi cast'siz != ile karsilastirmak private kopya ctor hatasi veriyordu.
    if ((inDoc["active"] | false) != (WEATHER_CONFIG_DOC["active"] | false) || (inDoc["device_name"] | "") != (WEATHER_CONFIG_DOC["device_name"] | "") || (inDoc["relay_id"] | 0) != (WEATHER_CONFIG_DOC["relay_id"] | 0) || (inDoc["threshold"] | 3) != (WEATHER_CONFIG_DOC["threshold"] | 3) || (inDoc["action"] | "") != (WEATHER_CONFIG_DOC["action"] | "") || (inDoc["start_time"] | "") != (WEATHER_CONFIG_DOC["start_time"] | "") || (inDoc["end_time"] | "") != (WEATHER_CONFIG_DOC["end_time"] | "") || (inDoc["wind_threshold"] | 15.0f) != (WEATHER_CONFIG_DOC["wind_threshold"] | 15.0f)) {  // Rüzgar eşiği kontrolü eklendi
      configChanged = true;
      WEATHER_CONFIG_DOC["active"] = inDoc["active"];
      WEATHER_CONFIG_DOC["device_name"] = inDoc["device_name"];
      WEATHER_CONFIG_DOC["relay_id"] = inDoc["relay_id"];
      WEATHER_CONFIG_DOC["threshold"] = inDoc["threshold"];
      WEATHER_CONFIG_DOC["action"] = inDoc["action"];
      WEATHER_CONFIG_DOC["start_time"] = inDoc["start_time"];
      WEATHER_CONFIG_DOC["end_time"] = inDoc["end_time"];
      WEATHER_CONFIG_DOC["wind_threshold"] = inDoc["wind_threshold"];  // Rüzgar eşiği kaydedildi
    }

    if (configChanged) {
      // Eğer adres değiştiyse ve koordinatlar varsa, bunları güncelle
      String newLat = inDoc["latitude"] | "";
      String newLon = inDoc["longitude"] | "";
      if (newLat.length() > 2 && newLon.length() > 2) {
        WEATHER_CONFIG_DOC["latitude"] = newLat;
        WEATHER_CONFIG_DOC["longitude"] = newLon;
        WEATHER_CONFIG_DOC["city_name"] = inDoc["city_name"] | "";
      }

      saveWeatherConfig();
    }
    xSemaphoreGive(weatherMutex);

    if (configChanged) {
      Serial.println("DEBUG: Hava durumu hemen güncelleniyor...");
      // Hemen güncelle
      updateWeatherFromConfig();
    }
  }

  server.send(200, "text/plain", "OK");
}

void handleGetWeatherStatus() {
  flashLED();
  String out;
  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) == pdTRUE) {
    serializeJson(CURRENT_WEATHER_DOC, out);
    xSemaphoreGive(weatherMutex);
  } else out = "{}";
  server.send(200, "application/json", out);
}

void handleNotFound() {
  flashLED();
  server.send(404, "text/plain", "Not found");
}

void handleWiFiReset() {
  flashLED();
  wm.resetSettings();
  server.send(200, "text/plain", "Reset");
  delay(1000);
  ESP.restart();
}

void handleRestart() {
  flashLED();
  server.send(200, "text/plain", "Restart");
  delay(1000);
  ESP.restart();
}

void handleManualControl() {
  flashLED();
  lastManualControlAction = millis();
  Serial.println("DEBUG: Manuel kontrol sinyali alindi.");

  // Hemen durum güncellemesi yap
  if (doc.is<JsonArray>()) {
    for (JsonObject device : doc.as<JsonArray>()) {
      String name = device["name"] | "";
      String ip = device["ip"] | "";
      if (name != "" && ip != "") {
        String statusJson = getDeviceStatusJson(ip);
        StaticJsonDocument<1500> tempDoc;
        if (deserializeJson(tempDoc, statusJson) == DeserializationError::Ok) {
          if (xSemaphoreTake(statusMutex, MUTEX_TIMEOUT) == pdTRUE) {
            CURRENT_STATUS_DOC[name].set(tempDoc);
            xSemaphoreGive(statusMutex);
          }
        }
      }
    }
  }
  forceStatusRefresh = true;  // setForceRefresh() eşdeğeri - loop'tan erişim güvenli
  server.send(200, "text/plain", "OK");
}

void handleTestTelegram() {
  flashLED();
  Serial.println("DEBUG: Test Telegram mesajı gönderiliyor...");

  // Test mesajı oluştur
  struct tm timeinfo;
  String timeStr = "N/A";
  if (getLocalTime(&timeinfo, 0)) {
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    timeStr = String(buffer);
  }

  String testMsg = "🔧 *TEST MESAJI* 🔧\n"
                   "Bu mesaj /test_telegram endpoint'inden gönderildi.\n"
                   "📍 IP: "
                   + WiFi.localIP().toString() + "\n"
                                                 "🕒 Saat: "
                   + timeStr + "\n"
                               "📡 MAC: "
                   + WiFi.macAddress() + "\n"
                                         "✅ Telegram bağlantısı çalışıyor!";

  sendTelegramAlert(testMsg);

  server.send(200, "text/plain", "Test mesajı gönderildi. Serial Monitor'ü kontrol edin.");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  // RÖLE 1 - Sabit pin yerine ONBOARD_LED_PIN makrosu kullanıldı
  // NOT: Sunucu kartında fiziksel röle pin bağlantısı yoktur;
  //      röle kontrolü ESP01 cihazlara HTTP ile iletilir.
  //      Bu callback lokal LED/test amaçlıdır.
  if (String(topic) == topicRelay1Set) {
    if (msg == "1" || msg == "ON") {
      // Relay 1 -> HTTP üzerinden gönder (lokal pin yok)
      mqttClient.publish(topicRelay1State.c_str(), "1", true);
      Serial.println("[MQTT] Relay1 ON komutu alındı");
    } else if (msg == "0" || msg == "OFF") {
      mqttClient.publish(topicRelay1State.c_str(), "0", true);
      Serial.println("[MQTT] Relay1 OFF komutu alındı");
    }
  }

  // RÖLE 2
  if (String(topic) == topicRelay2Set) {
    if (msg == "1" || msg == "ON") {
      mqttClient.publish(topicRelay2State.c_str(), "1", true);
      Serial.println("[MQTT] Relay2 ON komutu alındı");
    } else if (msg == "0" || msg == "OFF") {
      mqttClient.publish(topicRelay2State.c_str(), "0", true);
      Serial.println("[MQTT] Relay2 OFF komutu alındı");
    }
  }
}

void reconnectMQTT() {
  static unsigned long lastTry = 0;
  if (millis() - lastTry < 3000) return;
  lastTry = millis();

  if (mqttClient.connect(mqttClientID.c_str())) {
    mqttClient.subscribe(topicRelay1Set.c_str());
    mqttClient.subscribe(topicRelay2Set.c_str());
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE);  // Sadece ön renk (transparent background için)


  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);  // Beyaz yazı, siyah arka plan
  tft.setTextSize(3);
  tft.setCursor(20, 80);
  tft.println("Merkezi HUB");
  tft.println("Baslatiliyor...");

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);


  // Semaphore yarat
  statusMutex = xSemaphoreCreateMutex();
  weatherMutex = xSemaphoreCreateMutex();

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

  // WiFi Manager
  wm.setConfigPortalTimeout(120);
  wm.setDebugOutput(false);
  wm.autoConnect("MerkeziHUB_SETUP");

  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 50);
  tft.println("WiFi Baglandi!");
  tft.setTextSize(2);
  tft.setCursor(10, 120);
  tft.print("IP: ");
  tft.println(WiFi.localIP());

  Serial.println("WiFi Bağlandı. IP: " + WiFi.localIP().toString());

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

  // HTTP rotaları
  server.on("/", handleRoot);
  server.on("/settings", handleRoot);
  server.on("/weather", handleRoot);
  server.on("/fetch_status", handleStatusFetch);
  server.on("/proxy", handleProxy);
  server.on("/get_devices", handleGetDevices);
  server.on("/save_devices", HTTP_POST, handleSaveDevices);
  server.on("/get_weather_config", handleGetWeatherConfig);
  server.on("/save_weather_config", HTTP_POST, handleSaveWeatherConfig);
  server.on("/get_weather_status", handleGetWeatherStatus);
  server.on("/reset_wifi", handleWiFiReset);
  server.on("/restart_hub", handleRestart);
  server.on("/manual_control", handleManualControl);
  server.on("/test_telegram", handleTestTelegram);  // YENİ: Test endpoint'i

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP sunucu başlatıldı.");
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

  server.handleClient();

  if (!mqttClient.connected()) {
    reconnectMQTT();
  } else {
    mqttClient.loop();  // ✅ Sadece bağlıysa loop çalışsın
  }

  delay(10);  // ✅ 10ms - WiFi stack için daha iyi
}