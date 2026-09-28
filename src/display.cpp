#include "display.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "app_state.h"

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite scene = TFT_eSprite(&tft);   // canli hava arka plani (4-bit paletli)
TFT_eSprite ticker = TFT_eSprite(&tft);  // haber kayan yazisi (tam genislik, etiketsiz)

// ============== Yerlesim (320x240, rotation 1) ==============
// y 0..29   : ust serit (konum, surum, saat)      - dogrudan tft
// y 30..213 : hava sahnesi + paneller              - scene sprite
// y 214..239: haber kayan yazisi (tam genislik)     - ticker sprite
const int HEADER_H = 30;
const int TICKER_H = 26;
const int TICKER_Y = 240 - TICKER_H;
const int TICKER_W = 320;
const float TICKER_SPEED = 60.0f;  // px/sn
const int SCENE_Y = HEADER_H;
const int SCENE_W = 320;
const int SCENE_H = TICKER_Y - HEADER_H;

// Palet: koyu zemin + camgobegi/turuncu vurgu (web arayuzuyle ayni tonlar)
#define COL_HEADER  0x18C3  // ~#1C1C1C
#define COL_CARD    0x10A2  // ~#141414
#define COL_CYAN    0x06D8  // ~#03DAC6
#define COL_ORANGE  0xFCC0  // ~#FF9800
#define COL_GRAY    0x8C51  // ~#8C8C8C
#define COL_RED     0xCB2F  // ~#CF6679

// 4-bit sahne sprite'inin palet indeksleri. 1-9 sahneye gore degisir,
// 10-15 yazi/panel icin sabit (okunabilirlik her sahnede ayni kalsin).
enum : uint8_t {
  P_BG = 0, P_SKY1, P_SKY2, P_SKY3, P_SKY4, P_CLOUD_L, P_CLOUD_D, P_ACCENT, P_PRECIP, P_FLASH,
  P_PANEL, P_WHITE, P_CYAN, P_ORANGE, P_GRAY, P_RED
};

// ============== Metin yardimcilari ==============
// GLCD/Font2/4/7 sadece ASCII cizebiliyor - Turkce karakterler bozuk kutucuk
// olarak cikiyordu. UTF-8 Turkce harfleri ve tipografik isaretleri ASCII'ye cevirir.
String trAscii(const String& in) {
  String out;
  out.reserve(in.length());
  const uint8_t* s = (const uint8_t*)in.c_str();
  size_t n = in.length();
  for (size_t i = 0; i < n; i++) {
    uint8_t c = s[i];
    if (c < 0x80) { out += (c >= 0x20) ? (char)c : ' '; continue; }
    if (c == 0xC3 && i + 1 < n) {
      switch (s[++i]) {
        case 0xA7: out += 'c'; break; case 0x87: out += 'C'; break;
        case 0xB6: out += 'o'; break; case 0x96: out += 'O'; break;
        case 0xBC: out += 'u'; break; case 0x9C: out += 'U'; break;
        case 0xA2: case 0xA1: case 0xA0: out += 'a'; break;
        case 0x82: out += 'A'; break;
        case 0xAE: case 0xAD: out += 'i'; break;
        case 0xBB: out += 'u'; break;
        case 0xA9: case 0xA8: out += 'e'; break;
      }
    } else if (c == 0xC4 && i + 1 < n) {
      switch (s[++i]) {
        case 0x9F: out += 'g'; break; case 0x9E: out += 'G'; break;
        case 0xB1: out += 'i'; break; case 0xB0: out += 'I'; break;
      }
    } else if (c == 0xC5 && i + 1 < n) {
      switch (s[++i]) {
        case 0x9F: out += 's'; break; case 0x9E: out += 'S'; break;
      }
    } else if (c == 0xE2 && i + 2 < n && s[i + 1] == 0x80) {
      uint8_t e = s[i + 2];
      i += 2;
      if (e == 0x98 || e == 0x99) out += '\'';
      else if (e == 0x9C || e == 0x9D) out += '"';
      else if (e == 0x93 || e == 0x94) out += '-';
      else if (e == 0xA6) out += "...";
    } else {
      if (c >= 0xF0) i += 3; else if (c >= 0xE0) i += 2; else if (c >= 0xC0) i += 1;
    }
  }
  return out;
}

String decodeEntities(String s) {
  s.replace("&quot;", "\"");
  s.replace("&#39;", "'");
  s.replace("&apos;", "'");
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&nbsp;", " ");
  s.replace("&amp;", "&");  // en sonda - "&amp;quot;" gibi cift kodlamayi bozmasin
  return s;
}

// "Kalburcu Mahallesi, İstanbul, ..." -> "Kalburcu"
String shortLocation(const String& full) {
  int comma = full.indexOf(',');
  String p = (comma < 0) ? full : full.substring(0, comma);
  p.replace(" Mahallesi", "");
  p.replace(" Mah.", "");
  p.trim();
  return trAscii(p);
}

String fitText(String s, int maxW, int font) {
  if (tft.textWidth(s, font) <= maxW) return s;
  while (s.length() > 1 && tft.textWidth(s + "...", font) > maxW) s.remove(s.length() - 1);
  return s + "...";
}

// Sabit genislikte buyuk deger: sigmazsa ondaligi at
String bigValue(float v, int maxW) {
  String s = String(v, 1);
  if (tft.textWidth(s, 7) > maxW) s = String((int)roundf(v));
  return s;
}

// ============== Hava verisi (displayTask'in yerel kopyasi) ==============
struct WeatherView {
  bool loaded = false, ok = false, isDay = true;
  float temp = 0, wind = 0;
  int hum = 0, clouds = 0, code = -1;
  String dir, sunrise, sunset, city;
};
static WeatherView wv;

