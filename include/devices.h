#pragma once
#include <Arduino.h>

// ESP01 dugumleriyle HTTP iletisimi
String getDeviceStatusJson(const String& ip);
// src: olay kaydi icin kaynak (gunbatimi, gundogumu, bulut, bulut-geri, web);
// ESP01 v2.5+ bunu kendi MQTT olayina da yazar
bool sendControlSignal(const String& ip, int relay, const String& action, const String& src);

// MAC'i "bcff4d403e0f" bicimine getirir; gecersizse ""
String normMac(const String& raw);

// FreeRTOS gorevi: tum cihazlarin durumunu periyodik okur, WS'ye push eder
void updateAllDeviceStatuses(void* pvParameters);
