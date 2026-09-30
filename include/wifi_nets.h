#pragma once
#include <Arduino.h>

// Kayitli WiFi aglari (SuDepoSis'teki "WiFi" bolumunun karsiligi).
// Liste LittleFS'te; 0. eleman tercih edilen ag. Acilista sirayla denenir,
// hicbiri tutmazsa WiFiManager'in NVS'deki agina (bugunku yol) dusulur.

// setup(): kayitli aglari dener; baglandiysa true. Ilk calismada NVS'deki
// (WiFiManager) ev agini listeye alir ki her zaman bir yedek olsun.
bool wifiNetsBoot();

// Web API icin JSON (sifreler asla disari verilmez)
String wifiNetsInfoJson();
bool wifiNetsAdd(const String& ssid, const String& pass);   // varsa sifreyi gunceller
bool wifiNetsDelete(int idx);
bool wifiNetsMakePreferred(int idx);                        // listenin basina alir
void wifiNetsClear();                                       // WiFi sifirlama: liste de silinir