static bool readWeather() {
  if (xSemaphoreTake(weatherMutex, MUTEX_TIMEOUT) != pdTRUE) return false;
  String desc = CURRENT_WEATHER_DOC["description"] | "";
  wv.loaded = desc.length() > 0;
  wv.ok = (desc == "OK");
  wv.temp = CURRENT_WEATHER_DOC["temp"] | 0.0f;
  wv.wind = CURRENT_WEATHER_DOC["wind_speed"] | 0.0f;
  wv.hum = CURRENT_WEATHER_DOC["humidity"] | 0;
  wv.clouds = CURRENT_WEATHER_DOC["clouds"] | 0;
  wv.code = CURRENT_WEATHER_DOC["weathercode"] | -1;
  wv.isDay = (CURRENT_WEATHER_DOC["is_day"] | 1) == 1;
  wv.dir = CURRENT_WEATHER_DOC["wind_direction"] | "";
  wv.sunrise = CURRENT_WEATHER_DOC["sunrise_time"] | "";
  wv.sunset = CURRENT_WEATHER_DOC["sunset_time"] | "";
  wv.city = CURRENT_WEATHER_DOC["city_name"] | "";
  xSemaphoreGive(weatherMutex);
  return true;
}

// WMO hava kodu -> kisa Turkce (ASCII) aciklama
static const char* weatherText(int c) {
  switch (c) {
    case 0: return "Acik";
    case 1: return "Az bulutlu";
    case 2: return "Parcali bulutlu";
    case 3: return "Kapali";
    case 45: case 48: return "Sisli";
    case 51: case 53: case 55: return "Ciseleme";
    case 56: case 57: return "Dondurucu ciseleme";
    case 61: return "Hafif yagmur";
    case 63: return "Yagmurlu";
    case 65: return "Siddetli yagmur";
    case 66: case 67: return "Dondurucu yagmur";
    case 71: return "Hafif kar";
    case 73: return "Karli";
    case 75: return "Yogun kar";
    case 77: return "Kar taneleri";
    case 80: case 81: return "Saganak";
    case 82: return "Siddetli saganak";
    case 85: case 86: return "Kar saganagi";
    case 95: return "Firtina (simsek)";
    case 96: case 99: return "Dolulu firtina";
  }
  return "SICAKLIK";
}

// ============== Ust serit ==============
void drawClock(bool force) {
  static int lastMin = -1;
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  if (!force && t.tm_min == lastMin) return;
  lastMin = t.tm_min;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  tft.fillRect(240, 0, 80, HEADER_H - 1, COL_HEADER);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, COL_HEADER);
  tft.drawString(buf, 312, 3, 4);
  tft.setTextDatum(TL_DATUM);
}

static void drawHeader() {
  tft.fillRect(0, 0, 320, HEADER_H, COL_HEADER);
  tft.drawFastHLine(0, HEADER_H - 1, 320, COL_CYAN);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_CYAN, COL_HEADER);
  tft.drawString(fitText(wv.city.length() ? shortLocation(wv.city) : "Merkezi HUB", 180, 2), 8, 7, 2);
  // Surum: kucuk ama okunur (GLCD 6x8), saatin hemen solunda
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COL_GRAY, COL_HEADER);
  tft.drawString("v" FW_VERSION, 234, 12, 1);
  tft.setTextDatum(TL_DATUM);
  drawClock(true);
}

// ============== Paneller (yazilar her zaman duz koyu zemin uzerinde) ==============
// Hem sahne sprite'ina (palet indeksleri) hem de yedek statik ekrana (565
// renkler) ayni yerlesimle cizebilmek icin renkler parametre.
struct PanelColors { uint16_t panel, white, cyan, orange, gray, red; };
static const PanelColors SPRITE_COLS = { P_PANEL, P_WHITE, P_CYAN, P_ORANGE, P_GRAY, P_RED };
static const PanelColors TFT_COLS = { COL_CARD, TFT_WHITE, COL_CYAN, COL_ORANGE, COL_GRAY, COL_RED };

static void labelValue(TFT_eSPI& g, int x, int y, const char* label, const String& value,
                       uint16_t valueCol, const PanelColors& c) {
  g.setTextColor(c.gray, c.panel);
  g.drawString(label, x, y, 2);
  g.setTextColor(valueCol, c.panel);
  g.drawString(value, x + g.textWidth(label, 2) + 6, y, 2);
}

