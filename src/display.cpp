#include "display.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <functional>
#include "app_state.h"
#include "weather.h"
#include "alerts.h"

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
  int fcN = 0, fcCode[7], fcMax[7], fcMin[7];  // [0]=dun, [1]=bugun
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
  wv.fcN = 0;
  for (JsonArray d : CURRENT_WEATHER_DOC["fc"].as<JsonArray>()) {
    if (wv.fcN >= 7) break;
    wv.fcCode[wv.fcN] = d[0] | -1;
    wv.fcMax[wv.fcN] = d[1] | 0;
    wv.fcMin[wv.fcN] = d[2] | 0;
    wv.fcN++;
  }
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

  // Kisa tarih (surumun ustunde): "28 Eyl Pzt"
  static const char* MON[12] = {"Oca","Sub","Mar","Nis","May","Haz","Tem","Agu","Eyl","Eki","Kas","Ara"};
  static const char* WD[7] = {"Paz","Pzt","Sal","Car","Per","Cum","Cmt"};
  char date[16];
  snprintf(date, sizeof(date), "%d %s %s", t.tm_mday, MON[t.tm_mon], WD[t.tm_wday]);
  tft.fillRect(168, 0, 72, 12, COL_HEADER);
  tft.setTextColor(TFT_WHITE, COL_HEADER);
  tft.drawString(date, 234, 3, 1);
  tft.setTextDatum(TL_DATUM);
}

// Aktif uyari varsa ust seridin sol tarafi (konum) kirmizi kalin uyari metnine doner.
// Bos = uyari yok. Sadece displayTask yazar/okur.
static String hdrAlert;
static void drawHeaderLeft() {
  tft.fillRect(0, 0, 166, HEADER_H - 1, COL_HEADER);
  tft.setTextDatum(TL_DATUM);
  if (hdrAlert.length()) {
    String s = fitText(hdrAlert, 154, 2);
    tft.setTextColor(0xF800, COL_HEADER);  // saf kirmizi (COL_RED soluk pembe)
    tft.drawString(s, 8, 7, 2);
    tft.drawString(s, 9, 7, 2);  // 1px kaydirip tekrar: sahte kalin
  } else {
    tft.setTextColor(COL_CYAN, COL_HEADER);
    tft.drawString(fitText(wv.city.length() ? shortLocation(wv.city) : "Merkezi HUB", 155, 2), 8, 7, 2);
  }
}

static void drawHeader() {
  tft.fillRect(0, 0, 320, HEADER_H, COL_HEADER);
  tft.drawFastHLine(0, HEADER_H - 1, 320, COL_CYAN);
  drawHeaderLeft();
  // Surum: kucuk ama okunur (GLCD 6x8), tarihin altinda
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COL_GRAY, COL_HEADER);
  tft.drawString("v" FW_VERSION, 234, 16, 1);
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

// ~26x20 kutuda WMO koduna gore ikon; daire tabanli, olcek esit (en/boy korunur)
static void miniCloud(TFT_eSPI& g, int cx, int cy, uint16_t col) {
  g.fillCircle(cx - 5, cy + 1, 4, col);
  g.fillCircle(cx + 1, cy - 2, 5, col);
  g.fillCircle(cx + 6, cy + 1, 4, col);
  g.fillRect(cx - 5, cy + 1, 11, 4, col);
}

static void drawMiniIcon(TFT_eSPI& g, int cx, int cy, int code, bool past, const PanelColors& c) {
  uint16_t sun = past ? c.gray : c.orange;
  uint16_t cloud = past ? c.gray : c.white;
  uint16_t drop = past ? c.gray : c.cyan;
  switch (code < 0 ? SC_NONE : sceneFromCode(code)) {
    case SC_CLEAR:
      g.fillCircle(cx, cy, 5, sun);
      for (int a = 0; a < 8; a++) {
        float r = a * PI / 4;
        g.drawLine(cx + cosf(r) * 7, cy + sinf(r) * 7, cx + cosf(r) * 9, cy + sinf(r) * 9, sun);
      }
      break;
    case SC_PARTLY:
      g.fillCircle(cx - 4, cy - 3, 5, sun);
      miniCloud(g, cx + 2, cy + 2, cloud);
      break;
    case SC_FOG:
      for (int k = -1; k <= 1; k++) g.drawFastHLine(cx - 9, cy + k * 5, 18, c.gray);
      break;
    case SC_DRIZZLE:
    case SC_RAIN:
      miniCloud(g, cx, cy - 3, cloud);
      for (int k = -1; k <= 1; k++) g.drawLine(cx + k * 5, cy + 5, cx + k * 5 - 2, cy + 9, drop);
      break;
    case SC_SNOW:
      miniCloud(g, cx, cy - 3, cloud);
      for (int k = -1; k <= 1; k++) g.fillCircle(cx + k * 5, cy + 7, 1, c.white);
      break;
    case SC_THUNDER:
      miniCloud(g, cx, cy - 3, cloud);
      g.drawLine(cx + 1, cy + 3, cx - 2, cy + 7, sun);
      g.drawLine(cx - 2, cy + 7, cx + 2, cy + 7, sun);
      g.drawLine(cx + 2, cy + 7, cx - 1, cy + 10, sun);
      break;
    case SC_NONE:
      g.drawString("?", cx, cy - 4, 1);
      break;
    default:  // SC_CLOUDY
      miniCloud(g, cx, cy, cloud);
      break;
  }
}

