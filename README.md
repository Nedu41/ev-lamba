# ev-lamba — Merkezi HUB

ESP32 (TFT ekranlı) üzerinde çalışan, evdeki ESP01 tabanlı röle/lamba düğümlerini
tek bir web arayüzünden yöneten merkezi kontrol ünitesi ("Merkezi HUB"). Sürüm: `FW_VERSION` (`include/config.h`).

## Özellikler

- WiFiManager ile kurulum (AP: `MerkeziHUB_SETUP`), kayıtlı ağa otomatik bağlanma.
- Web arayüzü (`include/relay_webpage_multi.h`, PROGMEM): röle kontrolü, saatli zamanlayıcı,
  gündoğumu/günbatımı aksiyonları, hava otomasyonu. Tüm ayarlar değiştiği anda otomatik kaydedilir.
- Tarayıcı ↔ HUB arası WebSocket (`/ws`): röle komutları ve durum güncellemeleri anlık.
  HUB ↔ ESP01 arası HTTP (`/status`, `/control`, `/mac`, `/save`).
- Cihaz keşfi: mDNS (`esp01-XXXXXX.local`) ile ağdaki ESP01'ler taranır, isim verilerek eklenir.
- OTA: HUB (`merkezihub.local`) ve ESP01'ler kablosuz güncellenir.
- Hava durumu: Open-Meteo, model `ecmwf_ifs025`. Bulut/karanlık eşiğine göre röle otomasyonu,
  rüzgar eşiği aşılırsa Telegram uyarısı.
- TFT: hava durumuna göre canlı arka plan (güneş, ay/yıldız, bulut, sis, yağmur, kar, şimşek),
  alt kısımda Anadolu Ajansı dünya haberleri kayan yazısı.
- MQTT (HiveMQ public broker).

## Donanım

- Board: **ESP32 Dev Module**, PSRAM enable, partition `huge_app.csv` (`platformio.ini`).
- TFT: ILI9341 (240x320), pinler `platformio.ini` `build_flags`'ta:
  MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, BL 21.
- GPIO2 hem kart üstü LED hem `TFT_DC` — LED olarak kullanılmaz.

## Kod yapısı

| Dosya | İçerik |
|---|---|
| `src/main.cpp` | `setup()` / `loop()` |
| `include/config.h` | Sürüm, pinler, zaman aralıkları, dosya adları |
| `src/app_state.cpp` | Görevler arası paylaşılan durum (JSON dokümanları, mutex'ler) |
| `src/storage.cpp` | LittleFS: cihaz listesi, zamanlayıcı, hava ayarları |
| `src/devices.cpp` | ESP01 HTTP iletişimi + durum görevi |
| `src/weather.cpp` | Open-Meteo, geocode, NTP |
| `src/automation.cpp` | Bulut otomasyonu, gündoğumu/batımı + saatli zamanlayıcı, rüzgar uyarısı |
| `src/display.cpp` | TFT: canlı hava sahnesi, paneller, haber kayan yazısı |
| `src/web_server.cpp` | AsyncWebServer rotaları, WebSocket, ESP01 proxy worker, mDNS tarama |
| `src/mqtt_link.cpp`, `src/telegram.cpp` | MQTT, Telegram |

## Kurulum

1. `include/secrets.h.example` → `include/secrets.h`, Telegram bot token / chat ID gir
   (`secrets.h` `.gitignore`'da).
2. Derle: `pio run`. İlk yükleme USB ile: `pio run -t upload`.
3. İlk açılışta ESP `MerkeziHUB_SETUP` AP'si açar; oradan WiFi ağını seç.

### Kablosuz güncelleme (OTA)

- HUB: `pio run -t upload --upload-port merkezihub.local`
- ESP01 (proje: `ESP01_AI_Rolekontrol_AI_Mac_ID_v2_0_3/` (bu depoda), PlatformIO, `src/`):
  `pio run -t upload --upload-port esp01-XXXXXX.local` (XXXXXX = MAC'in son 6 hanesi)
- USB ile flaşlamadan sonraki **ilk** OTA "Invalid bootstrapping state" hatası verirse:
  cihazın gücünü bir kez kesip ver (USB flaş sonrası GPIO0 strap durumu).

## Notlar

- Geri dönüş: `pre-async-migration` git etiketi, WebSocket/modül geçişinden önceki tek dosyalı sürüm.
- `docs/legacy/` altındaki `index*.html` dosyaları kullanılmıyor, referans için duruyor.
- Eski sürüm klasörleri (`esp32Wroom_Sunucu_v*`) bu depoda değil, arşivde.
