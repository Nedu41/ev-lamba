#include "devices.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "app_state.h"
#include "storage.h"
#include "web_server.h"

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
// DEGISTI: WS control_result mesaji icin basari/hata bilgisi donduruluyor
bool sendControlSignal(const String& ip, int relay, const String& action, const String& src) {
  if (ip == "" || relay == 0 || action == "") return false;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  // BUGFIX: connect timeout verilmezse HTTPClient varsayilani 5sn - cevrimdisi
  // cihaza komut 5sn bekliyor, tarayici 3sn'de "zaman asimi" diyordu; otomasyon
  // ve komut kuyrugu da her cevrimdisi cihaz icin 5sn kilitleniyordu.
  http.setConnectTimeout(800);
  String url = "http://" + ip + "/control?id=" + String(relay) + "&action=" + action + "&src=" + src;
  http.begin(url);
  int code = http.GET();
  http.end();
  return code == 200;
}

// "BC:FF:4D:40:3E:0F" / "bcff4d403e0f" -> "bcff4d403e0f"; gecersizse ""
String normMac(const String& raw) {
  String m = raw;
  m.trim();
  m.toLowerCase();
  m.replace(":", "");
  if (m.length() != 12) return "";
  for (size_t i = 0; i < m.length(); i++) {
    if (!isxdigit(m[i])) return "";
  }
  return m;
}

// Cihaz listesini (doc) diske yazar - saveDevices tek yazma yolu kalsin
static void persistDevices() {
  String json;
  serializeJson(doc, json);
  saveDevices(json);
}

// WLED gibi: IP'si degisen (DHCP) cihazi mDNS'te MAC'iyle bulup kaydini gunceller.
// ESP01'ler MAC'i mDNS TXT kaydinda yayinliyor - aday basina HTTP gerekmiyor.
static bool followIpChanges() {
  int n = MDNS.queryService("http", "tcp");  // blocking ~1-3sn, bu gorevde sorun degil
  bool changed = false;
  for (int i = 0; i < n; i++) {
    String mac = MDNS.hasTxt(i, "mac") ? normMac(MDNS.txt(i, "mac")) : "";
    if (!mac.length()) continue;
    String newIp = MDNS.IP(i).toString();
    for (JsonObject device : doc.as<JsonArray>()) {
      if ((device["mac"] | String("")) == mac && (device["ip"] | String("")) != newIp) {
        Serial.printf("[Cihaz] %s IP degisti: %s -> %s (MAC %s)\n",
                      device["name"] | "", device["ip"] | "", newIp.c_str(), mac.c_str());
        device["ip"] = newIp;
        changed = true;
      }
    }
  }
  return changed;
}

// ============== Tasks ==============
void updateAllDeviceStatuses(void* pvParameters) {
  unsigned long lastFollow = 0;
  const unsigned long FOLLOW_INTERVAL = 60000;  // cevrimdisi cihaz varken en fazla dakikada 1 tarama

  while (1) {
    bool anyOffline = false;
    bool listChanged = false;
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
            if (strcmp(tempDoc["r1_state"] | "", "OFFLINE") == 0) {
              anyOffline = true;
            } else {
              // MAC ogrenme: IP degisirse cihazi bununla bulacagiz
              String mac = normMac(tempDoc["mac"] | "");
              if (mac.length() && (device["mac"] | String("")) != mac) {
                device["mac"] = mac;
                listChanged = true;
              }
            }
          }
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // 10ms yerine 20ms - WiFi stack için daha iyi
      }

      if (anyOffline && (lastFollow == 0 || millis() - lastFollow > FOLLOW_INTERVAL)) {
        lastFollow = millis();
        if (followIpChanges()) {
          listChanged = true;
          setForceRefresh();  // yeni IP'yi hemen yokla
        }
      }
      if (listChanged) persistDevices();
    }
    // YENİ: durum güncellendi - bağlı WS istemcilerine anında push et
    // (10s'lik normal tur VE setForceRefresh() ile zorlanan anlık yenileme
    // ikisi de buraya düşer, ayrı bir "push now" mekanizması gerekmiyor)
    wsBroadcastStatus();
    for (int i = 0; i < STATUS_CHECK_INTERVAL / 100; i++) {
      if (forceStatusRefresh) {
        clearForceRefresh();  // DÜZELTİLDİ: inline fonksiyon ile güvenli temizleme
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}