static void drawPanels(TFT_eSPI& g, int oy, const PanelColors& c) {
  // Sicaklik (etiket yerine hava aciklamasi: "Kapali", "Yagmurlu"...)
  g.fillRoundRect(6, oy + 6, 150, 80, 7, c.panel);
  g.drawRoundRect(6, oy + 6, 150, 80, 7, c.orange);
  const char* desc = !wv.loaded ? "Bekleniyor..." : !wv.ok ? "Veri alinamadi" : weatherText(wv.code);
  g.setTextColor(wv.loaded && !wv.ok ? c.red : c.white, c.panel);
  g.drawString(fitText(desc, 136, 2), 14, oy + 10, 2);
  g.setTextColor(c.orange, c.panel);
  if (wv.ok) {
    String t = bigValue(wv.temp, 104);
    g.drawString(t, 12, oy + 30, 7);
    int w = g.textWidth(t, 7);
    g.drawCircle(12 + w + 7, oy + 34, 4, c.orange);
    g.drawCircle(12 + w + 7, oy + 34, 3, c.orange);
    g.drawString("C", 12 + w + 13, oy + 30, 4);
  } else {
    g.drawString("--", 12, oy + 30, 7);
  }

  // Ruzgar
  g.fillRoundRect(164, oy + 6, 150, 80, 7, c.panel);
  g.drawRoundRect(164, oy + 6, 150, 80, 7, c.cyan);
  g.setTextColor(c.gray, c.panel);
  g.drawString("RUZGAR", 172, oy + 10, 2);
  g.setTextColor(c.cyan, c.panel);
  if (wv.ok) {
    g.setTextDatum(TR_DATUM);
    g.setTextColor(c.white, c.panel);
    g.drawString(trAscii(wv.dir), 306, oy + 10, 2);
    g.setTextDatum(TL_DATUM);
    g.setTextColor(c.cyan, c.panel);
    String v = bigValue(wv.wind, 100);
    g.drawString(v, 170, oy + 30, 7);
    g.drawString("km/s", 170 + g.textWidth(v, 7) + 4, oy + 62, 2);
  } else {
    g.drawString("--", 170, oy + 30, 7);
  }

  // Alt bilgi
  g.fillRoundRect(6, oy + 138, 308, 42, 7, c.panel);
  g.drawRoundRect(6, oy + 138, 308, 42, 7, c.gray);
  if (wv.ok) {
    labelValue(g, 14, oy + 142, "Nem", "%" + String(wv.hum), c.white, c);
    labelValue(g, 164, oy + 142, "Bulut", "%" + String(wv.clouds), c.white, c);
    labelValue(g, 14, oy + 160, "Dogus", wv.sunrise.length() ? wv.sunrise : "--", c.orange, c);
    labelValue(g, 164, oy + 160, "Batis", wv.sunset.length() ? wv.sunset : "--", c.cyan, c);
  }
}

// ============== Canli hava sahnesi ==============
enum Scene : uint8_t { SC_NONE, SC_CLEAR, SC_PARTLY, SC_CLOUDY, SC_FOG, SC_DRIZZLE, SC_RAIN, SC_SNOW, SC_THUNDER };

static Scene sceneFromCode(int c) {
  if (c == 0) return SC_CLEAR;
  if (c == 1 || c == 2) return SC_PARTLY;
  if (c == 45 || c == 48) return SC_FOG;
  if (c >= 51 && c <= 57) return SC_DRIZZLE;
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return SC_RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return SC_SNOW;
  if (c >= 95) return SC_THUNDER;
  return SC_CLOUDY;  // 3 ve bilinmeyenler
}

static float frand(float a, float b) { return a + (b - a) * (esp_random() / 4294967295.0f); }

typedef uint8_t RGB[3];
static uint16_t skyColors[4];  // simsek parlamasindan sonra geri yuklemek icin

static uint16_t shade(const RGB c, float k) {
  return tft.color565((uint8_t)min(255.0f, c[0] * k), (uint8_t)min(255.0f, c[1] * k), (uint8_t)min(255.0f, c[2] * k));
}

static void setFixedPalette() {
  scene.setPaletteColor(P_BG, TFT_BLACK);
  scene.setPaletteColor(P_FLASH, tft.color565(255, 255, 215));
  scene.setPaletteColor(P_PANEL, tft.color565(16, 20, 28));
  scene.setPaletteColor(P_WHITE, TFT_WHITE);
  scene.setPaletteColor(P_CYAN, COL_CYAN);
  scene.setPaletteColor(P_ORANGE, COL_ORANGE);
  scene.setPaletteColor(P_GRAY, tft.color565(150, 150, 150));
  scene.setPaletteColor(P_RED, COL_RED);
}

// 0 = alacakaranlik yok, 1 = tam kizillik. Gundogumu/gunbatimi saatine (HH:MM,
// Open-Meteo'dan) ne kadar yakinsak o kadar guclu; pencere disinda soner.
static float twilightFactor() {
  if (wv.sunrise.length() != 5 || wv.sunset.length() != 5) return 0;
  struct tm t;
  if (!getLocalTime(&t, 0)) return 0;
  int nowMin = t.tm_hour * 60 + t.tm_min;
  int sunriseMin = wv.sunrise.substring(0, 2).toInt() * 60 + wv.sunrise.substring(3, 5).toInt();
  int sunsetMin  = wv.sunset.substring(0, 2).toInt() * 60 + wv.sunset.substring(3, 5).toInt();
  const int WINDOW = 50;  // dakika, gundogumu/batimindan her iki yonde
  int d = min(abs(nowMin - sunriseMin), abs(nowMin - sunsetMin));
  if (d > WINDOW) return 0;
  return 1.0f - (float)d / WINDOW;
}

