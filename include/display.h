#pragma once
#include <Arduino.h>

// Acilis ekranlari - sadece setup() icinde, displayTask baslamadan once
void displayBoot();
void displayWifiConnected();

// Bu cagridan sonra TFT'ye SADECE displayTask erisir (tek SPI sahibi)
void startDisplayTask();

// Canli sahnede 24 saati ~2 dakikada oynatir (tan/kizillik/gece onizleme)
void startSkyDemo();
// Tum hava sahnelerini (acik, bulut, sis, yagmur, kar, firtina) 8'er sn gosterir
void startWeatherDemo();

// AA dunya RSS -> NEWS_TICKER_TEXT (blocking HTTPS, hava gorevinden cagrilir)
bool fetchNews();
String newsStatus();  // son haber cekme sonucu (uzaktan teshis)
