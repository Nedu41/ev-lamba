#pragma once
#include <Arduino.h>

// ESP01 dugumleriyle HTTP iletisimi
String getDeviceStatusJson(const String& ip);
bool sendControlSignal(const String& ip, int relay, const String& action);

// MAC'i "bcff4d403e0f" bicimine getirir; gecersizse ""
String normMac(const String& raw);

// FreeRTOS gorevi: tum cihazlarin durumunu periyodik okur, WS'ye push eder
void updateAllDeviceStatuses(void* pvParameters);