static void setScenePalette(Scene sc, bool day) {
  // gok (ust->alt), acik bulut, koyu bulut, vurgu (gunes/ay/-), yagis
  static const RGB CLEAR_D[4] = {{20,70,160},{40,100,190},{70,135,215},{120,175,230}};
  static const RGB PARTLY_D[4] = {{30,80,165},{55,110,190},{85,140,210},{130,175,225}};
  static const RGB NIGHT[4] = {{4,8,26},{8,14,38},{12,22,52},{20,32,68}};
  static const RGB CLOUDY[4] = {{70,80,95},{88,98,113},{108,118,133},{128,138,152}};
  static const RGB FOG[4] = {{120,124,130},{135,139,145},{150,154,160},{165,169,175}};
  static const RGB RAIN[4] = {{38,46,58},{48,58,72},{58,70,86},{70,83,100}};
  static const RGB SNOW[4] = {{85,98,118},{105,118,138},{128,142,162},{152,166,186}};
  static const RGB STORM[4] = {{20,20,38},{28,28,50},{36,36,62},{44,44,74}};

  const RGB* sky = CLOUDY;
  RGB cl = {205,210,216}, cd = {150,156,166}, acc = {255,205,50}, pr = {150,190,235};
  auto set3 = [](RGB d, uint8_t r, uint8_t g, uint8_t b) { d[0] = r; d[1] = g; d[2] = b; };
  float k = 1.0f, kc = 1.0f;  // gece karartma (gok, bulut)

  switch (sc) {
    case SC_CLEAR:
    case SC_PARTLY:
      if (day) {
        sky = (sc == SC_CLEAR) ? CLEAR_D : PARTLY_D;
        set3(cl, 240,244,248); set3(cd, 190,200,212);
      } else {
        sky = NIGHT;
        set3(cl, 95,105,125); set3(cd, 65,72,90); set3(acc, 240,238,205);  // ay
      }
      break;
    case SC_FOG:     sky = FOG;  set3(cl, 205,208,212); set3(cd, 185,188,192); break;
    case SC_DRIZZLE:
    case SC_RAIN:    sky = RAIN; set3(cl, 125,130,140); set3(cd, 88,93,104); set3(pr, 130,175,235); break;
    case SC_SNOW:    sky = SNOW; set3(cl, 220,225,232); set3(cd, 175,182,192); set3(pr, 250,252,255); break;
    case SC_THUNDER: sky = STORM; set3(cl, 95,95,115); set3(cd, 62,62,80); set3(pr, 120,160,225); break;
    default: break;
  }
  // Gece: her havada gokyuzu siyah (ufka dogru cok hafif lacivert) - gece
  // oldugu belirgin olsun. Bulutlar siyah uzerinde gri kalir.
  static const RGB BLACK_NIGHT[4] = {{0,0,0},{0,0,3},{2,3,10},{6,8,20}};
  if (!day) {
    sky = BLACK_NIGHT;
    kc = (sc == SC_CLEAR || sc == SC_PARTLY) ? 1.0f : 0.55f;
  }
  // Kapali/sisli/ciselemede gok cismi bulutlarin arkasindan soluk gorunur:
  // gunduz solgun gunes, gece ay - gece/gunduz her havada ayirt edilsin
  if (sc != SC_CLEAR && sc != SC_PARTLY) {
    if (day) set3(acc, 235,225,170);
    else set3(acc, 225,222,195);
  }

  // Gundogumu/gunbatimi kizilligi: ufka dogru turuncu-kizil, tepede mor-lacivert.
  // Kalin bulutta sonumlu ama tamamen kaybolmaz (ufuktan hafif sizinti gercekci).
  RGB dawn[4];
  float glow = twilightFactor() * (1.0f - constrain(wv.clouds, 0, 100) / 100.0f * 0.6f);
  if (glow > 0.02f) {
    static const RGB DUSK[4] = {{45,20,68},{110,42,78},{205,78,58},{255,142,58}};  // ust->ufuk
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 3; j++) dawn[i][j] = (uint8_t)(sky[i][j] * (1 - glow) + DUSK[i][j] * glow);
    sky = dawn;
    k = max(k, 1.0f - glow * 0.3f);  // kizillik zaten koyulastiriyor, ustune tam gece karartmasi binmesin
  }

  for (int i = 0; i < 4; i++) {
    skyColors[i] = shade(sky[i], k);
    scene.setPaletteColor(P_SKY1 + i, skyColors[i]);
  }
  scene.setPaletteColor(P_CLOUD_L, shade(cl, kc));
  scene.setPaletteColor(P_CLOUD_D, shade(cd, kc));
  scene.setPaletteColor(P_ACCENT, shade(acc, 1.0f));
  scene.setPaletteColor(P_PRECIP, shade(pr, day ? 1.0f : 0.8f));
  // P_FLASH sadece firtinada simsek icin; diger sahnelerde (ay gorundugunde)
  // ayin gri tonu: denizler (maria), kenar kararmasi, golge siniri. Palet 16 renk dolu.
  scene.setPaletteColor(P_FLASH, sc == SC_THUNDER ? tft.color565(255, 255, 215) : tft.color565(165, 163, 148));
}

// Animasyon durumu
struct Drop { float x, y, v; uint8_t len; };
struct Cloud { float x; int16_t y; float s, v; };
struct Star { int16_t x, y; uint8_t ph; };
static Drop drops[70];
static int dropCount = 0;
static Cloud cloudsA[6];
static int cloudCount = 0;
static const int STAR_COUNT = 60;
static Star stars[STAR_COUNT];
static uint32_t frameNo = 0;
static uint32_t nextStrikeMs = 0, strikeEndMs = 0;
static int16_t boltX[7], boltY[7];

// Sahnenin "pencere" bandi: panellerin arasinda serbest kalan orta bolge
const int WIN_TOP = 86, WIN_BOT = 138, WIN_MID = (WIN_TOP + WIN_BOT) / 2;

static void initScene(Scene sc, bool day) {
  dropCount = 0;
  if (sc == SC_RAIN || sc == SC_THUNDER) dropCount = 70;
  else if (sc == SC_DRIZZLE) dropCount = 40;
  else if (sc == SC_SNOW) dropCount = 50;
  for (int i = 0; i < dropCount; i++) {
    Drop& d = drops[i];
    d.x = frand(0, SCENE_W + 20);
    d.y = frand(-SCENE_H, SCENE_H);
    if (sc == SC_SNOW)        { d.v = frand(22, 45);   d.len = (uint8_t)frand(1, 3); }
    else if (sc == SC_DRIZZLE){ d.v = frand(150, 220); d.len = (uint8_t)frand(4, 7); }
    else                      { d.v = frand(260, 380); d.len = (uint8_t)frand(8, 14); }
  }

  cloudCount = (sc == SC_CLEAR) ? 0 : (sc == SC_PARTLY) ? 2 : (sc == SC_FOG) ? 0 : 5;
  if (!day && sc == SC_CLOUDY) cloudCount = 3;  // gece kapali: ay ve yildizlar sik sik gorunsun
  for (int i = 0; i < cloudCount; i++) {
    Cloud& c = cloudsA[i];
    c.s = (sc == SC_PARTLY) ? frand(0.7f, 0.9f) : frand(0.8f, 1.3f);
    c.x = frand(-60, SCENE_W);
    c.y = (int16_t)frand(WIN_TOP - 4, WIN_BOT - 16);
    c.v = frand(5, 14);
  }

  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].x = (int16_t)frand(2, SCENE_W - 2);
    stars[i].y = (int16_t)frand(2, SCENE_H - 2);
    stars[i].ph = (uint8_t)frand(0, 50);
  }
  nextStrikeMs = millis() + 1500;
  strikeEndMs = 0;
}

