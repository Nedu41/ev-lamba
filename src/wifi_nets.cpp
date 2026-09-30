#include "wifi_nets.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

#define WIFI_NETS_FILE "/wifi_nets.json"
static const int WIFI_NETS_MAX = 6;
static const uint32_t WIFI_TRY_MS = 10000;  // ag basina deneme suresi

static JsonDocument loadNets() {
  JsonDocument d;
  File f = LittleFS.open(WIFI_NETS_FILE, "r");
  if (!f || deserializeJson(d, f) || !d.is<JsonArray>()) d.to<JsonArray>();
  if (f) f.close();
  return d;
}

static bool saveNets(JsonDocument& d) {
  File f = LittleFS.open(WIFI_NETS_FILE, "w");
  if (!f) return false;
  serializeJson(d, f);
  f.close();
  return true;
}

bool wifiNetsBoot() {
  WiFi.mode(WIFI_STA);
  // WiFiManager'in NVS'ye kaydettigi ev agi (acilista surucu config'ine yuklenir)
  wifi_config_t conf{};
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  String nvsSsid = String((const char*)conf.sta.ssid);
  String nvsPass = String((const char*)conf.sta.password);

  JsonDocument d = loadNets();
  JsonArray a = d.as<JsonArray>();
  if (a.size() == 0 && nvsSsid.length()) {
    JsonObject n = a.add<JsonObject>();
    n["ssid"] = nvsSsid;
    n["pass"] = nvsPass;
    saveNets(d);
    Serial.printf("[WiFi] Ev agi listeye alindi: %s\n", nvsSsid.c_str());
  }

  // Denemeler NVS'ye YAZILMASIN: yanlis sifreli bir ag eklenirse WiFiManager'in
  // ev agi bilgisi ezilir, HUB USB'siz geri getirilemezdi.
  WiFi.persistent(false);
  bool ok = false;
  for (JsonObject n : a) {
    const char* ssid = n["ssid"] | "";
    if (!*ssid) continue;
    Serial.printf("[WiFi] Kayitli ag deneniyor: %s\n", ssid);
    WiFi.begin(ssid, n["pass"] | "");
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TRY_MS) delay(200);
    if (WiFi.status() == WL_CONNECTED) { ok = true; break; }
    WiFi.disconnect();
    delay(100);
  }
  // Hicbiri tutmadiysa surucu config'ini NVS'deki ev agina geri al - arkadan
  // gelen WiFiManager.autoConnect() bugunku gibi onunla denesin
  if (!ok && nvsSsid.length()) WiFi.begin(nvsSsid.c_str(), nvsPass.c_str());
  WiFi.persistent(true);
  return ok;
}

String wifiNetsInfoJson() {
  JsonDocument out;
  bool conn = WiFi.status() == WL_CONNECTED;
  out["ssid"] = conn ? WiFi.SSID() : "";
  out["rssi"] = conn ? WiFi.RSSI() : 0;
  out["ip"] = conn ? WiFi.localIP().toString() : "";
  JsonArray saved = out["saved"].to<JsonArray>();
  JsonDocument d = loadNets();
  int i = 0;
  for (JsonObject n : d.as<JsonArray>()) {
    JsonObject o = saved.add<JsonObject>();
    o["idx"] = i++;
    o["ssid"] = n["ssid"] | "";
  }
  String s;
  serializeJson(out, s);
  return s;
}

bool wifiNetsAdd(const String& ssid, const String& pass) {
  if (!ssid.length() || ssid.length() > 32 || pass.length() > 63) return false;
  JsonDocument d = loadNets();
  JsonArray a = d.as<JsonArray>();
  for (JsonObject n : a) {
    if (ssid == (n["ssid"] | "")) {
      n["pass"] = pass;
      return saveNets(d);
    }
  }
  if ((int)a.size() >= WIFI_NETS_MAX) return false;
  JsonObject n = a.add<JsonObject>();
  n["ssid"] = ssid;
  n["pass"] = pass;
  return saveNets(d);
}

bool wifiNetsDelete(int idx) {
  JsonDocument d = loadNets();
  JsonArray a = d.as<JsonArray>();
  if (idx < 0 || idx >= (int)a.size()) return false;
  a.remove(idx);
  return saveNets(d);
}

void wifiNetsClear() {
  LittleFS.remove(WIFI_NETS_FILE);
}

bool wifiNetsMakePreferred(int idx) {
  JsonDocument d = loadNets();
  JsonArray a = d.as<JsonArray>();
  if (idx < 0 || idx >= (int)a.size()) return false;
  JsonDocument out;
  JsonArray b = out.to<JsonArray>();
  b.add(a[idx]);
  for (int i = 0; i < (int)a.size(); i++)
    if (i != idx) b.add(a[i]);
  return saveNets(out);
}