static void drawPanels(TFT_eSPI& g, int oy, const PanelColors& c) {
  // Sicaklik (etiket yerine hava aciklamasi: "Kapali", "Yagmurlu"...)
  // Sicaklik/ruzgar kutulari seffaf: dolgu yok, yazilar zeminsiz (arkadaki
  // canli sahne gorunur) - sadece cerceve kaliyor
  g.drawRoundRect(6, oy + 6, 150, 80, 7, c.orange);
  const char* desc = !wv.loaded ? "Bekleniyor..." : !wv.ok ? "Veri alinamadi" : weatherText(wv.code);
  g.setTextColor(wv.loaded && !wv.ok ? c.red : c.white);
  g.drawString(fitText(desc, 136, 2), 14, oy + 10, 2);
  g.setTextColor(c.orange);
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
  g.drawRoundRect(164, oy + 6, 150, 80, 7, c.cyan);
  g.setTextColor(c.gray);
  g.drawString("RUZGAR", 172, oy + 10, 2);
  g.setTextColor(c.cyan);
  if (wv.ok) {
    g.setTextDatum(TR_DATUM);
    g.setTextColor(c.white);
    g.drawString(trAscii(wv.dir), 306, oy + 10, 2);
    g.setTextDatum(TL_DATUM);
    g.setTextColor(c.cyan);
    String v = bigValue(wv.wind, 100);
    g.drawString(v, 170, oy + 30, 7);
    g.drawString("km/s", 170 + g.textWidth(v, 7) + 4, oy + 62, 2);
  } else {
    g.drawString("--", 170, oy + 30, 7);
  }

  // Alt bilgi: 7 gunluk tahmin ikonlari (dun, BUGUN, +5 gun) - seffaf serit
  // (dolgu yok), arkadaki animasyon gorunur
  if (!wv.ok || wv.fcN == 0) return;
  const int py = oy + 134, ph = 46;
  g.drawRoundRect(6, py, 308, ph, 7, c.gray);

  static const char* DAYS[7] = {"Pz", "Pt", "Sa", "Ca", "Pe", "Cu", "Ct"};
  struct tm t;
  int todayW = getLocalTime(&t, 0) ? t.tm_wday : -1;
  const int colW = 43, x0 = 10;
  g.setTextDatum(TC_DATUM);
  for (int i = 0; i < wv.fcN; i++) {
    int cx = x0 + i * colW + colW / 2;
    bool today = (i == 1), past = (i == 0);
    if (today) g.drawRoundRect(cx - colW / 2 + 1, py + 2, colW - 2, ph - 4, 5, c.orange);
    uint16_t txt = today ? c.orange : past ? c.gray : c.white;
    g.setTextColor(txt);
    g.drawString(todayW < 0 ? "--" : (today ? "BUGUN" : DAYS[(todayW + i + 6) % 7]), cx, py + 4, 1);
    g.setTextColor(past ? c.gray : c.cyan);
    g.drawString(String(wv.fcMax[i]) + "/" + String(wv.fcMin[i]), cx, py + 14, 1);
    drawMiniIcon(g, cx, py + 33, wv.fcCode[i], past, c);
  }
  g.setTextDatum(TL_DATUM);
}

// ============== Canli hava sahnesi ==============

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

// ============== Gunes konumu / gokyuzu renkleri (saate gore) ==============
// Gunes yuksekligi (derece, yaklasik): gunduz 50*sin(pi*f) (f = gun ici oran),
// gece ufuk civarindaki hizla (~0.2 derece/dk) asagi iner. Gercek astronomi
// degil ama tan agarmasi (-18..-6), sivil alacakaranlik (-6..0), altin saat
// (0..8) gibi evreleri gundogumu/batimi saatinden dogru sirayla uretir.
struct SkyTime {
  bool valid;
  float elev;      // derece; <0 gunes ufkun altinda
  bool evening;    // ogleden sonra / aksam (kizillik sabahtan daha kirmizi)
  float dayFrac;   // 0 = gundogumu, 1 = gunbatimi (gunes yayi icin)
  float noonH;     // gunes ogleni (saat) - ay konumu icin
};

// Gokyuzu demo modu (/sky_demo): 24 saati ~2 dakikada oynatir, sonra gercek
// saate doner - tan/kizillik/gece gecislerini beklemeden gormek icin
static volatile bool skyDemoReq = false;
static float skyDemoMin = -1, skyDemoLeft = 0;
const float SKY_DEMO_SPEED = 12.0f;  // dakika / saniye

void startSkyDemo() { skyDemoReq = true; }

// Hava demosu (/weather_demo): acik -> firtina tum sahneler 8'er sn
static volatile bool wxDemoReq = false;
static int wxDemoIdx = -1;
static uint32_t wxDemoStart = 0;
void startWeatherDemo() { wxDemoReq = true; }

// Sahnenin kullandigi saat (gece yarisindan beri dakika) - demo acikken sanal saat
static bool sceneMinutes(float& out) {
  if (skyDemoMin >= 0) { out = skyDemoMin; return true; }
  struct tm t;
  if (!getLocalTime(&t, 0)) return false;
  out = t.tm_hour * 60 + t.tm_min + t.tm_sec / 60.0f;
  return true;
}

static SkyTime skyTime() {
  SkyTime s{false, wv.isDay ? 30.0f : -30.0f, false, 0.5f, 12.5f};
  if (wv.sunrise.length() != 5 || wv.sunset.length() != 5) return s;
  float now;
  if (!sceneMinutes(now)) return s;
  float rise = wv.sunrise.substring(0, 2).toInt() * 60 + wv.sunrise.substring(3, 5).toInt();
  float set = wv.sunset.substring(0, 2).toInt() * 60 + wv.sunset.substring(3, 5).toInt();
  if (set <= rise) return s;
  const float PEAK = 50.0f, RATE = 0.2f, EVE_RATE = 0.5f;
  s.valid = true;
  s.noonH = (rise + set) / 120.0f;
  if (now >= rise && now <= set) {
    s.dayFrac = (now - rise) / (set - rise);
    s.elev = PEAK * sinf(PI * s.dayFrac);
    s.evening = s.dayFrac > 0.5f;
  } else if (now < rise) {
    s.elev = -RATE * (rise - now);
    s.dayFrac = 0;
  } else {
    s.elev = -EVE_RATE * (now - set);  // aksam kizilligi sabahtan daha cabuk sonsun
    s.evening = true;
    s.dayFrac = 1;
  }
  if (s.elev < -30) s.elev = -30;
  return s;
}

static void mixRGB(const RGB a, const RGB b, float f, RGB out) {
  f = constrain(f, 0.0f, 1.0f);
  for (int j = 0; j < 3; j++) out[j] = (uint8_t)(a[j] + (b[j] - a[j]) * f);
}