static void drawSky() {
  const int band = SCENE_H / 4;
  for (int i = 0; i < 4; i++) {
    int h = (i == 3) ? SCENE_H - 3 * band : band;
    scene.fillRect(0, i * band, SCENE_W, h, P_SKY1 + i);
  }
}

static void drawSun(int cx, int cy, float t) {
  float rot = t * 0.4f;
  for (int k = 0; k < 10; k++) {
    float a = rot + k * (2 * PI / 10);
    float c = cosf(a), s = sinf(a);
    int r2 = 29 + (int)(2 * sinf(t * 2 + k));
    scene.drawLine(cx + c * 21, cy + s * 21, cx + c * r2, cy + s * r2, P_ACCENT);
    scene.drawLine(cx + c * 21 + 1, cy + s * 21, cx + c * r2 + 1, cy + s * r2, P_ACCENT);
  }
  scene.fillCircle(cx, cy, 16 + (int)sinf(t * 1.5f), P_ACCENT);
}

// Gercek ay evresi: 0 = yeni ay, 0.5 = dolunay. Bilinen yeni ay 2000-01-06
// 18:14 UTC + sinodik ay 29.530588853 gun. Dogrulandi: 2024-04-08 gunes
// tutulmasi -> 0.994, 2025-03-14 ay tutulmasi -> 0.492.
static float moonPhase() {
  time_t now = time(nullptr);
  if (now < 1600000000) return 0.5f;  // saat henuz senkron degil: dolunay goster
  double p = fmod((now - 947182440.0) / 86400.0 / 29.530588853, 1.0);
  return (float)(p < 0 ? p + 1 : p);
}

static void drawMoon(int cx, int cy, float phase) {
  const int R = 22;
  float c = cosf(2 * PI * phase);
  float litFrac = (1 - c) / 2;
  bool waxing = phase < 0.5f;  // buyuyen: sagdan aydinlik (kuzey yarimkure)

  // Yumusak hare: aydinlik orana gore, iceride yogun, disarida seyrek
  // titresimli (dithered) noktalar - duz halka yerine isik dagilmasi gibi
  if (litFrac > 0.15f) {
    const int H = R + 7;
    for (int dy = -H; dy <= H; dy++) {
      for (int dx = -H; dx <= H; dx++) {
        int d2 = dx * dx + dy * dy;
        if (d2 <= (R + 1) * (R + 1) || d2 > H * H) continue;
        bool inner = d2 <= (R + 4) * (R + 4);
        if (inner ? ((dx + dy) & 1) == 0 : ((dx & 1) && (dy & 1) && ((dx + dy) & 3) == 0))
          scene.drawPixel(cx + dx, cy + dy, P_CLOUD_D);
      }
    }
  }

  // Ay denizleri (maria), dolunay fotografindaki yerlerine gore (kuzey yukari).
  // (dx, dy, r) R=22 icin: Imbrium, Serenitatis, Tranquillitatis, Crisium,
  // Procellarum (2 parca), Nubium, Fecunditatis, Nectaris, Frigoris
  static const int8_t MARIA[][3] = {
    {-7, -8, 6}, {3, -8, 4}, {7, -2, 5}, {15, -6, 3},
    {-14, 0, 6}, {-11, 7, 5}, {-3, 8, 4}, {12, 4, 3}, {7, 8, 2}, {-2, -16, 2}
  };
  for (int y = -R; y <= R; y++) {
    float w = sqrtf((float)(R * R - y * y));
    float term = w * c;  // golge siniri (terminator)
    int x0 = waxing ? (int)roundf(term) : (int)roundf(-w);
    int x1 = waxing ? (int)roundf(w) : (int)roundf(-term);
    if (x1 <= x0) continue;  // bu satir tamamen karanlik (gorunmez, gokyuzu kalir)
    scene.drawFastHLine(cx + x0, cy + y, x1 - x0, P_ACCENT);
    for (auto& m : MARIA) {
      int dy = y - m[1];
      if (abs(dy) > m[2]) continue;
      int mw = (int)sqrtf((float)(m[2] * m[2] - dy * dy));
      int a = max(x0, m[0] - mw), b = min(x1, m[0] + mw);
      if (b > a) scene.drawFastHLine(cx + a, cy + y, b - a, P_FLASH);
    }
    // Kenar kararmasi + yumusak golge siniri
    scene.drawPixel(cx + x0, cy + y, P_FLASH);
    scene.drawPixel(cx + x1 - 1, cy + y, P_FLASH);
    if (litFrac < 0.97f && x1 - x0 > 3) {
      scene.drawPixel(waxing ? cx + x0 + 1 : cx + x1 - 2, cy + y, P_FLASH);
    }
  }
  // Parlak kraterler: Tycho (guney), Copernicus - aydinlik taraftaysa
  auto crater = [&](int dx, int dy) {
    float w = sqrtf((float)(R * R - dy * dy));
    float term = w * c;
    bool lit = waxing ? (dx >= term && dx < w) : (dx >= -w && dx < -term);
    if (lit) { scene.drawPixel(cx + dx, cy + dy, P_WHITE); scene.drawPixel(cx + dx + 1, cy + dy, P_WHITE); }
  };
  crater(-4, 16);
  crater(-9, -2);
}

