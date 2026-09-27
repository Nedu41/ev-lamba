#pragma once
#include <Arduino.h>

// Acilis ekranlari - sadece setup() icinde, displayTask baslamadan once
void displayBoot();
void displayWifiConnected();

// Bu cagridan sonra TFT'ye SADECE displayTask erisir (tek SPI sahibi)
void startDisplayTask();

// AA dunya RSS -> NEWS_TICKER_TEXT (blocking HTTPS, hava gorevinden cagrilir)
bool fetchNews();