// Acik gokyuzu, gunes yuksekligine gore (ust -> ufuk). Aksam tablosu ufukta
// daha kizil/mor (toz, nem), sabah daha pembe-altin.
static const float SKY_E[10] = {-18, -12, -7, -3, 0, 3, 8, 15, 30, 50};
static const RGB SKY_AM[10][4] = {
  {{0,0,0},{0,0,0},{0,0,2},{1,2,6}},               // gece: siyah
  {{0,0,2},{1,2,8},{4,6,20},{14,14,38}},           // astronomik tan
  {{6,12,44},{22,30,80},{60,55,110},{120,80,120}}, // tan agarmasi (mor)
  {{16,26,84},{80,60,130},{220,80,85},{255,105,50}},    // safak kizilligi
  {{30,50,130},{150,90,140},{255,110,70},{255,125,35}},  // gundogumu
  {{40,80,165},{120,125,190},{250,145,90},{255,165,60}},
  {{35,90,180},{70,130,210},{140,175,220},{220,210,190}},  // altin saat sonu
  {{28,85,185},{55,120,210},{100,160,230},{175,205,235}},
  {{20,75,178},{42,108,202},{78,148,226},{150,195,240}},
  {{14,64,170},{34,98,198},{68,138,224},{140,190,242}},    // ogle
};
static const RGB SKY_PM[10][4] = {
  {{0,0,0},{0,0,0},{0,0,2},{1,2,6}},
  {{0,0,2},{2,2,8},{8,4,18},{22,8,20}},
  {{6,6,34},{40,18,62},{130,30,60},{190,40,30}},   // alacakaranlik (mor-kizil)
  {{20,20,72},{120,40,90},{240,55,45},{255,75,20}},
  {{40,45,110},{170,65,105},{255,80,40},{255,100,20}},    // gunbatimi
  {{40,75,160},{140,100,160},{255,120,70},{255,145,45}},
  {{35,90,180},{70,130,210},{140,175,220},{220,210,190}},
  {{28,85,185},{55,120,210},{100,160,230},{175,205,235}},
  {{20,75,178},{42,108,202},{78,148,226},{150,195,240}},
  {{14,64,170},{34,98,198},{68,138,224},{140,190,242}},
};

static void skyAt(float e, bool evening, RGB out[4]) {
  const RGB (*T)[4] = evening ? SKY_PM : SKY_AM;
  int i = 0;
  while (i < 8 && e >= SKY_E[i + 1]) i++;
  float f = (e - SKY_E[i]) / (SKY_E[i + 1] - SKY_E[i]);
  for (int k = 0; k < 4; k++) mixRGB(T[i][k], T[i + 1][k], f, out[k]);
}

// Gunes rengi: ufukta kizil-turuncu, yukseldikce sari-beyaz
static void sunColorAt(float e, RGB out) {
  static const float E[6] = {-2, 0, 3, 8, 20, 40};
  static const RGB C[6] = {{255,50,25},{255,80,30},{255,130,45},{255,190,80},{255,225,120},{255,242,175}};
  int i = 0;
  while (i < 4 && e >= E[i + 1]) i++;
  mixRGB(C[i], C[i + 1], (e - E[i]) / (E[i + 1] - E[i]), out);
}

// 0 = alacakaranlik yok, 1 = tam kizillik (gunes ufuk civarindayken) -
// kapali/yagisli havalarin paletinde ufka sizan renk icin
static float twilightFactor() {
  SkyTime st = skyTime();
  if (!st.valid) return 0;
  return constrain(1.0f - fabsf(st.elev - 0.5f) / 9.0f, 0.0f, 1.0f);
}

// Acik/parcali bulutlu gokyuzu paleti: tamamen gunes yuksekligine gore
static void setOpenSkyPalette(Scene sc, const SkyTime& st) {
  RGB sky[4];
  skyAt(st.elev, st.evening, sky);
  for (int i = 0; i < 4; i++) {
    skyColors[i] = tft.color565(sky[i][0], sky[i][1], sky[i][2]);
    scene.setPaletteColor(P_SKY1 + i, skyColors[i]);
  }
  RGB sun;
  sunColorAt(st.elev, sun);
  static const RGB MOON = {240, 238, 205};
  bool sunUp = st.elev > -2;
  const uint8_t* acc = sunUp ? sun : MOON;
  scene.setPaletteColor(P_ACCENT, tft.color565(acc[0], acc[1], acc[2]));

  // Zemin (tepeler): gunduz koyu yesil, alacakaranlikta siyaha yakin siluet
  float light = constrain((st.elev + 4) / 16.0f, 0.0f, 1.0f);
  static const RGB GROUND_D = {14, 32, 20}, GROUND_N = {16, 18, 30};
  RGB g;
  mixRGB(GROUND_N, GROUND_D, light, g);
  scene.setPaletteColor(P_BG, tft.color565(g[0], g[1], g[2]));

  // Bulutlar: gunduz beyaz, gece koyu gri; gunes ufuk civarindayken alttan
  // ufuk rengiyle (pembe/turuncu) boyanir
  float tw = constrain(1.0f - fabsf(st.elev - 1) / 8.0f, 0.0f, 1.0f);
  static const RGB CL_D = {240,244,248}, CD_D = {190,200,212}, CL_N = {70,78,98}, CD_N = {45,50,66};
  RGB cl, cd;
  mixRGB(CL_N, CL_D, light, cl);
  mixRGB(CD_N, CD_D, light, cd);
  mixRGB(cl, sky[3], tw * 0.55f, cl);
  mixRGB(cd, sky[2], tw * 0.5f, cd);

  if (sc == SC_PARTLY) {
    scene.setPaletteColor(P_CLOUD_L, tft.color565(cl[0], cl[1], cl[2]));
    scene.setPaletteColor(P_CLOUD_D, tft.color565(cd[0], cd[1], cd[2]));
  } else if (sunUp) {
    // Acik gunduz: bulut yok - CLOUD_D gunes haresi (gunes ile gok arasi)
    RGB glow;
    mixRGB(sun, sky[2], 0.55f, glow);
    scene.setPaletteColor(P_CLOUD_D, tft.color565(glow[0], glow[1], glow[2]));
    scene.setPaletteColor(P_CLOUD_L, tft.color565(255, 222, 170));
  } else {
    // Acik gece: sicak renkli yildizlar + Samanyolu/ay haresi
    scene.setPaletteColor(P_CLOUD_L, tft.color565(255, 222, 170));
    scene.setPaletteColor(P_CLOUD_D, tft.color565(48, 52, 84));
  }
  scene.setPaletteColor(P_PRECIP, tft.color565(165, 190, 255));  // yagis yok: mavi yildizlar
  scene.setPaletteColor(P_FLASH, tft.color565(165, 163, 148));    // ay denizleri
}

