#pragma once
#include <Arduino.h>

// AsyncWebServer rotalari + WebSocket (/ws) + ESP01 proxy worker gorevi
void setupWebServer();

// Periyodik isler (WS istemci temizligi, non-blocking LED) - loop()'tan cagrilir
void webLoop();

// Guncel durumu bagli tum WS istemcilerine push eder
void wsBroadcastStatus();