// Bulutlarin arkasindaki soluk gunes (isinsiz disk)
static void drawDimSun(int cx, int cy) {
  scene.drawCircle(cx, cy, 20, P_SKY4);
  scene.fillCircle(cx, cy, 16, P_ACCENT);
}

static void drawStars() {
  for (int i = 0; i < STAR_COUNT; i++) {
    bool bright = (i % 3 == 0);                        // ~1/3 parlak, gerisi sonuk
    bool on = ((frameNo + stars[i].ph) % 50) < 40;     // titresim
    uint8_t col = bright ? (on ? P_WHITE : P_GRAY) : (on ? P_GRAY : P_SKY3);
    scene.drawPixel(stars[i].x, stars[i].y, col);
    if (bright && on && (i % 9 == 0)) {  // en parlaklar: kucuk arti
      scene.drawPixel(stars[i].x - 1, stars[i].y, P_GRAY);
      scene.drawPixel(stars[i].x + 1, stars[i].y, P_GRAY);
      scene.drawPixel(stars[i].x, stars[i].y - 1, P_GRAY);
      scene.drawPixel(stars[i].x, stars[i].y + 1, P_GRAY);
    }
  }
}

static void drawCloud(int x, int y, float s) {
  scene.fillRoundRect(x - 12 * s, y + 3 * s, 50 * s, 12 * s, 6 * s, P_CLOUD_D);  // alt golge
  scene.fillCircle(x, y, 11 * s, P_CLOUD_L);
  scene.fillCircle(x + 14 * s, y - 6 * s, 15 * s, P_CLOUD_L);
  scene.fillCircle(x + 28 * s, y, 10 * s, P_CLOUD_L);
  scene.fillRoundRect(x - 11 * s, y, 49 * s, 10 * s, 5 * s, P_CLOUD_L);
}

static void updateClouds(float dt) {
  float windBoost = 1.0f + min(wv.wind, 40.0f) / 20.0f;  // ruzgar arttikca bulut hizlanir
  for (int i = 0; i < cloudCount; i++) {
    Cloud& c = cloudsA[i];
    c.x += c.v * windBoost * dt;
    if (c.x > SCENE_W + 20) {
      c.x = -60 * c.s;
      c.y = (int16_t)frand(WIN_TOP - 4, WIN_BOT - 16);
    }
    drawCloud((int)c.x, c.y, c.s);
  }
}

static void updatePrecip(Scene sc, float dt, float t) {
  float slant = (sc == SC_SNOW) ? 0 : constrain(wv.wind / 12.0f, 0.0f, 4.0f);  // ruzgara gore egim
  for (int i = 0; i < dropCount; i++) {
    Drop& d = drops[i];
    d.y += d.v * dt;
    if (sc == SC_SNOW) d.x += sinf(t * 1.5f + i) * 0.5f;
    else d.x -= slant * d.v * dt / d.len;
    if (d.y > SCENE_H || d.x < -10) {
      d.y = frand(-20, -4);
      d.x = frand(0, SCENE_W + 20);
    }
    if (sc == SC_SNOW) scene.fillCircle((int)d.x, (int)d.y, d.len, P_PRECIP);
    else scene.drawLine((int)d.x, (int)d.y, (int)(d.x - slant), (int)d.y + d.len, P_PRECIP);
  }
}

static void updateLightning(uint32_t now) {
  if (now >= nextStrikeMs) {
    int x = (int)frand(40, SCENE_W - 40);
    int y = 40;
    for (int i = 0; i < 7; i++) {
      boltX[i] = x;
      boltY[i] = y;
      x += (int)frand(-16, 16);
      y += (int)frand(12, 20);
    }
    strikeEndMs = now + 260;
    nextStrikeMs = now + (uint32_t)frand(2500, 7500);
  }
  bool striking = now < strikeEndMs;
  bool flash = striking && (strikeEndMs - now) > 170;  // ilk ~90ms gokyuzu parlar
  for (int i = 0; i < 4; i++) {
    scene.setPaletteColor(P_SKY1 + i, flash ? tft.color565(185, 185, 215) : skyColors[i]);
  }
  if (striking) {
    for (int i = 0; i < 6; i++) {
      scene.drawLine(boltX[i], boltY[i], boltX[i + 1], boltY[i + 1], P_FLASH);
      scene.drawLine(boltX[i] + 1, boltY[i], boltX[i + 1] + 1, boltY[i + 1], P_FLASH);
    }
  }
}

static void drawFog(float t) {
  for (int y = 4; y < SCENE_H; y += 6) {
    int off = (int)(t * (8 + (y % 12))) % 40;
    for (int x = -40 + off; x < SCENE_W; x += 40) {
      scene.drawFastHLine(x, y, 26, ((y / 6) % 2) ? P_CLOUD_L : P_CLOUD_D);
    }
  }
}

static void renderScene(Scene sc, bool day, float dt, float t, uint32_t now) {
  drawSky();
  bool openSky = (sc == SC_CLEAR || sc == SC_PARTLY);
  // Yildizlar: gece yagissiz her havada (kapalida bulutlar gectikce ortulur)
  bool precip = (sc == SC_RAIN || sc == SC_SNOW || sc == SC_THUNDER || sc == SC_DRIZZLE || sc == SC_FOG);
  if (!day && !precip) drawStars();
  // Gok cismi bulutlardan ONCE cizilir: bulutlar gecerken onu ortup acar.
  // BUGFIX: sadece yagmur/kar/firtina haric tutuluyordu - ciseleme (drizzle,
  // kod 51-57) ve sis de "precip" sayildigi halde disarida kaliyordu, %100
  // bulutlu ciselemede bile soluk gunes gorunuyordu (2026-09-28 gozlemlendi,
  // canli kod 55). Simdi TUM yagisli/sisli sahnelerde gok cismi gizli.
  if (!precip) {
    if (day) {
      if (openSky) drawSun(160, WIN_MID, t);
      else drawDimSun(160, WIN_MID);
    } else {
      drawMoon(160, WIN_MID, moonPhase());
    }
  }
  if (sc == SC_FOG) drawFog(t);
  updateClouds(dt);
  if (sc == SC_THUNDER) updateLightning(now);
  if (dropCount) updatePrecip(sc, dt, t);
  frameNo++;
}