static void setScenePalette(Scene sc, bool day) {
  SkyTime st = skyTime();
  if ((sc == SC_CLEAR || sc == SC_PARTLY) && st.valid) {
    setOpenSkyPalette(sc, st);
    return;
  }
  // Zemin (tepe siluetleri): karda beyaz, siste soluk, digerlerinde koyu
  {
    RGB g = {24, 36, 30};
    if (sc == SC_SNOW) { g[0] = 200; g[1] = 205; g[2] = 214; }
    else if (sc == SC_FOG) { g[0] = 96; g[1] = 100; g[2] = 104; }
    float gk = day ? 1.0f : 0.3f;
    scene.setPaletteColor(P_BG, shade(g, gk));
  }
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
static const int STAR_COUNT = 90;
static Star stars[STAR_COUNT];
static uint32_t frameNo = 0;
static uint32_t nextStrikeMs = 0, strikeEndMs = 0;
static int16_t boltX[7], boltY[7];
static const int MW_COUNT = 220;           // Samanyolu noktalari
static int16_t mwX[MW_COUNT], mwY[MW_COUNT];
static uint32_t meteorStart = 0, nextMeteor = 0;
static float mX, mY, mVX, mVY;

// Sahnenin "pencere" bandi: panellerin arasinda serbest kalan orta bolge
const int WIN_TOP = 86, WIN_BOT = 134, WIN_MID = (WIN_TOP + WIN_BOT) / 2;
// Ufuk: tepe siluetlerinin ortalama ust siniri; gunes/ay buradan dogup batar
const int HORIZON_Y = 160;
const int ARC_TOP = 104;  // ogle gunesi (sicaklik/ruzgar yazilarinin altinda kalsin)
static int8_t hillH[SCENE_W];   // yakin sira (koyu, dolu)
static int8_t farH[SCENE_W];    // uzak daglar (yuksek, soluk - noktali)

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
    stars[i].y = (int16_t)frand(2, HORIZON_Y - 42);  // daglarin ustundeki gok
    stars[i].ph = (uint8_t)frand(0, 50);
  }
  // Samanyolu: sol alttan sag uste capraz, ortasi yogun bir serit
  for (int i = 0; i < MW_COUNT; i++) {
    float u = frand(0, 1);
    float off = (frand(-1, 1) + frand(-1, 1) + frand(-1, 1)) * 9;  // ~normal dagilim
    mwX[i] = (int16_t)(30 + u * 260 + off * 0.6f);
    mwY[i] = (int16_t)(HORIZON_Y - u * (HORIZON_Y - 4) + off);
  }
  // Daglar: sinus toplamlari - her acilista ayni manzara. Uzak sira sivri
  // tepeli (|sin| ile), yakin sira yuvarlak ve alcak.
  for (int x = 0; x < SCENE_W; x++) {
    hillH[x] = (int8_t)(9 + 7 * sinf(x * 0.021f + 1.3f) + 4 * sinf(x * 0.057f + 0.4f) + 1.5f * sinf(x * 0.13f));
    float peaks = 18 * (1 - fabsf(sinf(x * 0.018f + 0.6f))) + 9 * (1 - fabsf(sinf(x * 0.047f + 2.1f)));
    farH[x] = (int8_t)(12 + peaks + 1.5f * sinf(x * 0.21f));
  }
  nextStrikeMs = millis() + 1500;
  strikeEndMs = 0;
  nextMeteor = millis() + (uint32_t)frand(15000, 40000);
  meteorStart = 0;
}

// 4 gok rengi, bant sinirlari Bayer desenli noktalarla (dithering) eritilir:
// 16 renkli palette duz seritler yerine yumusak gradyan gibi gorunur
static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static void drawSky() {
  const int band = HORIZON_Y / 4;
  const int BLEND = 12;  // sinirin her iki yaninda gecis satiri
  for (int y = 0; y < SCENE_H; y++) {
    int i = min(y / band, 3);
    int base = P_SKY1 + i, next = -1;
    float p = 0;
    int below = (i + 1) * band, above = i * band;
    if (i < 3 && y >= below - BLEND) { next = base + 1; p = (y - (below - BLEND)) / (2.0f * BLEND); }
    else if (i > 0 && y < above + BLEND) { next = base - 1; p = ((above + BLEND) - y) / (2.0f * BLEND); }
    scene.drawFastHLine(0, y, SCENE_W, base);
    if (next >= 0) {
      int th = (int)(p * 16);
      const uint8_t* row = BAYER4[y & 3];
      for (int x = 0; x < SCENE_W; x++)
        if (row[x & 3] < th) scene.drawPixel(x, y, next);
    }
  }
}

static void drawHills(bool farVisible) {
  // Uzak daglar: ayni renk ama noktali -> havanin arkasinda soluk kalir
  // (hava perspektifi), yakin sira onun onunde tam koyu. Siste uzak sira yok.
  for (int x = 0; x < SCENE_W && farVisible; x++) {
    int top = HORIZON_Y - farH[x];
    for (int y = top; y < HORIZON_Y; y++)
      if (((x + y) & 3) != 0 || y <= top + 1) scene.drawPixel(x, y, P_BG);
  }
  for (int x = 0; x < SCENE_W; x++) {
    int top = HORIZON_Y - hillH[x];
    scene.drawFastVLine(x, top, SCENE_H - top, P_BG);
  }
}

