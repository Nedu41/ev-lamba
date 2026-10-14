// LC Technology ESP-01 4 kanal role modulu TANI ARACI (v3).
// ESP-01 "LC4R_TEST" adli acik WiFi agi kurar; telefonla baglanip http://192.168.4.1 acin.
// Sayfa: role MCU'sunun ESP-01'e seriden gonderdigi baytlari canli gosterir (hex + metin),
// baud secilir (115200/9600), el sikisma ve role komutlari elle gonderilir.
// Seri hat role MCU'suna bagli oldugu icin Serial hata ayiklama icin KULLANILMAZ; gozlem WiFi uzerinden.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

static ESP8266WebServer web(80);
static String hexLog, ascLog;
static uint32_t baud = 115200, rxCount = 0, lastRxMs = 0;
static bool autoHs = true;
static String win;

static const char EL_SIKISMA[] =
    "WIFI CONNECTED\nWIFI GOT IP\nAT+CIPMUX=1\nAT+CIPSERVER=1,8080\nAT+CIPSTO=360";

static void trim(String& s, size_t n) { if (s.length() > n) s.remove(0, s.length() - n); }

static void gonderHex(const String& h) {
  for (size_t i = 0; i + 1 < h.length(); i += 2) Serial.write((uint8_t)strtol(h.substring(i, i + 2).c_str(), nullptr, 16));
  Serial.flush();
}

static void rolek(uint8_t ch, bool on) {
  uint8_t f[4] = {0xA0, ch, (uint8_t)(on ? 1 : 0), 0};
  f[3] = f[0] + f[1] + f[2];
  Serial.write(f, 4);
  Serial.flush();
}

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><meta name=viewport content="width=device-width,initial-scale=1">
<body style="background:#14141a;color:#dadae4;font:16px system-ui;margin:12px">
<h3 style="color:#a070ff">LC 4 röle tanı</h3>
<div id=st>…</div>
<p>Baud: <button onclick="g('/baud?b=115200')">115200</button> <button onclick="g('/baud?b=9600')">9600</button>
 <button onclick="g('/hs')">El sıkışma gönder</button> <button onclick="g('/clr')">Temizle</button>
 <label><input type=checkbox id=au checked onchange="g('/auto?v='+(this.checked?1:0))"> AT+RST'ye otomatik cevap</label></p>
<p>Röle: <span id=rb></span></p>
<p>Elle hex: <input id=hx value="A00101A2" size=14> <button onclick="g('/tx?h='+hx.value)">Gönder</button></p>
<p>Gelen (hex):</p><pre id=hexo style="background:#1f1f28;padding:8px;white-space:pre-wrap"></pre>
<p>Gelen (metin):</p><pre id=asco style="background:#1f1f28;padding:8px;white-space:pre-wrap"></pre>
<script>
function g(u){fetch(u).then(()=>setTimeout(r,200))}
let b='';for(let i=1;i<=4;i++)b+=`<button onclick="g('/rl?c=${i}&s=1')">${i} AÇ</button> <button onclick="g('/rl?c=${i}&s=0')">${i} KAPAT</button> `;
rb.innerHTML=b;
function r(){fetch('/rx').then(x=>x.json()).then(j=>{
 st.textContent='Baud '+j.baud+' | alınan bayt: '+j.n+' | son alım: '+(j.n?Math.round((j.up-j.last)/1000)+' sn önce':'hiç');
 hexo.textContent=j.hex||'(hiçbir şey gelmedi)';asco.textContent=j.asc||'';})}
setInterval(r,700);r();
</script>)HTML";

void setup() {
  Serial.begin(baud);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("LC4R_TEST");
  web.on("/", []() { web.send_P(200, "text/html; charset=utf-8", PAGE); });
  web.on("/rx", []() {
    String j = "{\"baud\":" + String(baud) + ",\"n\":" + String(rxCount) + ",\"last\":" + String(lastRxMs) + ",\"up\":" + String(millis()) +
               ",\"hex\":\"" + hexLog + "\",\"asc\":\"";
    for (size_t i = 0; i < ascLog.length(); i++) { char c = ascLog[i]; if (c == 34 || c == 92) j += (char)92; if (c == 10) j += (char)92, j += 'n'; else if (c == 13) j += (char)92, j += 'r'; else j += c; }
    j += "\"}";
    web.send(200, "application/json", j);
  });
  web.on("/baud", []() { baud = web.arg("b").toInt(); Serial.end(); Serial.begin(baud); web.send(200, "text/plain", "ok"); });
  web.on("/hs", []() { Serial.print(EL_SIKISMA); Serial.flush(); web.send(200, "text/plain", "ok"); });
  web.on("/clr", []() { hexLog = ascLog = win = ""; rxCount = 0; web.send(200, "text/plain", "ok"); });
  web.on("/auto", []() { autoHs = web.arg("v") == "1"; web.send(200, "text/plain", "ok"); });
  web.on("/tx", []() { gonderHex(web.arg("h")); web.send(200, "text/plain", "ok"); });
  web.on("/rl", []() { rolek(web.arg("c").toInt(), web.arg("s") == "1"); web.send(200, "text/plain", "ok"); });
  web.begin();
}

void loop() {
  while (Serial.available()) {
    uint8_t c = Serial.read();
    rxCount++;
    lastRxMs = millis();
    char b[4];
    snprintf(b, sizeof(b), "%02X ", c);
    hexLog += b;
    ascLog += (c >= 32 && c < 127) || c == '\n' || c == '\r' ? (char)c : '.';
    trim(hexLog, 900);
    trim(ascLog, 300);
    win += (char)c;
    trim(win, 16);
    if (autoHs && win.endsWith("AT+RST\r\n")) { win = ""; Serial.print(EL_SIKISMA); Serial.flush(); }
  }
  web.handleClient();
}
