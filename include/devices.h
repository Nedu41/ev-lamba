#pragma once
#include <Arduino.h>

// ESP01 dugumleriyle HTTP iletisimi
String getDeviceStatusJson(const String& ip);
bool sendControlSignal(const String& ip, int relay, const String& action);

// FreeRTOS gorevi: tum cihazlarin durumunu periyodik okur, WS'ye push eder
void updateAllDeviceStatuses(void* pvParameters);