// Yumusak hare: iceride yogun, disarida seyrek titresimli noktalar
static void drawHalo(int cx, int cy, int r, int w, uint8_t col) {
  const int H = r + w;
  for (int dy = -H; dy <= H; dy++) {
    for (int dx = -H; dx <= H; dx++) {
      int d2 = dx * dx + dy * dy;
      if (d2 <= (r + 1) * (r + 1) || d2 > H * H) continue;
      bool inner = d2 <= (r + w / 2) * (r + w / 2);
      if (inner ? ((dx + dy) & 1) == 0 : ((dx & 1) && (dy & 1) && ((dx + dy) & 3) == 0))
        scene.drawPixel(cx + dx, cy + dy, col);
    }
  }
}

// Gok cismi yayi: f=0 doguda ufukta, 0.5 en yuksekte, 1 batida ufukta
static void arcPos(float f, int& x, int& y) {
  x = 24 + (int)(f * (SCENE_W - 48));
  y = HORIZON_Y - (int)(sinf(PI * f) * (HORIZON_Y - ARC_TOP));
}

static void sunPos(const SkyTime& st, int& x, int& y) {
  if (!st.valid) { x = 160; y = WIN_MID; return; }
  arcPos(st.dayFrac, x, y);
  if (st.elev < 0) y = HORIZON_Y + (int)(-st.elev * 4);  // batmis: tepelerin arkasina iner
}

// Ay: gunese gore evre kadar geride - yeni ay gunesle birlikte, dolunay gece
// yarisi meridyende. Ufuk ustunde degilse false (o gece ay yok ya da gec dogar).
static bool moonPos(const SkyTime& st, float phase, int& x, int& y) {
  if (!st.valid) { x = 160; y = WIN_MID; return true; }
  float nowMin;
  if (!sceneMinutes(nowMin)) return false;
  float nowH = nowMin / 60.0f;
  float transit = fmodf(st.noonH + phase * 24.84f, 24.0f);
  float ha = nowH - transit;
  if (ha > 12) ha -= 24;
  if (ha < -12) ha += 24;
  const float HALF = 7.0f;  // ufuk ustunde kalma yarisi (saat)
  if (fabsf(ha) > HALF + 0.3f) return false;
  arcPos((ha + HALF) / (2 * HALF), x, y);
  return true;
}

static void drawSun(int cx, int cy, float t, float elev, bool glow) {
  int r = elev < 6 ? 19 : 16;  // ufukta daha iri gorunur
  if (glow) drawHalo(cx, cy, r, 12, P_CLOUD_D);
  if (elev > 6) {
    float rot = t * 0.4f;
    for (int k = 0; k < 10; k++) {
      float a = rot + k * (2 * PI / 10);
      float c = cosf(a), s = sinf(a);
      int r2 = r + 13 + (int)(2 * sinf(t * 2 + k));
      scene.drawLine(cx + c * (r + 5), cy + s * (r + 5), cx + c * r2, cy + s * r2, P_ACCENT);
      scene.drawLine(cx + c * (r + 5) + 1, cy + s * (r + 5), cx + c * r2 + 1, cy + s * r2, P_ACCENT);
    }
  }
  scene.fillCircle(cx, cy, r + (int)sinf(t * 1.5f), P_ACCENT);
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
  // Soluk kucuk kraterler (sadece aydinlik tarafta): halka ya da tek nokta
  static const int8_t CRATERS[][3] = {
    {6, -15, 1}, {-13, -11, 1}, {11, 13, 2}, {-17, 9, 1}, {16, -3, 1}, {3, 15, 1}, {-5, 3, 1}, {-15, -4, 2}
  };
  for (auto& k : CRATERS) {
    int dx = k[0], dy = k[1];
    float w = sqrtf((float)(R * R - dy * dy));
    float term = w * c;
    bool lit = waxing ? (dx >= term + 1 && dx < w - 1) : (dx > -w + 1 && dx < -term - 1);
    if (!lit) continue;
    if (k[2] >= 2) scene.drawCircle(cx + dx, cy + dy, 2, P_FLASH);
    else scene.drawPixel(cx + dx, cy + dy, P_FLASH);
  }
}

// Bulutlarin arkasindaki soluk gunes (isinsiz disk)
static void drawDimSun(int cx, int cy) {
  scene.drawCircle(cx, cy, 20, P_SKY4);
  scene.fillCircle(cx, cy, 16, P_ACCENT);
}

// Yildizlar gok karardikca kademeli cikar: once en parlaklar (gunes -4
// derecenin altinda), sonra orta, tam karanlikta (-12) hepsi. Orta parlaktakilerin
// bir kismi mavimsi / sicak renkli (renkler sadece acik gecede palette var).
static void drawStars(bool colored, float elev) {
  int tier = elev < -12 ? 3 : elev < -8 ? 2 : elev < -4 ? 1 : 0;
  if (!tier) return;
  for (int i = 0; i < STAR_COUNT; i++) {
    int mag = (i % 5 == 0) ? 1 : (i % 2 == 0) ? 2 : 3;  // 1 = en parlak
    if (mag > tier) continue;
    bool on = ((frameNo + stars[i].ph) % 50) < 44;       // titresim
    uint8_t col;
    if (mag == 1) col = on ? P_WHITE : P_GRAY;
    else if (mag == 2) {
      int k = i % 4;
      col = !on ? P_GRAY : (colored && k == 0) ? P_PRECIP : (colored && k == 1) ? P_CLOUD_L : P_WHITE;
    } else col = on ? P_GRAY : P_SKY3;
    scene.drawPixel(stars[i].x, stars[i].y, col);
    if (mag == 1 && on) {  // en parlaklar: kucuk arti
      scene.drawPixel(stars[i].x - 1, stars[i].y, P_GRAY);
      scene.drawPixel(stars[i].x + 1, stars[i].y, P_GRAY);
      scene.drawPixel(stars[i].x, stars[i].y - 1, P_GRAY);
      scene.drawPixel(stars[i].x, stars[i].y + 1, P_GRAY);
    }
  }
}

