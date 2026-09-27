# ev-lamba — Merkezi HUB

ESP32 (TFT ekranlı) üzerinde çalışan, evdeki ESP01 tabanlı röle/lamba düğümlerini
tek bir web arayüzünden yöneten merkezi kontrol ünitesi ("Merkezi HUB").

## Özellikler

- WiFiManager ile kurulum (AP: `MerkeziHUB_SETUP`), kayıtlı ağa otomatik bağlanma.
- Web arayüzü (`include/relay_webpage_multi.h` içine gömülü, PROGMEM) üzerinden
  cihaz listesi, röle kontrolü, zaman/gün doğumu-batımı zamanlayıcısı.
- Cihazlara (`ip/status`, `ip/control`, `ip/mac`) HTTP proxy ile erişim — CORS/karışıklık
  olmadan tarayıcıdan tek adresten tüm cihazlar yönetilir.
- Open-Meteo'dan hava durumu (bulut, rüzgar, nem, gün doğumu/batımı) çekip
  hava durumuna göre otomasyon + rüzgar eşiği aşılırsa Telegram uyarısı.
- MQTT (HiveMQ public broker) ile röle durumu paylaşımı.
- TFT ekranda anlık şehir/sıcaklık/nem/rüzgar gösterimi.

## Donanım / Arduino IDE karşılığı

Bu proje daha önce Arduino IDE'de tek `.ino` dosyası olarak yürütülüyordu:

- ESP32 core: 2.0.18 (arduino-esp32 v3.1.0)
- Board: **ESP32 Dev Module**
- PSRAM: **Enable**
- Partition Scheme: **Huge APP**
- TFT: ILI9341 (240x320), pinler `platformio.ini` içindeki `build_flags`'ta birebir
  `D:\Arduino\libraries\TFT_eSPI\User_Setup.h` ile aynı (MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, BL 21).

PlatformIO'ya taşınırken bu ayarlar `platformio.ini` içine build_flags olarak işlendi
ve **`pio run` ile derlemesi doğrulandı** (RAM ~%15, Flash ~%40).

⚠️ **Bilinen pin çakışması:** `ONBOARD_LED_PIN` (2) ile TFT'nin `TFT_DC` pini (2) aynı.
`flashLED()` her HTTP isteğinde bu pini toggluyor — bu muhtemelen ekranda küçük
titreme/flicker'a sebep oluyordur. Dokunulmadı (davranışı değiştirmemek için), ama
ileride ayrı bir GPIO'ya taşınması önerilir.

## Kurulum

1. `include/secrets.h.example` dosyasını `include/secrets.h` olarak kopyala, kendi
   Telegram bot token / chat ID bilgilerini gir. `secrets.h` `.gitignore`'da, repoya girmez.
2. VS Code + PlatformIO eklentisiyle klasörü aç, `pio run` (derle) / `pio run -t upload` (yükle).
3. İlk açılışta ESP `MerkeziHUB_SETUP` adında bir AP açar; oradan gerçek WiFi ağını seç.

## Geçmiş / notlar

- Bu proje `D:\Arduino\ESP\esp32Wroom_Sunucu_v5_6_8` klasöründen (dosya içi sürüm etiketi:
  v5.7.0) buraya taşındı — `D:\Arduino\ESP` altında v1.0'dan v5.6.8'e onlarca eski versiyon
  klasörü hâlâ duruyor, arşiv olarak orada bırakıldı.
- Taşınmadan önce kod **derlenmiyordu**: `handleSaveWeatherConfig()` içinde ArduinoJson v7
  ile uyumsuz bir karşılaştırma vardı (ham `MemberProxy`'yi `!=` ile kopyalamaya çalışıyordu).
  Düzeltildi, davranış değişmedi (bkz. `src/main.cpp` içindeki ilgili yorum satırı).
- `docs/legacy/` altındaki `index*.html` dosyaları artık kullanılmıyor — web arayüzü
  `include/relay_webpage_multi.h` içine taşınmış durumda, bunlar sadece referans için duruyor.
- Eski GitHub reposu `Nedu41/merkezi-hub.io` sadece bu legacy HTML dosyalarını içeriyordu,
  firmware koduna hiç sahip değildi. Bu repo (`ev-lamba`) firmware + web arayüzünü birlikte tutar.
