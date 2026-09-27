#include "devices.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "app_state.h"
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
bool sendControlSignal(const String& ip, int relay, const String& action) {
  if (ip == "" || relay == 0 || action == "") return false;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = "http://" + ip + "/control?id=" + String(relay) + "&action=" + action;
  http.begin(url);
  int code = http.GET();
  http.end();
  return code == 200;
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
