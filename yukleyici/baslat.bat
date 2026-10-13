@echo off
cd /d "%~dp0"
echo Firmware Yukleyici: http://localhost:8080/  (kapatmak icin bu pencereyi kapatin)
start "" http://localhost:8080/
python -m http.server 8080 --bind 127.0.0.1 || py -m http.server 8080 --bind 127.0.0.1
