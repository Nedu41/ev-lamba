#pragma once
#include <Arduino.h>

// Web arayuzu basliginda ve TFT ust seridinde gosterilir - tek kaynak burasi
#define FW_VERSION "6.4.1"

// ---- Donanim ----
#define BACKLIGHT_PIN 21
// GPIO2: kart uzerindeki LED ile TFT_DC ayni hat - LED olarak KULLANILMAZ

// ---- Ozellikler ----
#define TELEGRAM_ENABLED true

// ---- Zamanlama ----
#define MUTEX_TIMEOUT pdMS_TO_TICKS(100)
const int STATUS_CHECK_INTERVAL = 10000;           // cihaz durum turu
const int HTTP_TIMEOUT_MS = 1000;                  // ESP01 kontrol istegi
const uint16_t STATUS_PUSH_PORT = 4211;             // ESP01 v2.8 UDP durum push (firmware ile ayni)
const unsigned long DEBOUNCE_DELAY_MS = 600000;    // manuel kontrolden sonra otomasyon beklemesi
const unsigned long NEWS_UPDATE_INTERVAL = 900000; // 15 dk
// Anadolu Ajansi dunya haberleri: ~15-22KB, yonlendirme yok, duz <title> (2026-09-27 dogrulandi)
#define NEWS_RSS_URL "https://www.aa.com.tr/tr/rss/default?cat=dunya"
const int NEWS_MAX_ITEMS = 6;

// ---- MQTT ----
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883

// ---- Cihaz/role limitleri ----
#define MAX_DEVICES 10
#define MAX_RELAYS 2

// ---- LittleFS dosyalari ----
#define DEVICES_FILE "/devices.json"
#define SCHEDULE_FILE "/devices_schedule.json"
#define WEATHER_CONFIG_FILE "/weather_config.json"
#define NOTES_FILE "/notes.json"