// ============== Haber kayan yazisi (tam genislik, etiketsiz) ==============
int tickerCharWidth(char c) {
  char b[2] = {c, 0};
  return ticker.textWidth(b, 2);
}

static String tickerText;
static int tickerStart = 0;          // gorunen ilk karakter
static float tickerX = TICKER_W;     // o karakterin x konumu (baslangicta sagdan girer)

static void tickerStep(float dt) {
  // Yeni tur basliyorsa en guncel haber metnini al
  if (tickerStart == 0 && tickerX >= TICKER_W) {
    if (xSemaphoreTake(newsMutex, MUTEX_TIMEOUT) == pdTRUE) {
      tickerText = NEWS_TICKER_TEXT;
      xSemaphoreGive(newsMutex);
    }
    if (!tickerText.length()) tickerText = "Haberler yukleniyor...";
  }

  tickerX -= TICKER_SPEED * dt;  // zamana bagli: kare hizi degisse de ayni hiz
  // Tamamen soldan cikan karakterleri atla - her karede 600+ karakterin
  // hepsini cizmek yerine sadece gorunen ~70 karakteri ciziyoruz.
  while (tickerStart < (int)tickerText.length() && tickerX + tickerCharWidth(tickerText[tickerStart]) <= 0) {
    tickerX += tickerCharWidth(tickerText[tickerStart]);
    tickerStart++;
  }
  if (tickerStart >= (int)tickerText.length()) {
    tickerStart = 0;
    tickerX = TICKER_W;
  }

  ticker.fillSprite(COL_HEADER);
  ticker.setTextColor(TFT_WHITE, COL_HEADER);
  ticker.drawString(tickerText.substring(tickerStart, tickerStart + 90), (int)tickerX, 5, 2);
  ticker.pushSprite(0, TICKER_Y);
}

// ============== Haber (AA dunya RSS) ==============
// USB takmadan uzaktan teshis: /fetch_status "news" alaninda gorunur
static String newsState = "henuz denenmedi";
String newsStatus() { return newsState; }

static void setNewsState(const String& s) {
  newsState = s;
  Serial.println("[Haber] " + s);
}

// BUGFIX: eskiden tum RSS (dun 15KB, bugun 21.7KB) getString() ile tek parca
// bellege aliniyordu. Ekran sprite'lari + TLS'den sonra o buyuklukte blok
// kalmayinca yanit bos geliyor, kod da LOG YAZMADAN vazgeciyordu ("Haberler
// yukleniyor" takili kaliyordu). Simdi akis halinde okunup ilk NEWS_MAX_ITEMS
// baslik bulununca baglanti kesiliyor (~1KB bellek).
bool fetchNews() {
  if (WiFi.status() != WL_CONNECTED) { setNewsState("WiFi yok"); return false; }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(5000);
  http.useHTTP10(true);  // chunked kodlama olmasin - ham akista parca boyutlari araya girmesin
  http.setUserAgent("MerkeziHUB/1.0");  // addHeader("User-Agent") ESP32'de yok sayiliyor
  if (!http.begin(client, NEWS_RSS_URL)) { setNewsState("begin basarisiz"); return false; }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setNewsState("HTTP " + String(code) + (code < 0 ? " " + http.errorToString(code) : ""));
    http.end();
    return false;
  }

  WiFiClient* s = http.getStreamPtr();
  String text, title, win;
  bool inItem = false, inTitle = false;
  int count = 0;
  unsigned long deadline = millis() + 8000;
  while (count < NEWS_MAX_ITEMS && millis() < deadline && (s->connected() || s->available())) {
    if (!s->available()) { delay(5); continue; }
    char ch = (char)s->read();
    if (inTitle) {
      title += ch;
      if (title.endsWith("</title>")) {
        title.remove(title.length() - 8);
        title.replace("<![CDATA[", "");
        title.replace("]]>", "");
        title = trAscii(decodeEntities(title));
        title.trim();
        if (title.length()) {
          if (text.length()) text += "     +++     ";
          text += title;
          count++;
        }
        inTitle = false;
        inItem = false;  // haber basina sadece ilk <title>
        title = "";
      } else if (title.length() > 400) {  // bozuk/beklenmedik icerik
        inTitle = false;
        title = "";
      }
      continue;
    }
    win += ch;
    if (win.length() > 8) win.remove(0, win.length() - 8);
    if (!inItem && win.endsWith("<item>")) inItem = true;
    else if (inItem && win.endsWith("<title>")) { inTitle = true; title = ""; }
  }
  http.end();

  if (!count) { setNewsState("baslik bulunamadi (bos heap " + String(ESP.getFreeHeap()) + ")"); return false; }
  if (xSemaphoreTake(newsMutex, MUTEX_TIMEOUT) == pdTRUE) {
    NEWS_TICKER_TEXT = text;
    xSemaphoreGive(newsMutex);
  }
  struct tm t;
  char ts[6] = "--:--";
  if (getLocalTime(&t, 0)) strftime(ts, sizeof(ts), "%H:%M", &t);
  setNewsState("OK " + String(count) + " baslik " + ts);
  return true;
}