static void drawMilkyWay() {
  for (int i = 0; i < MW_COUNT; i++) scene.drawPixel(mwX[i], mwY[i], P_CLOUD_D);
}

// Kayan yildiz: 15-60 sn'de bir, ~0.6 sn suren parlak cizgi + sonen kuyruk
static void updateMeteor(uint32_t now) {
  if (now >= nextMeteor) {
    meteorStart = now;
    nextMeteor = now + (uint32_t)frand(15000, 60000);
    mX = frand(40, SCENE_W - 40);
    mY = frand(6, 60);
    float a = frand(0.35f, 0.7f), dir = frand(0, 1) < 0.5f ? -1 : 1;
    mVX = dir * cosf(a) * 260;
    mVY = sinf(a) * 260;
  }
  uint32_t age = now - meteorStart;
  if (!meteorStart || age > 600) return;
  float s = age / 1000.0f;
  for (int k = 0; k < 16; k++) {
    float b = s - k * 0.006f;
    if (b < 0) break;
    scene.drawPixel((int)(mX + mVX * b), (int)(mY + mVY * b), k < 4 ? P_WHITE : P_GRAY);
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

// Sis: yere yakin yogun, yukari seyrelen, yavasca suruklenen dalgali perdeler.
// Yogunluk 4px'lik hucrelerde iki sinus dalgasinin toplami; hucre ici Bayer
// desenli noktalarla (dithering) cizilir - duz cizgiler yerine yumusak pus.
static void drawFog(float t) {
  const int TOP = 30, CELL = 4;
  for (int y = TOP; y < SCENE_H; y++) {
    float base = (y - TOP) / (float)(SCENE_H - TOP);  // 0 ust, 1 yer
    const uint8_t* row = BAYER4[y & 3];
    for (int cx = 0; cx < SCENE_W; cx += CELL) {
      float w = 0.5f * sinf(cx * 0.015f + t * 0.22f + y * 0.045f)
              + 0.3f * sinf(cx * 0.037f - t * 0.15f + y * 0.021f);
      float d = base * 0.8f + w * 0.3f;
      int th = (int)(constrain(d, 0.0f, 1.0f) * 16);
      if (th <= 0) continue;
      uint8_t col = d > 0.55f ? P_CLOUD_L : P_CLOUD_D;  // yogun yerde acik, seyrekte ince doku
      for (int x = cx; x < cx + CELL; x++)
        if (row[x & 3] < th) scene.drawPixel(x, y, col);
    }
  }
}

static void renderScene(Scene sc, bool day, float dt, float t, uint32_t now) {
  drawSky();
  bool openSky = (sc == SC_CLEAR || sc == SC_PARTLY);
  SkyTime st = skyTime();
  float elev = st.valid ? st.elev : (day ? 30.0f : -30.0f);
  bool sunUp = elev > -2;
  // Yildizlar: gece yagissiz her havada (kapalida bulutlar gectikce ortulur)
  bool precip = (sc == SC_RAIN || sc == SC_SNOW || sc == SC_THUNDER || sc == SC_DRIZZLE || sc == SC_FOG);
  if (!precip) {
    if (sc == SC_CLEAR && elev < -15) drawMilkyWay();
    drawStars(openSky, elev);
    if (openSky && elev < -10) updateMeteor(now);
  }
  // Gok cismi bulutlardan ONCE cizilir: bulutlar gecerken onu ortup acar.
  // BUGFIX: sadece yagmur/kar/firtina haric tutuluyordu - ciseleme (drizzle,
  // kod 51-57) ve sis de "precip" sayildigi halde disarida kaliyordu, %100
  // bulutlu ciselemede bile soluk gunes gorunuyordu (2026-09-28 gozlemlendi,
  // canli kod 55). Simdi TUM yagisli/sisli sahnelerde gok cismi gizli.
  // Gunes/ay gun icinde yay cizerek hareket eder, tepelerin arkasina batar.
  // Siste gok cismi pusun arkasindan soluk disk olarak secilir (sis ustten cizilir)
  if (!precip || sc == SC_FOG) {
    int x, y;
    if (sunUp) {
      sunPos(st, x, y);
      if (openSky) drawSun(x, y, t, elev, sc == SC_CLEAR);
      else drawDimSun(x, y);
    }
    if (elev < 0 && moonPos(st, moonPhase(), x, y)) drawMoon(x, y, moonPhase());
  }
  drawHills(sc != SC_FOG);
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
    // Aktif uyarilar (son 60 dk) haberin onune eklenir
    String pre = trAscii(alertsTickerText());
    if (xSemaphoreTake(newsMutex, MUTEX_TIMEOUT) == pdTRUE) {
      tickerText = pre + NEWS_TICKER_TEXT;
      xSemaphoreGive(newsMutex);
    }
    if (!tickerText.length()) tickerText = pre + "Haberler yukleniyor...";
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

// TLS gerektiren bir islemi (haber, hava durumu, ...) sprite'lari GUVENLE
// gecici serbest birakip calistirir, sonra geri kurar (bkz. asagidaki not).
// Sadece bu task sahibi oldugu icin displayTask'in KENDI icinden cagirilan
// islerde kullanilmali.
static bool animated = false;
static Scene cur = SC_NONE;

static bool withTlsSpritesFreed(std::function<bool()> fn, const char* tag, bool* spriteOkOut = nullptr) {
  bool wasAnimated = animated;
  if (animated) scene.deleteSprite();
  ticker.deleteSprite();
  bool ok = fn();
  ticker.createSprite(TICKER_W, TICKER_H);
  scene.setColorDepth(4);
  // BUGFIX (2026-09-28): TLS'in kendi gecici tahsisleri, bosalttigimiz
  // bloklari baska gorevlerin (hava/durum) o sirada yaptigi kucuk
  // tahsislerle bolebiliyor - ilk deneme basarisiz olabiliyor. Digerleri
  // serbest birakana kadar kisa aralikla birkac kez dene.
  animated = false;
  for (int attempt = 0; attempt < 10 && !animated; attempt++) {
    if (attempt) vTaskDelay(pdMS_TO_TICKS(300));
    animated = scene.createSprite(SCENE_W, SCENE_H) != nullptr;
  }
  if (animated) { setFixedPalette(); cur = SC_NONE; }  // palet sifirlandi, yeniden kur
  bool spriteOk = animated || !wasAnimated;  // sadece ACIK'tan KAPALI'ya duserse sorun
  if (spriteOkOut) *spriteOkOut = spriteOk;
  if (!spriteOk) {
    Serial.printf("[Ekran] UYARI: %s sonrasi sahne geri kurulamadi | bos heap %u, en buyuk blok %u\n",
                  tag, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  drawHeader();
  weatherDisplayDirty = true;  // animated durumu degismis olabilir - guvenli tam yeniden cizim
  return ok;
}

// ============== Uyari banner'i (hava/deprem) ==============
// Sahne sprite'ina (palet indeksi) ya da yedek statik ekrana (565 renk) cizilir.
// Kirmizi <-> koyu zemin arasinda yanip soner; beyaz yazi ikisinde de okunur.
// Sicaklik/ruzgar kutularinin (y 6..86) hemen altindan baslar, haftalik tahmin
// seridini (y 134..180) tamamen kapatir.
static const int BANNER_Y = 90;
static const int BANNER_H = 90;

// Metni maxW'ye sigacak sekilde en fazla 2 satira boler (kelime sinirindan)
static void wrapTwo(const String& s, int maxW, int font, String& l1, String& l2) {
  if (tft.textWidth(s, font) <= maxW) { l1 = s; l2 = ""; return; }
  int cut = s.length();
  while (cut > 1 && tft.textWidth(s.substring(0, cut), font) > maxW) cut--;
  int sp = s.lastIndexOf(' ', cut);
  if (sp > 0) cut = sp;
  l1 = s.substring(0, cut);
  l2 = s.substring(cut);
  l2.trim();
  l2 = fitText(l2, maxW, font);
}

static void drawAlertBanner(TFT_eSPI& g, int oy, bool sprite, const Alert& a, int idx, int n) {
  bool flip = (millis() / 600) & 1;
  uint16_t bg = sprite ? (flip ? P_RED : P_PANEL) : (flip ? 0xF800 : COL_CARD);
  uint16_t fg = sprite ? P_WHITE : TFT_WHITE;
  uint16_t edge = sprite ? P_ORANGE : COL_ORANGE;
  uint16_t dim = sprite ? P_GRAY : COL_GRAY;
  int y = oy + BANNER_Y;
  g.fillRect(0, y, 320, BANNER_H, bg);
  g.drawRect(0, y, 320, BANNER_H, edge);
  g.drawRect(1, y + 1, 318, BANNER_H - 2, edge);
  g.setTextDatum(TL_DATUM);
  g.setTextColor(fg, bg);
  g.drawString(fitText(trAscii(a.title), n > 1 ? 250 : 300, 4), 8, y + 8, 4);
  String l1, l2;
  wrapTwo(trAscii(a.detail), 304, 2, l1, l2);
  g.drawString(l1, 8, y + 42, 2);
  if (l2.length()) g.drawString(l2, 8, y + 60, 2);
  g.setTextColor(dim, bg);
  g.drawString("Onay: HUB web sayfasi", 8, y + 78, 1);
  if (n > 1) {
    g.setTextColor(fg, bg);
    g.setTextDatum(TR_DATUM);
    g.drawString(String(idx + 1) + "/" + String(n), 312, y + 12, 2);
    g.setTextDatum(TL_DATUM);
  }
}

// ============== Ekran gorevi (TFT'nin tek sahibi) ==============
void displayTask(void* pvParameters) {
  // Acilis ekranlarindan kalan setTextSize(2/3) sonraki tum font cizimlerini
  // 2-3 kat buyutuyordu - sifirla
  tft.setTextSize(1);

  ticker.createSprite(TICKER_W, TICKER_H);
  scene.setColorDepth(4);
  animated = scene.createSprite(SCENE_W, SCENE_H) != nullptr;
  Serial.printf("[Ekran] canli sahne: %s | bos heap %u, en buyuk blok %u\n",
                animated ? "ACIK" : "KAPALI (bellek yok, statik ekran)",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (animated) setFixedPalette();

  tft.fillScreen(TFT_BLACK);
  readWeather();
  drawHeader();

  bool curDay = true;
  uint32_t last = millis();
  float t = 0;

  // Uyari durumu (alerts.cpp) - her karede String kopyalamamak icin surume gore onbellek
  Alert view[6];
  int viewN = 0, viewIdx = 0, lastIdx = -1;
  uint32_t viewVer = 0, lastBannerDraw = 0;
  bool bannerShown = false;

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

    // Uyarilar: liste degisince/dondurulen uyari degisince ust seridi guncelle
    if (alertsVersion() != viewVer) {
      viewVer = alertsVersion();
      viewN = alertsSnapshot(view, 6);
      lastIdx = -1;
    }
    viewIdx = viewN ? (now / 4000) % viewN : 0;
    if (viewIdx != lastIdx) {
      lastIdx = viewIdx;
      hdrAlert = viewN ? trAscii(view[viewIdx].title) : String("");
      drawHeaderLeft();
    }
    if (!animated) {
      if (viewN) {
        if (now - lastBannerDraw >= 600) {
          lastBannerDraw = now;
          bannerShown = true;
          drawAlertBanner(tft, SCENE_Y, false, view[viewIdx], viewIdx, viewN);
        }
      } else if (bannerShown) {
        bannerShown = false;
        weatherDisplayDirty = true;  // paneller banner'in altini sildi - yeniden ciz
      }
    }

    // Haber (HTTPS/TLS, AA RSS). BUGFIX (2026-09-28): TLS handshake tek parca
    // ~35-45KB bellek istiyor; sahne+ticker sprite'lari acikken en buyuk bos
    // blok bunun altina dustugu icin surekli basarisiz oluyordu. Sprite'lar
    // sadece bu task'a ait, TLS sirasinda guvenle gecici serbest birakiliyor.
    {
      static unsigned long lastNewsUpdate = 0;
      static bool newsOk = false, spriteOk = true, firstRun = true;
      unsigned long interval = (newsOk && spriteOk) ? NEWS_UPDATE_INTERVAL : 30000UL;
      if (firstRun || now - lastNewsUpdate >= interval) {
        firstRun = false;
        lastNewsUpdate = now;
        newsOk = withTlsSpritesFreed([]() { return fetchNews(); }, "haber", &spriteOk);
      }
    }

    // Hava durumu (HTTPS/TLS, Open-Meteo + gerekirse Nominatim geocode).
    // BUGFIX (2026-09-28): bu istek eskiden ayri bir gorevde (automation.cpp)
    // yapiliyordu - o gorev ekran sprite'larina erisemedigi icin ayni
    // "SSL - Memory allocation failed" hatasini hep aliyordu (canli log ile
    // dogrulandi), otomasyon hicbir zaman guncel hava verisi goremiyordu.
    {
      static unsigned long lastWeatherUpdate = 0;
      static bool spriteOk = true, firstRun = true;
      const unsigned long WEATHER_UPDATE_INTERVAL = 600000UL;  // 10 dakika
      unsigned long interval = spriteOk ? WEATHER_UPDATE_INTERVAL : 30000UL;
      if (firstRun || weatherRefreshRequested || now - lastWeatherUpdate >= interval) {
        firstRun = false;
        weatherRefreshRequested = false;
        lastWeatherUpdate = now;
        withTlsSpritesFreed([]() { forceNtpSync(); updateWeatherFromConfig(); return true; }, "hava", &spriteOk);
      }
    }

    // Deprem sorgusu (AFAD, HTTPS/TLS) - hava/haber ile ayni nedenle burada.
    // Ilk sorgu acilistan ~60sn sonra (haber+hava ile ust uste binmesin).
    {
      static unsigned long lastEq = 0;
      static bool eqOk = false, spriteOk = true, firstRun = true;
      if (firstRun) { firstRun = false; lastEq = now - (ALERT_EQ_INTERVAL_MS - 60000UL); }
      unsigned long interval = (eqOk && spriteOk) ? ALERT_EQ_INTERVAL_MS : 120000UL;
      if (now - lastEq >= interval) {
        lastEq = now;
        eqOk = withTlsSpritesFreed([]() { return fetchEarthquakes(); }, "deprem", &spriteOk);
      }
    }

    if (animated) {
      // Hava demosu: tum hava tipleri sirayla (gercek saatin gokyuzuyle)
      static const Scene WX_SEQ[] = {SC_CLEAR, SC_PARTLY, SC_CLOUDY, SC_FOG, SC_DRIZZLE, SC_RAIN, SC_SNOW, SC_THUNDER};
      static const char* WX_NAME[] = {"Acik", "Parcali", "Kapali", "Sis", "Ciseleme", "Yagmur", "Kar", "Firtina"};
      const int WX_N = sizeof(WX_SEQ) / sizeof(WX_SEQ[0]);
      const uint32_t WX_STEP_MS = 8000;
      if (wxDemoReq) {
        wxDemoReq = false;
        wxDemoStart = now;
        wxDemoIdx = 0;
      }
      if (wxDemoIdx >= 0) {
        wxDemoIdx = (now - wxDemoStart) / WX_STEP_MS;
        if (wxDemoIdx >= WX_N) wxDemoIdx = -1;
      }
      // Demonun son adiminda (Firtina) ornek uyari banner'i/ust serit/alt yazi gorunsun
      {
        static bool demoAlertOn = false;
        bool want = (wxDemoIdx == WX_N - 1);
        if (want != demoAlertOn) { demoAlertOn = want; alertsDemo(want); }
      }
      Scene sc = wxDemoIdx >= 0 ? WX_SEQ[wxDemoIdx]
               : skyDemoMin >= 0 ? SC_CLEAR : wv.ok ? sceneFromCode(wv.code) : SC_CLOUDY;
      static uint32_t lastPalette = 0;
      if (skyDemoReq) {
        skyDemoReq = false;
        float m;
        skyDemoMin = sceneMinutes(m) ? m : 0;
        skyDemoLeft = 24 * 60;
      }
      if (skyDemoMin >= 0) {
        skyDemoMin = fmodf(skyDemoMin + dt * SKY_DEMO_SPEED, 24 * 60);
        skyDemoLeft -= dt * SKY_DEMO_SPEED;
        if (skyDemoLeft <= 0) skyDemoMin = -1;
        lastPalette = 0;  // demoda renkler her karede akar
      }
      if (sc != cur || wv.isDay != curDay) {
        cur = sc;
        curDay = wv.isDay;
        setScenePalette(cur, curDay);
        initScene(cur, curDay);
        lastPalette = now;
      } else if (lastPalette == 0 || now - lastPalette > 20000) {
        // Gok renkleri saate bagli (tan, kizillik, alacakaranlik) - eskiden sadece
        // hava/gun-gece degisince kuruluyordu, kizillik o anki haliyle donup kaliyordu
        setScenePalette(cur, curDay);
        lastPalette = now;
      }
      renderScene(cur, curDay, dt, t, now);
      drawPanels(scene, 0, SPRITE_COLS);
      if (skyDemoMin >= 0) {
        char dm[12];
        snprintf(dm, sizeof(dm), "DEMO %02d:%02d", (int)skyDemoMin / 60, (int)skyDemoMin % 60);
        scene.setTextColor(P_WHITE);
        scene.setTextDatum(TR_DATUM);
        scene.drawString(dm, SCENE_W - 8, WIN_TOP + 2, 2);
        scene.setTextDatum(TL_DATUM);
      }
      if (wxDemoIdx >= 0) {
        String dm = String("DEMO ") + WX_NAME[wxDemoIdx];
        scene.setTextColor(P_WHITE);
        scene.setTextDatum(TR_DATUM);
        scene.drawString(dm, SCENE_W - 8, WIN_TOP + 2, 2);
        scene.setTextDatum(TL_DATUM);
      }
      if (viewN) drawAlertBanner(scene, 0, true, view[viewIdx], viewIdx, viewN);
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
