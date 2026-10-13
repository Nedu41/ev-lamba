# Firmware Yükleyici

Tarayıcıdan (Chrome/Edge, USB) ESP cihazlarına firmware yükler. Cihaz **modelini** seç, USB ile bağla, **Yükle**'ye bas, açılan pencereden **COM portunu** seç.

- **Yerelde:** `baslat.bat` dosyasına çift tıkla (`http://localhost:8080`; Python gerekir). Sayfa dosya olarak açılırsa liste görünür ama Yükle çalışmaz.
- **İnternetten:** GitHub Pages ile bu klasör yayınlanabilir (Settings → Pages → `main` / kök, adres `.../ev-lamba/yukleyici/`).
- **Cihazlar:** merkezi cihazlar (Elecrow CrowPanel 7", ESP32 + ILI9341 TFT) ve röle düğümleri (ESP-01/07, NodeMCU, D1 mini, Sonoff Basic, ESP32 DevKit, ESP32-C3, ESP32-S3). Liste: `cihazlar.json`.
- **Yeni imaj:** dosyayı `firmware/` altına koy, `cihazlar.json`'u güncelle, `python tools/uret.py` (manifestleri ve `cihazlar.js`'i üretir).
- ESP Web Tools `vendor/` altına gömülüdür (Apache-2.0). "Temizle" seçeneği kartı tamamen siler.