// ============== Ekran gorevi (TFT'nin tek sahibi) ==============
void displayTask(void* pvParameters) {
  // Acilis ekranlarindan kalan setTextSize(2/3) sonraki tum font cizimlerini
  // 2-3 kat buyutuyordu - sifirla
  tft.setTextSize(1);

  ticker.createSprite(TICKER_W, TICKER_H);
  scene.setColorDepth(4);
  bool animated = scene.createSprite(SCENE_W, SCENE_H) != nullptr;
  Serial.printf("[Ekran] canli sahne: %s | bos heap %u, en buyuk blok %u\n",
                animated ? "ACIK" : "KAPALI (bellek yok, statik ekran)",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (animated) setFixedPalette();

  tft.fillScreen(TFT_BLACK);
  readWeather();
  drawHeader();

  Scene cur = SC_NONE;
  bool curDay = true;
  uint32_t last = millis();
  float t = 0;

  for (;;) {
    uint32_t now = millis();
    float dt = (now - last) / 1000.0f;
    last = now;
    if (dt > 0.2f) dt = 0.2f;  // uzun takilmadan sonra parcaciklar isinlanmasin
    t += dt;

    if (weatherDisplayDirty) {
      weatherDisplayDirty = false;
      if (readWeather()) {
        drawHeader();
        if (!animated) {
          tft.fillRect(0, SCENE_Y, SCENE_W, SCENE_H, TFT_BLACK);
          drawPanels(tft, SCENE_Y, TFT_COLS);
        }
      } else {
        weatherDisplayDirty = true;  // mutex mesgul - sonraki turda tekrar dene
      }
    }
    drawClock(false);

    // Haber (HTTPS/TLS). BUGFIX (2026-09-28): TLS handshake tek parca ~35-45KB
    // bellek istiyor; sahne+ticker sprite'lari acikken en buyuk bos blok bunun
    // altina dustugu icin surekli "start_ssl_client: -32512" (bellek yetersiz)
    // ile basarisiz oluyordu - haber SONSUZA dek "yukleniyor" gorunuyordu.
    // Sprite'lar sadece bu task'a ait, o yuzden TLS sirasinda GUVENLE gecici
    // silinip sonra geri kurulabiliyor (baska task erismiyor).
    {
      static unsigned long lastNewsUpdate = 0;
      static bool newsOk = false, spriteOk = true, firstNewsRun = true;
      unsigned long newsInterval = (newsOk && spriteOk) ? NEWS_UPDATE_INTERVAL : 30000UL;
      if (firstNewsRun || now - lastNewsUpdate >= newsInterval) {
        firstNewsRun = false;
        lastNewsUpdate = now;
        bool wasAnimated = animated;
        if (animated) scene.deleteSprite();
        ticker.deleteSprite();
        newsOk = fetchNews();
        // Tani: fetchNews HEMEN sonrasi, herhangi bir deneme yapmadan ONCE -
        // gercek "an" bu. Onceki log 6 deneme+900ms SONRA yazdiriliyordu, o
        // sirada baska gorevler bellek serbest biraktigi icin yaniltici
        // (40948 gorunuyordu ama ilk denemeler daha az bulmus olmali).
        Serial.printf("[Ekran] haber sonrasi (deneme oncesi): bos heap %u, en buyuk blok %u, gerekli %u\n",
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap(), (unsigned)((SCENE_W * SCENE_H) / 2 + 1));
        ticker.createSprite(TICKER_W, TICKER_H);
        scene.setColorDepth(4);
        // BUGFIX (2026-09-28): TLS'in kendi gecici tahsisleri, bosalttigimiz
        // bloklari baska gorevlerin (hava/durum) o sirada yaptigi kucuk
        // tahsislerle bolebiliyor - ilk deneme basarisiz olabiliyor ("gun
        // animasyonu yok" sikayeti). Digerleri serbest birakana kadar kisa
        // aralikla birkac kez dene.
        animated = false;
        for (int attempt = 0; attempt < 10 && !animated; attempt++) {
          if (attempt) vTaskDelay(pdMS_TO_TICKS(300));
          animated = scene.createSprite(SCENE_W, SCENE_H) != nullptr;
        }
        if (animated) { setFixedPalette(); cur = SC_NONE; }  // palet sifirlandi, yeniden kur
        spriteOk = animated || !wasAnimated;  // sadece ACIK'tan KAPALI'ya duserse sorun
        if (!spriteOk) {
          Serial.printf("[Ekran] UYARI: haber sonrasi sahne geri kurulamadi (10 deneme) | bos heap %u, en buyuk blok %u\n",
                        ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
        drawHeader();
        weatherDisplayDirty = true;  // animated durumu degismis olabilir - guvenli tam yeniden cizim
      }
    }

    if (animated) {
      Scene sc = wv.ok ? sceneFromCode(wv.code) : SC_CLOUDY;
      if (sc != cur || wv.isDay != curDay) {
        cur = sc;
        curDay = wv.isDay;
        setScenePalette(cur, curDay);
        initScene(cur, curDay);
      }
      renderScene(cur, curDay, dt, t, now);
      drawPanels(scene, 0, SPRITE_COLS);
      scene.pushSprite(0, SCENE_Y);
    }

    tickerStep(dt);
    vTaskDelay(pdMS_TO_TICKS(animated ? 5 : 25));  // diger gorevlere nefes payi
  }
}

void displayBoot() {
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(20, 80);
  tft.println("Merkezi HUB");
  tft.println("Baslatiliyor...");
}

void displayWifiConnected() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 50);
  tft.println("WiFi Baglandi!");
  tft.setTextSize(2);
  tft.setCursor(10, 120);
  tft.print("IP: ");
  tft.println(WiFi.localIP());
}

void startDisplayTask() {
  xTaskCreatePinnedToCore(displayTask, "Display", 6144, NULL, 1, NULL, 1);
}
