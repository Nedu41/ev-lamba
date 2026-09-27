// ===================== YEREL HTML (PROGMEM) =====================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Merkezi Hub v5.5.3 (YEREL)</title>
<meta http-equiv="Cache-Control" content="max-age=31536000, must-revalidate"> 

<style>
@keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.7; } 100% { opacity: 1; } }
.btn.pulsing { animation: pulse 1s infinite; }
.btn.success { background: #4CAF50 !important; color: white !important; }
.btn.error { background: #cf6679 !important; color: white !important; }
.btn.warning { background: #ff9800 !important; color: black !important; }

body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #121212; color: #eee; margin: 0; padding-bottom: 20px; }
.header { background: #222; color: #03dac6; padding: 12px; text-align: center; font-size: 1.1em; font-weight: bold; border-bottom: 2px solid #333; display: flex; justify-content: space-between; align-items: center; }
.header span { font-size: 0.8em; color: #888; }
.nav { background: #1a1a1a; padding: 8px; display: flex; justify-content: center; gap: 15px; border-bottom: 1px solid #333; }
.nav a { color: #888; text-decoration: none; font-size: 0.9em; padding: 5px 10px; border-radius: 4px; transition: 0.3s; }
.nav a.active { background: #3700b3; color: #fff; }

.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(160px, 1fr)); gap: 10px; padding: 10px; max-width: 800px; margin: 0 auto; }
.card { background: #1e1e1e; border: 1px solid #333; border-radius: 10px; padding: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.5); position: relative; display: flex; flex-direction: column; transition: transform 0.2s; }
.card-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; border-bottom: 1px solid #333; padding-bottom: 5px; }
.dev-name { font-weight: bold; font-size: 0.95em; color: #fff; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 100px; }
.status-dot { width: 10px; height: 10px; border-radius: 50%; background: #555; display: inline-block; margin-right: 5px; }
.online-dot { background: #03dac6; box-shadow: 0 0 5px #03dac6; }
.offline-dot { background: #cf6679; }
.timer-icon-btn { background: transparent; border: none; color: #bb86fc; cursor: pointer; font-size: 1.2em; padding: 0 5px; }

.relay-row { display: flex; align-items: center; justify-content: space-between; margin-bottom: 8px; background: #252525; padding: 5px; border-radius: 6px; }
.relay-label { font-size: 0.8em; color: #aaa; font-weight: bold; width: 25px; }
.btn { flex-grow: 1; padding: 8px 5px; border: none; border-radius: 6px; font-weight: bold; font-size: 0.85em; cursor: pointer; transition: 0.2s; text-transform: uppercase; letter-spacing: 0.5px; }
.on { background: #03dac6; color: #000; }
.off { background: #333; color: #ccc; border: 1px solid #444; }
.off:hover { background: #444; }
.sched-info { font-size: 0.65em; color: #bb86fc; text-align: center; margin-top: -3px; margin-bottom: 5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }

.section { margin-top: 5px; background: #2a2a2a; padding: 8px; border-radius: 8px; display: none; border: 1px solid #444; font-size: 0.85em; }
.days { display: flex; justify-content: space-between; gap: 2px; margin: 10px 0; }
.days label { width: 25px; height: 25px; line-height: 25px; background: #444; color: #eee; border-radius: 4px; text-align: center; cursor: pointer; font-size: 0.75em; font-weight: bold; }
.days input[type="checkbox"]:checked + label { background: #bb86fc; color: #000; }
.days input[type="checkbox"] { display: none; }

input[type="time"], input[type="text"], input[type="number"], select { 
    background: #111; border: 1px solid #444; color: #fff; padding: 5px; border-radius: 4px; width: 100%; box-sizing: border-box; 
}
.action-btns { display: flex; gap: 5px; margin-top: 10px; }

.settings-container { max-width: 600px; margin: 10px auto; padding: 15px; background: #1f1f1f; border-radius: 8px; }
table { width: 100%; border-collapse: collapse; }
td { padding: 8px; border-bottom: 1px solid #333; }

.github-info { 
    background: #1a1a1a; 
    padding: 5px 10px; 
    border-radius: 4px; 
    font-size: 0.8em; 
    color: #888; 
    text-align: center;
    margin-top: 10px;
    border: 1px solid #333;
}
.github-info.active { border-color: #4CAF50; color: #4CAF50; }
.github-info.inactive { border-color: #cf6679; color: #cf6679; }

/* WiFi bilgi kutusu */
.wifi-info {
    font-size: 0.7em;
    color: #888;
    margin-bottom: 8px;
    padding: 4px;
    background: #1a1a1a;
    border-radius: 4px;
    text-align: center;
    border: 1px solid #333;
    transition: all 0.3s ease;
}

.wifi-info:hover {
    background: #252525;
    border-color: #555;
}

/* Tablo stilleri */
#devicesTable {
    width: 100%;
    border-collapse: collapse;
    margin-top: 10px;
}

#devicesTable th {
    background: #2a2a2a;
    color: #bb86fc;
    padding: 12px 8px;
    text-align: left;
    font-size: 0.9em;
    border-bottom: 2px solid #333;
}

#devicesTable td {
    padding: 10px 8px;
    border-bottom: 1px solid #333;
    vertical-align: middle;
}

#devicesTable tr:hover {
    background: #252525;
}

/* MAC ID kutusu */
.mac-box {
    background: #1a1a1a;
    padding: 4px 8px;
    border-radius: 4px;
    border: 1px solid #333;
    font-family: 'Courier New', monospace;
    font-size: 0.85em;
    color: #bb86fc;
    letter-spacing: 0.5px;
}

/* Cihaz işlem butonları */
.device-action-btns {
    display: flex;
    gap: 5px;
    margin-top: 5px;
    justify-content: center;
}

.device-action-btn {
    padding: 4px 8px;
    font-size: 0.75em;
    border-radius: 4px;
    border: none;
    cursor: pointer;
    transition: 0.2s;
}

.device-action-btn.eeprom {
    background: #ff9800;
    color: black;
}

.device-action-btn.wifi {
    background: #2196F3;
    color: white;
}

.device-action-btn:hover {
    opacity: 0.9;
}

/* RSSI Sütunu Stilleri */
.rssi-value {
    font-family: 'Courier New', monospace;
    font-weight: bold;
    font-size: 0.85em;
    text-align: center;
    padding: 3px 8px;
    border-radius: 4px;
    display: inline-block;
    min-width: 70px;
}

.rssi-excellent { 
    background: rgba(76, 175, 80, 0.2); 
    color: #4CAF50;
    border: 1px solid #4CAF50;
}
.rssi-good { 
    background: rgba(255, 193, 7, 0.2); 
    color: #FFC107;
    border: 1px solid #FFC107;
}
.rssi-fair { 
    background: rgba(255, 152, 0, 0.2); 
    color: #FF9800;
    border: 1px solid #FF9800;
}
.rssi-poor { 
    background: rgba(244, 67, 54, 0.2); 
    color: #f44336;
    border: 1px solid #f44336;
}
.rssi-offline { 
    background: rgba(117, 117, 117, 0.2); 
    color: #757575;
    border: 1px solid #757575;
}

/* RSSI İkonları */
.rssi-icon {
    font-size: 1.1em;
    margin-right: 5px;
}

/* CİHAZLAR SAYFASI İÇİN YENİ STİLLER */
.devices-container {
    max-width: 100%;
    margin: 10px auto;
    padding: 15px;
    background: #1f1f1f;
    border-radius: 8px;
    overflow-x: auto;
}

.devices-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 15px;
    flex-wrap: wrap;
    gap: 10px;
}

.devices-header h3 {
    margin: 0;
    color: #bb86fc;
}

.devices-header-buttons {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
}

/* Cihazlar Tablosu için responsive tasarım */
.devices-table-wrapper {
    overflow-x: auto;
    border-radius: 8px;
    border: 1px solid #333;
    background: #222;
}

#devicesTable {
    width: 100%;
    min-width: 800px;
    border-collapse: collapse;
    margin: 0;
}

#devicesTable th {
    background: #2a2a2a;
    color: #bb86fc;
    padding: 12px 8px;
    text-align: left;
    font-size: 0.85em;
    border-bottom: 2px solid #333;
    white-space: nowrap;
    position: sticky;
    top: 0;
    z-index: 10;
}

#devicesTable td {
    padding: 10px 8px;
    border-bottom: 1px solid #333;
    vertical-align: middle;
    font-size: 0.85em;
}

#devicesTable tr:hover {
    background: #252525;
}

/* Hücre özel stilleri */
.cell-compact {
    max-width: 120px;
    min-width: 100px;
}

.cell-ip {
    min-width: 120px;
    max-width: 150px;
    font-family: 'Courier New', monospace;
    font-size: 0.9em;
}

.cell-name {
    min-width: 120px;
    max-width: 150px;
}

.cell-mac {
    min-width: 140px;
    max-width: 160px;
}

.cell-rssi {
    min-width: 100px;
    max-width: 120px;
}

.cell-status {
    min-width: 90px;
    max-width: 100px;
}

.cell-actions {
    min-width: 140px;
    max-width: 160px;
}

.cell-delete {
    min-width: 60px;
    max-width: 70px;
    text-align: center;
}

/* Input alanları için optimize stil */
.compact-input {
    width: 100%;
    min-width: 100px;
    padding: 6px 8px;
    background: #111;
    border: 1px solid #444;
    color: #fff;
    border-radius: 4px;
    font-size: 0.85em;
    box-sizing: border-box;
}

.compact-input:focus {
    outline: none;
    border-color: #bb86fc;
}

/* MAC ID kutusu */
.mac-box-compact {
    background: #1a1a1a;
    padding: 4px 8px;
    border-radius: 4px;
    border: 1px solid #333;
    font-family: 'Courier New', monospace;
    font-size: 0.8em;
    color: #bb86fc;
    letter-spacing: 0.5px;
    display: inline-block;
    max-width: 140px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

/* Cihaz işlem butonları */
.device-action-btns-compact {
    display: flex;
    flex-wrap: wrap;
    gap: 4px;
    justify-content: flex-start;
}

.device-action-btn-compact {
    padding: 4px 6px;
    font-size: 0.7em;
    border-radius: 4px;
    border: none;
    cursor: pointer;
    transition: 0.2s;
    white-space: nowrap;
}

.device-action-btn-compact.eeprom {
    background: #ff9800;
    color: black;
}

.device-action-btn-compact.wifi {
    background: #2196F3;
    color: white;
}

.device-action-btn-compact:hover {
    opacity: 0.9;
    transform: translateY(-1px);
}

/* RSSI göstergesi */
.rssi-value-compact {
    font-family: 'Courier New', monospace;
    font-weight: bold;
    font-size: 0.8em;
    text-align: center;
    padding: 4px 6px;
    border-radius: 4px;
    display: inline-block;
    min-width: 70px;
    white-space: nowrap;
}

/* Durum göstergesi */
.status-indicator {
    display: flex;
    align-items: center;
    gap: 6px;
    white-space: nowrap;
}

.status-dot-indicator {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    display: inline-block;
}

.status-online {
    background: #03dac6;
    box-shadow: 0 0 5px #03dac6;
}

.status-offline {
    background: #cf6679;
}

/* Ekleme/Silme butonları */
.add-device-btn {
    background: #4CAF50 !important;
    color: white !important;
    padding: 8px 12px !important;
}

.delete-device-btn {
    background: #cf6679 !important;
    color: white !important;
    padding: 6px 10px !important;
    font-size: 0.9em;
}

.save-devices-btn {
    background: #bb86fc !important;
    color: black !important;
    padding: 8px 16px !important;
    font-weight: bold;
}

/* Mobil için responsive düzenlemeler */
@media (max-width: 768px) {
    .devices-container {
        padding: 10px;
        margin: 5px;
    }
    
    .devices-header {
        flex-direction: column;
        align-items: stretch;
    }
    
    .devices-header-buttons {
        justify-content: center;
    }
    
    #devicesTable th,
    #devicesTable td {
        padding: 8px 6px;
        font-size: 0.8em;
    }
    
    .cell-compact {
        min-width: 90px;
        max-width: 110px;
    }
    
    .compact-input {
        font-size: 0.8em;
        padding: 4px 6px;
    }
    
    .device-action-btns-compact {
        flex-direction: column;
    }
    
    .device-action-btn-compact {
        width: 100%;
        text-align: center;
    }
    
    .mac-box-compact {
        max-width: 120px;
        font-size: 0.75em;
    }
    
    .rssi-value-compact {
        min-width: 60px;
        font-size: 0.75em;
    }
}

@media (max-width: 480px) {
    #devicesTable {
        min-width: 600px;
    }
    
    .devices-header h3 {
        font-size: 1.1em;
    }
    
    .compact-input {
        font-size: 0.75em;
    }
}

/* Scrollbar stil */
.devices-table-wrapper::-webkit-scrollbar {
    height: 8px;
}

.devices-table-wrapper::-webkit-scrollbar-track {
    background: #1a1a1a;
    border-radius: 4px;
}

.devices-table-wrapper::-webkit-scrollbar-thumb {
    background: #444;
    border-radius: 4px;
}

.devices-table-wrapper::-webkit-scrollbar-thumb:hover {
    background: #555;
}

/* Hücre araç ipuçları */
[title] {
    position: relative;
}

[title]:hover::after {
    content: attr(title);
    position: absolute;
    bottom: 100%;
    left: 50%;
    transform: translateX(-50%);
    background: #333;
    color: #fff;
    padding: 5px 10px;
    border-radius: 4px;
    font-size: 0.8em;
    white-space: nowrap;
    z-index: 100;
    pointer-events: none;
    opacity: 0.9;
}

/* İkonlar */
.icon-small {
    font-size: 0.9em;
    margin-right: 3px;
}

/* Responsive için */
@media (max-width: 768px) {
    .settings-container {
        padding: 10px;
        margin: 5px;
    }
    
    #devicesTable th, 
    #devicesTable td {
        padding: 8px 5px;
        font-size: 0.85em;
    }
    
    #devicesTable .btn {
        padding: 4px 8px;
        font-size: 0.75em;
    }
    
    .device-action-btns {
        flex-direction: column;
        gap: 3px;
    }
    
    .wifi-info {
        font-size: 0.65em;
        padding: 3px;
    }
    
    .card {
        min-width: 140px;
    }
    
    .rssi-value {
        min-width: 60px;
        font-size: 0.8em;
        padding: 2px 6px;
    }
}

</style>
</head>
<body>

<div class="header">
    <div>MERKEZİ HUB v5.5.3</div>
    <span id="clock">00:00</span>
</div>

<div class="nav">
  <a href="/" id="nav1" onclick="route('/');return false;">Panel</a>
  <a href="/settings" id="nav2" onclick="route('/settings');return false;">Cihazlar</a>
  <a href="/weather" id="nav3" onclick="route('/weather');return false;">Oto-Hava</a>
</div>

<div id="content">Yükleniyor...</div>

<div id="githubStatus" class="github-info inactive">GitHub: YEREL MOD</div>

<script>
let DEVICES = [], STATUS = {}, CURRENT_WEATHER = {}, WEATHER_CONFIG = {};
let lastToggleTime = 0;
const daysName = ["Pt","Sa","Ça","Pe","Cu","Ct","Pz"];

// Saat Göstergesi
setInterval(() => {
    const d = new Date();
    document.getElementById('clock').innerText = d.toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'});
}, 1000);

// Navigasyon
function route(path) { 
    history.pushState(null, '', path); 
    navigate(); 
}

// API Fonksiyonları
function fetchDevices(){ 
    return fetch("/get_devices").then(r=>r.json()).then(d=>DEVICES=d); 
}

function fetchStatus(){ 
    return fetch("/fetch_status").then(r=>r.json()).then(d=>{ 
        STATUS = d.devices || {}; 
        CURRENT_WEATHER = d.weather || {}; 
        WEATHER_CONFIG = d.weather_config || {}; 
        
        // GitHub durumunu güncelle
        const githubStatus = document.getElementById('githubStatus');
        if (githubStatus && d.github_mode) {
            githubStatus.textContent = `GitHub: ${d.github_mode.toUpperCase()} MOD`;
            githubStatus.className = d.github_mode === 'github' ? 'github-info active' : 'github-info inactive';
            if (d.github_size) {
                githubStatus.textContent += ` (${Math.round(d.github_size/1024)}KB)`;
            }
        }
        
        return d; 
    }); 
}

// GitHub Kontrolü
function githubControl(action) {
    fetch(`/github_control?action=${action}`)
        .then(r => r.text())
        .then(response => {
            alert(response);
            if (action === 'update' || action === 'enable' || action === 'disable') {
                setTimeout(() => location.reload(), 1000);
            }
        });
}

// Geri bildirim
function feedbackSuccess(btnId) {
    const btn = document.getElementById(btnId); 
    if(!btn) return;
    const t = btn.textContent; 
    const c = btn.className;
    btn.textContent = '✓ OK'; 
    btn.className = 'btn success';
    setTimeout(() => { 
        btn.textContent = t; 
        btn.className = c; 
    }, 2000);
}

// RSSI değerine göre sınıf belirleme fonksiyonu
function getRSSIClass(rssi) {
    if (rssi === undefined || rssi === null || rssi === 0 || rssi === "OFFLINE") {
        return "rssi-offline";
    }
    
    const rssiValue = parseInt(rssi);
    
    if (rssiValue >= -50) {
        return "rssi-excellent";
    } else if (rssiValue >= -60) {
        return "rssi-good";
    } else if (rssiValue >= -70) {
        return "rssi-fair";
    } else {
        return "rssi-poor";
    }
}

// RSSI değerine göre ikon belirleme fonksiyonu
function getRSSIIcon(rssi) {
    if (rssi === undefined || rssi === null || rssi === 0 || rssi === "OFFLINE") {
        return "📴";
    }
    
    const rssiValue = parseInt(rssi);
    
    if (rssiValue >= -50) {
        return "📶"; // Mükemmel sinyal
    } else if (rssiValue >= -60) {
        return "📶"; // İyi sinyal
    } else if (rssiValue >= -70) {
        return "📶"; // Orta sinyal
    } else if (rssiValue >= -80) {
        return "📶"; // Zayıf sinyal
    } else {
        return "📶"; // Çok zayıf sinyal
    }
}

// RSSI değerini formatlama fonksiyonu
function formatRSSI(rssi) {
    if (rssi === undefined || rssi === null || rssi === 0) {
        return "OFFLINE";
    }
    if (rssi === "OFFLINE") {
        return "Çevrimdışı";
    }
    return `${rssi} dBm`;
}

// --- ANA PANEL RENDER ---
function renderControl() {
    let html = `<div class="grid">`;
    
    if(!DEVICES.length) {
        html += `<div style="text-align:center;width:100%;padding:20px;color:#cf6679">
                    <h4>❌ Cihaz Yok</h4>
                    <p>Ayarlar > Cihazlar sekmesinden cihaz ekleyin.</p>
                    <button class="btn" onclick="route('/settings')" style="margin-top:10px">Cihaz Ekle</button>
                 </div>`;
    }
    
    DEVICES.forEach((dev, i) => {
        html += `
        <div class="card">
            <div class="card-header">
                <div style="display:flex;align-items:center">
                    <span class="status-dot" id="dot_${i}"></span>
                    <span class="dev-name" title="${dev.ip}">${dev.name}</span>
                </div>
                <button class="timer-icon-btn" onclick="openSect(${i})" title="Zamanlayıcı Ayarları">🕒</button>
            </div>

            <!-- WiFi Sinyal Bilgisi -->
            <div class="wifi-info" id="wifi_${i}" style="font-size:0.7em;color:#888;margin-bottom:8px;padding:4px;background:#1a1a1a;border-radius:4px;text-align:center">
                📶 WiFi: Yükleniyor...
            </div>

            <div class="relay-row">
                <div class="relay-label">R1</div>
                <button class="btn off" id="b1_${i}" onclick="toggle(${i},1)">...</button>
            </div>
            <div class="sched-info" id="p1_${i}"></div>

            <div class="relay-row">
                <div class="relay-label">R2</div>
                <button class="btn off" id="b2_${i}" onclick="toggle(${i},2)">...</button>
            </div>
            <div class="sched-info" id="p2_${i}"></div>

            <div class="section" id="sec_${i}">
                <div style="display:flex;gap:10px;justify-content:center;margin-bottom:10px;">
                    <label><input type="radio" name="r${i}" value="1" checked onchange="loadProgValues(${i})"> Röle 1</label>
                    <label><input type="radio" name="r${i}" value="2" onchange="loadProgValues(${i})"> Röle 2</label>
                </div>
                
                <div style="display:flex;gap:5px;align-items:center">
                    <input type="time" id="t1_${i}" value="08:00"> 
                    <span>-</span> 
                    <input type="time" id="t2_${i}" value="18:00">
                </div>

                <div class="days">
                    ${daysName.map((d,j)=>`<input type="checkbox" id="d${i}_${j}"><label for="d${i}_${j}">${d}</label>`).join("")}
                </div>
                
                <div style="margin:8px 0; padding:6px; background:#1a1a1a; border-radius:6px; font-size:0.75em">

<div style="margin:8px 0; padding:6px; background:#1a1a1a; border-radius:6px; font-size:0.75em">

  <div style="margin-bottom:6px">
    ☀️ Gündoğumu:
    <select id="sunrise_act_${i}">
      <option value="0">Pasif</option>
      <option value="ON">AÇ</option>
      <option value="OFF">KAPAT</option>
    </select>
  </div>

  <div>
    🌙 Günbatımı:
    <select id="sunset_act_${i}">
      <option value="0">Pasif</option>
      <option value="ON">AÇ</option>
      <option value="OFF">KAPAT</option>
    </select>
  </div>

</div>

                 </div>

                <div class="action-btns">
                    <button class="btn on" id="saveProgBtn_${i}" onclick="saveProg(${i})">KAYDET</button>
                    <button class="btn off" onclick="clearProg(${i})">SİL</button>
                </div>
            </div>
        </div>`;
    });
    
    html += `</div>`;
    
    // GitHub kontrol butonları
    html += `
    <div style="text-align:center;margin-top:20px;">
        <button class="btn" onclick="githubControl('update')" style="background:#333;margin:5px">🔄 GitHub Güncelle</button>
        <button class="btn" onclick="githubControl('enable')" style="background:#4CAF50;margin:5px">✅ GitHub Aktif</button>
        <button class="btn" onclick="githubControl('disable')" style="background:#cf6679;margin:5px">❌ GitHub Pasif</button>
        <button class="btn" onclick="githubControl('status')" style="background:#2196F3;margin:5px">ℹ️ GitHub Durumu</button>
    </div>`;
    
    document.getElementById("content").innerHTML = html;
}

// Durum Güncelleme
function update() {
    if(location.pathname !== "/") return;
    
    fetchStatus().then(() => {
        DEVICES.forEach((dev, i) => {
            const s = STATUS[dev.name] || {r1_state:"OFFLINE", r2_state:"OFFLINE"};
            
            // Online/Offline Durumu
            const online = s.r1_state !== "OFFLINE";
            const dot = document.getElementById("dot_"+i);
            if(dot) {
                dot.className = "status-dot " + (online ? "online-dot" : "offline-dot");
            }

            // WiFi Sinyal Bilgisi Güncelleme
            const wifiInfo = document.getElementById("wifi_"+i);
            if(wifiInfo) {
                if(online && s.wifi_rssi) {
                    const rssi = s.wifi_rssi;
                    const quality = s.wifi_quality || 0;
                    const status = s.wifi_status || "Bilinmiyor";
                    
                    // Sinyal gücüne göre renk belirleme
                    let signalColor = "#cf6679"; // Kırmızı (zayıf)
                    if(rssi >= -50) signalColor = "#4CAF50"; // Yeşil (mükemmel)
                    else if(rssi >= -60) signalColor = "#8BC34A"; // Açık yeşil (çok iyi)
                    else if(rssi >= -70) signalColor = "#FF9800"; // Turuncu (iyi)
                    else if(rssi >= -80) signalColor = "#FFC107"; // Sarı (zayıf)
                    
                    // Sinyal çubuğu ikonu
                    let signalIcon = "📶";
                    if(rssi >= -50) signalIcon = "📶";
                    else if(rssi >= -60) signalIcon = "📶";
                    else if(rssi >= -70) signalIcon = "📶";
                    else if(rssi >= -80) signalIcon = "📶";
                    else signalIcon = "📶";
                    
                    wifiInfo.innerHTML = `
                        <span style="color:${signalColor}">
                            ${signalIcon} ${rssi}dBm (${quality}%) - ${status}
                        </span>
                    `;
                    wifiInfo.title = `SSID: ${s.wifi_ssid || 'Bilinmiyor'}\nSinyal Gücü: ${rssi}dBm\nKalite: ${quality}%\nDurum: ${status}`;
                } else {
                    wifiInfo.innerHTML = '<span style="color:#cf6679">📶 Çevrimdışı</span>';
                    wifiInfo.title = 'Cihaz çevrimdışı';
                }
            }

            // Röle Durumları
            for(let r = 1; r <= 2; r++) {
                const state = s[`r${r}_state`] || "OFFLINE";
                const btn = document.getElementById("b"+r+"_"+i);
                const prog = document.getElementById("p"+r+"_"+i);

                if(state === "OFFLINE") {
                    if(btn) { 
                        btn.textContent = "❌ HATA"; 
                        btn.className = "btn error"; 
                        btn.disabled = true; 
                    }
                } else {
                    if(btn) {
                        btn.textContent = state === "ON" ? "✅ AÇIK" : "⭕ KAPALI";
                        btn.className = "btn " + (state === "ON" ? "on" : "off");
                        btn.disabled = false;
                    }
                    // Zamanlayıcı Bilgisi
                    const active = s[`sched_active_r${r}`] === "true";
                    const start = s[`sched_start_r${r}`];
                    if(prog) {
                        prog.innerText = active ? `🕒 ${start} - ${s[`sched_end_r${r}`]}` : "";
                    }
                }

let sunInfo = "";
const sr = localStorage.getItem(`sun_${dev.name}_r${r}_rise`);
const ss = localStorage.getItem(`sun_${dev.name}_r${r}_set`);

if(sr && sr !== "0") sunInfo += `☀️${sr} `;
if(ss && ss !== "0") sunInfo += `🌙${ss}`;

if(prog && sunInfo) prog.innerText += " " + sunInfo;

            }
        });
    });
}

// Zamanlayıcı Bölümü Aç/Kapa
function openSect(i) { 
    const sec = document.getElementById("sec_"+i);
    const isVis = sec.style.display === "block";
    
    // Diğer bölümleri kapat
    document.querySelectorAll('.section').forEach(s => s.style.display = 'none');
    
    // İsteneni aç/kapa
    if(!isVis) { 
        sec.style.display = "block"; 
        loadProgValues(i); 
    }
}

// Seçili Röle
function getRelay(i) { 
    return document.querySelector(`input[name="r${i}"]:checked`).value; 
}

// Gün Maskesi
function getMask(i) { 
    let m = 0; 
    for(let j = 0; j < 7; j++) {
        if(document.getElementById("d"+i+"_"+j).checked) {
            m |= 1 << j;
        }
    }
    return m; 
}

// Röle Toggle
function toggle(i, r) {
    const dev = DEVICES[i]; 
    const btn = document.getElementById("b"+r+"_"+i);
    const originalText = btn.textContent;
    const originalClass = btn.className;
    
    // Optimistik UI
    btn.textContent = "⏳";
    btn.disabled = true;
    
    // Manuel kontrol sinyali
    fetch("/manual_control");
    
    // Röle durumunu belirle
    const action = originalText.includes("AÇIK") ? "off" : "on";
    
    fetch(`/proxy?ip=${encodeURIComponent(dev.ip)}&url=${encodeURIComponent("/control?id="+r+"&action="+action)}`)
        .then(response => {
            if (response.ok) {
                btn.textContent = action === "on" ? "✅ AÇIK" : "⭕ KAPALI";
                btn.className = action === "on" ? "btn on" : "btn off";
                setTimeout(update, 500);
            } else {
                throw new Error("HTTP hatası");
            }
        })
        .catch(error => {
            btn.textContent = originalText;
            btn.className = originalClass;
            btn.disabled = false;
            alert(`Hata: ${dev.name} ile iletişim kurulamadı.`);
        });
}

// Zamanlayıcı Kaydet
function saveProg(i) {
    const dev = DEVICES[i];
    const r = getRelay(i);
    const start = document.getElementById("t1_"+i).value;
    const end = document.getElementById("t2_"+i).value;
    const [sh, sm] = start.split(":");
    const [eh, em] = end.split(":");
    const mask = getMask(i);

const sunriseAct = document.getElementById(`sunrise_act_${i}`).value;
const sunsetAct  = document.getElementById(`sunset_act_${i}`).value;


    if(!mask) {
        alert("Lütfen en az bir gün seçin!");
        return;
    }

    // LocalStorage
    localStorage.setItem(`p_${dev.name}_r${r}_s`, start);
    localStorage.setItem(`p_${dev.name}_r${r}_e`, end);
    localStorage.setItem(`p_${dev.name}_r${r}_m`, mask);
localStorage.setItem(`sun_${dev.name}_r${r}_rise`, sunriseAct);
localStorage.setItem(`sun_${dev.name}_r${r}_set`, sunsetAct);


    // ✅ TEK url TANIMI
const url =
 `/save?relay_id=${r}` +
 `&start_h=${sh}&start_m=${sm}` +
 `&end_h=${eh}&end_m=${em}` +
 `&days=${mask}&active=1` +
 `&sunrise_act=${sunriseAct}` +
 `&sunset_act=${sunsetAct}`;


    fetch(`/proxy?ip=${encodeURIComponent(dev.ip)}&url=${encodeURIComponent(url)}`)
        .then(response => {
            if(response.ok) {
                feedbackSuccess('saveProgBtn_'+i);
                setTimeout(update, 1000);
            } else {
                alert("Zamanlayıcı kaydedilemedi!");
            }
        })
        .catch(() => alert("İletişim hatası!"));
}


// Zamanlayıcı Temizle
function clearProg(i) {
    const dev = DEVICES[i];
    const r = getRelay(i);

    // Local Storage'dan sil
    localStorage.removeItem(`p_${dev.name}_r${r}_s`);
    localStorage.removeItem(`p_${dev.name}_r${r}_e`);
    localStorage.removeItem(`p_${dev.name}_r${r}_m`);
localStorage.removeItem(`sun_${dev.name}_r${r}_rise`);
localStorage.removeItem(`sun_${dev.name}_r${r}_set`);


    fetch(`/proxy?ip=${encodeURIComponent(dev.ip)}&url=${encodeURIComponent(`/save?relay_id=${r}&days=0&active=0`)}`)
        .then(() => {
            alert("Zamanlayıcı iptal edildi!");
            setTimeout(update, 800);
        })
        .catch(() => alert("İletişim hatası!"));
}

// Zamanlayıcı Değerlerini Yükle
function loadProgValues(i) {
    const dev = DEVICES[i];
    const r = getRelay(i);
    const s = localStorage.getItem(`p_${dev.name}_r${r}_s`);
    const e = localStorage.getItem(`p_${dev.name}_r${r}_e`);
    const m = localStorage.getItem(`p_${dev.name}_r${r}_m`);
const sr = localStorage.getItem(`sun_${dev.name}_r${r}_rise`) || "0";
const ss = localStorage.getItem(`sun_${dev.name}_r${r}_set`)  || "0";

document.getElementById(`sunrise_act_${i}`).value = sr;
document.getElementById(`sunset_act_${i}`).value  = ss;


    document.getElementById(`t1_${i}`).value = s || "08:00";
    document.getElementById(`t2_${i}`).value = e || "18:00";
    
    const mask = m ? parseInt(m) : 127; // Varsayılan: tüm günler
    for(let j = 0; j < 7; j++) {
        document.getElementById(`d${i}_${j}`).checked = (mask & (1 << j)) !== 0;
    }
}

// --- AYARLAR SAYFASI ---
function renderSettings() {
    fetchStatus().then(() => {
        let html = `
        <div class="devices-container">
            <div class="devices-header">
                <h3>📱 Cihaz Yönetimi</h3>
                <div class="devices-header-buttons">
                    <button class="btn warning" onclick="restartHub()" title="Hub'ı yeniden başlat">🔄 Yeniden Başlat</button>
                    <button class="btn add-device-btn" onclick="add()" title="Yeni cihaz ekle">➕ Yeni Cihaz</button>
                    <button class="btn save-devices-btn" onclick="saveDevices()" title="Tüm değişiklikleri kaydet">💾 Kaydet</button>
                </div>
            </div>
            
            <div class="devices-table-wrapper">
                <table id="devicesTable">
                    <thead>
                        <tr>
                            <th class="cell-compact cell-name">Cihaz Adı</th>
                            <th class="cell-compact cell-ip">IP Adresi</th>
                            <th class="cell-compact cell-mac">MAC ID</th>
                            <th class="cell-compact cell-rssi">WiFi Sinyal</th>
                            <th class="cell-compact cell-status">Durum</th>
                            <th class="cell-compact cell-actions">İşlemler</th>
                            <th class="cell-delete">Sil</th>
                        </tr>
                    </thead>
                    <tbody>`;
        
        DEVICES.forEach((d, i) => {
            const status = STATUS[d.name] || {};
            const mac = status.mac || "?";
            const online = status.r1_state !== "OFFLINE";
            
            // WiFi sinyal bilgisi
            const rssi = status.wifi_rssi;
            const rssiIcon = getRSSIIcon(rssi);
            const rssiClass = getRSSIClass(rssi);
            const formattedRSSI = formatRSSI(rssi);
            const wifiQuality = status.wifi_quality || 0;
            const wifiSSID = status.wifi_ssid || 'Bilinmiyor';
            
            html += `
                    <tr>
                        <td class="cell-name">
                            <input type="text" id="n${i}" value="${d.name}" 
                                   class="compact-input" 
                                   placeholder="Cihaz Adı"
                                   title="Cihaz adını düzenle">
                        </td>
                        <td class="cell-ip">
                            <input type="text" id="i${i}" value="${d.ip}" 
                                   class="compact-input" 
                                   placeholder="192.168.1.x"
                                   title="IP adresini düzenle">
                        </td>
                        <td class="cell-mac">
                            <div class="mac-box-compact" title="${mac}">${mac}</div>
                            <button onclick="refreshMac(${i})" 
                                    class="btn" 
                                    style="font-size:0.7em;padding:2px 5px;margin-top:3px;width:100%">
                                    🔄 MAC Yenile
                            </button>
                        </td>
                        <td class="cell-rssi">
                            <div class="rssi-value-compact ${rssiClass}" 
                                 title="WiFi: ${wifiSSID}
Sinyal: ${formattedRSSI}
Kalite: ${wifiQuality}%">
                                <span class="rssi-icon">${rssiIcon}</span>${formattedRSSI}
                                ${wifiQuality ? `<br><small>(${wifiQuality}%)</small>` : ''}
                            </div>
                            <button onclick="refreshRSSI(${i})" 
                                    class="btn" 
                                    style="font-size:0.7em;padding:2px 5px;margin-top:3px;width:100%">
                                    🔄 Sinyal Yenile
                            </button>
                        </td>
                        <td class="cell-status">
                            <div class="status-indicator">
                                <span class="status-dot-indicator ${online ? 'status-online' : 'status-offline'}"></span>
                                <span style="color:${online ? '#0F0' : '#F00'}">
                                    ${online ? '🟢 Çevrimiçi' : '🔴 Çevrimdışı'}
                                </span>
                            </div>
                        </td>
                        <td class="cell-actions">
                            <div class="device-action-btns-compact">
                                <button class="device-action-btn-compact eeprom" 
                                        onclick="deviceEepromClear(${i})" 
                                        title="Cihaz EEPROM'unu sıfırla">
                                    <span class="icon-small">💾</span> EEPROM Temizle
                                </button>
                                <button class="device-action-btn-compact wifi" 
                                        onclick="deviceWifiClear(${i})" 
                                        title="Cihaz WiFi ayarlarını sıfırla">
                                    <span class="icon-small">📶</span> WiFi Temizle
                                </button>
                            </div>
                        </td>
                        <td class="cell-delete">
                            <button class="delete-device-btn" 
                                    onclick="del(${i})" 
                                    title="${d.name} cihazını sil">
                                🗑️
                            </button>
                        </td>
                    </tr>`;
        });
        
        html += `</tbody>
                </table>
            </div>
            
            <!-- Alt Bilgi -->
            <div style="margin-top: 15px; padding: 10px; background: #252525; border-radius: 6px; font-size: 0.8em; color: #888;">
                <div>📊 <b>${DEVICES.length}</b> cihaz kayıtlı</div>
                <div style="margin-top: 5px; font-size: 0.75em;">
                    💡 İpucu: Tabloyu yatay olarak kaydırarak tüm sütunları görebilirsiniz.
                </div>
            </div>
        </div>`;
        
        document.getElementById("content").innerHTML = html;
    });
}

// RSSI Yenileme
function refreshRSSI(deviceIndex) {
    const device = DEVICES[deviceIndex];
    const rssiElement = document.querySelector(`tr:nth-child(${deviceIndex+1}) .rssi-value-compact`);
    
    if (rssiElement) {
        const originalText = rssiElement.textContent;
        const originalClass = rssiElement.className;
        
        rssiElement.textContent = "⏳ Yenileniyor...";
        rssiElement.className = "rssi-value-compact";
        
        // Cihazdan RSSI bilgisini yeniden çek
        fetch(`/proxy?ip=${encodeURIComponent(device.ip)}&url=${encodeURIComponent("/status")}`)
            .then(response => {
                if (response.ok) {
                    return response.json();
                } else {
                    throw new Error("Cihaza ulaşılamadı");
                }
            })
            .then(data => {
                const rssi = data.wifi_rssi;
                const rssiIcon = getRSSIIcon(rssi);
                const rssiClass = getRSSIClass(rssi);
                const formattedRSSI = formatRSSI(rssi);
                const wifiQuality = data.wifi_signal || 0;
                
                rssiElement.innerHTML = `
                    <span class="rssi-icon">${rssiIcon}</span>${formattedRSSI}
                    ${wifiQuality ? `<br><small>(${wifiQuality}%)</small>` : ''}
                `;
                rssiElement.className = `rssi-value-compact ${rssiClass}`;
                rssiElement.title = `WiFi Sinyal Gücü: ${formattedRSSI}${wifiQuality ? ' - Kalite: ' + wifiQuality + '%' : ''}`;
            })
            .catch(error => {
                rssiElement.textContent = "❌ Hata";
                rssiElement.className = "rssi-value-compact rssi-offline";
                rssiElement.title = "Sinyal bilgisi alınamadı";
                
                setTimeout(() => {
                    rssiElement.innerHTML = originalText;
                    rssiElement.className = originalClass;
                }, 2000);
            });
    }
}

// MAC ID Yenileme
function refreshMac(deviceIndex) {
    const device = DEVICES[deviceIndex];
    const macElement = document.querySelector(`tr:nth-child(${deviceIndex+1}) .mac-box-compact`);
    
    if (macElement) {
        const originalText = macElement.textContent;
        macElement.textContent = "⏳ Yenileniyor...";
        macElement.style.color = "#FF9800";
        
        fetchStatus().then(() => {
            const status = STATUS[device.name] || {};
            const newMacId = status.mac || "Bulunamadı";
            
            macElement.textContent = newMacId;
            macElement.style.color = status.mac !== "OFFLINE" && status.mac !== "NO_MAC" ? '#bb86fc' : '#666';
        }).catch(() => {
            macElement.textContent = "❌ Hata";
            macElement.style.color = "#cf6679";
            setTimeout(() => {
                macElement.textContent = originalText;
                macElement.style.color = '#bb86fc';
            }, 2000);
        });
    }
}

// Cihaz EEPROM Temizleme
function deviceEepromClear(deviceIndex) {
    const device = DEVICES[deviceIndex];
    
    fetch(`/proxy?ip=${encodeURIComponent(device.ip)}&url=${encodeURIComponent("/eeprom_clr")}`)
        .then(response => {
            if (response.ok) {
                alert(`✅ ${device.name} cihazının EEPROM'u başarıyla sıfırlandı.\nCihaz yeniden başlatılacak.`);
                // Durumu güncelle
                setTimeout(() => fetchStatus(), 3000);
            } else {
                throw new Error("EEPROM temizleme başarısız");
            }
        })
        .catch(error => {
            alert(`❌ Hata: ${device.name} cihazının EEPROM'u sıfırlanamadı.\nCihaz çevrimdışı olabilir.`);
        });
}

// Cihaz WiFi Temizleme
function deviceWifiClear(deviceIndex) {
    const device = DEVICES[deviceIndex];
    
    fetch(`/proxy?ip=${encodeURIComponent(device.ip)}&url=${encodeURIComponent("/wifi_clr")}`)
        .then(response => {
            if (response.ok) {
                alert(`✅ ${device.name} cihazının WiFi ayarları sıfırlandı.\nCihaz WiFi ayarlama moduna geçti.`);
                // Durumu güncelle
                setTimeout(() => fetchStatus(), 3000);
            } else {
                throw new Error("WiFi temizleme başarısız");
            }
        })
        .catch(error => {
            alert(`❌ Hata: ${device.name} cihazının WiFi ayarları sıfırlanamadı.\nCihaz çevrimdışı olabilir.`);
        });
}

function add() { 
    DEVICES.push({name: `Cihaz_${DEVICES.length+1}`, ip: "192.168.1."}); 
    renderSettings(); 
}

function del(idx) { 
    DEVICES.splice(idx, 1); 
    renderSettings(); 
}

function saveDevices() {
    const arr = [];
    for(let j = 0; j < DEVICES.length; j++) {
        arr.push({
            name: document.getElementById("n"+j).value.trim(),
            ip: document.getElementById("i"+j).value.trim()
        });
    }
    
    fetch("/save_devices", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(arr)
    }).then(r => {
        if (r.ok) {
            feedbackSuccess('saveDevBtn');
            // Sayfayı yeniden yükle
            setTimeout(() => renderSettings(), 500);
        }
    });
}

function restartHub() { 

        fetch("/restart_hub");
        setTimeout(() => alert("Yeniden başlatılıyor..."), 500);

}

function resetWifi() { 
         fetch("/reset_wifi");
        setTimeout(() => alert("WiFi sıfırlanıyor..."), 500);
}

function handleTestTelegram() {
    fetch("/test_telegram")
        .then(() => alert("Test mesajı gönderildi! Telegram'ı kontrol edin."))
        .catch(() => alert("Test mesajı gönderilemedi!"));
}

// --- HAVA DURUMU SAYFASI ---
function renderWeather() {
    fetchStatus().then(() => {
        const c = WEATHER_CONFIG;
        const w = CURRENT_WEATHER;
        
        // Durum Mesajı
        let statusMessage = "✅ Sistem Aktif";
        let statusColor = "#03dac6";
        let statusIcon = "✅";
        
        if (w.description === "FETCH_ERROR") {
            statusMessage = "❌ Hava verisi alınamadı";
            statusColor = "#cf6679";
            statusIcon = "❌";
        } else if (!w.temp && !w.humidity) {
            statusMessage = "⏳ Hava durumu yükleniyor...";
            statusColor = "#bb86fc";
            statusIcon = "⏳";
        }
        
        // Rüzgar Bilgisi
        let windInfo = "-";
        if (w.wind_speed) {
            windInfo = `${w.wind_speed} Km/s`;
            if (w.wind_direction && w.wind_direction !== "-") {
                windInfo += ` ${w.wind_direction}`;
            }
        }
        
        // Bulut Skoru
        let scoreInfo = "";
        if (w.lightness_score && w.lightness_score > 0) {
            const score = w.lightness_score;
            let scoreText = "";
            let scoreColor = "";
            
            switch(score) {
                case 1:
                    scoreText = "AÇIK HAVA (Bulut ≤%25)";
                    scoreColor = "#4CAF50";
                    break;
                case 2:
                    scoreText = "PARÇALI BULUTLU (%25-95)";
                    scoreColor = "#FF9800";
                    break;
                case 3:
                    scoreText = "KAPALI/GECE (%95+)";
                    scoreColor = "#2196F3";
                    break;
                default:
                    scoreText = `SKOR: ${score}`;
                    scoreColor = "#9C27B0";
            }
            
            scoreInfo = `
            <div style="background:#1a1a1a;padding:15px;border-radius:8px;margin-bottom:20px;border-left:5px solid ${scoreColor}">
                <div style="font-size:1.2em;font-weight:bold;color:${scoreColor};margin-bottom:5px">
                    🎯 BULUT SKORU: ${score}
                </div>
                <div style="font-size:0.9em;color:#ccc;font-weight:500">
                    ${scoreText}
                </div>
                <div style="font-size:0.8em;color:#888;margin-top:8px">
                    ${c.threshold ? `📏 Eşik Değer: ${c.threshold}` : ''}
                </div>
            </div>`;
        }
        
        let html = `
        <div class="settings-container">
            <h3>🌤️ Oto Meteoroloji Kontrol</h3>
            
            <!-- Hava Durumu Bilgisi -->
            <div style="background:#222;padding:15px;border-radius:8px;margin-bottom:20px">
                <div style="font-size:1.1em;font-weight:bold;color:#03dac6;margin-bottom:10px">
                    ${w.city_name || c.city || '-'}
                </div>
                <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px">
                    <div>🌡️ Sıcaklık: <b>${w.temp || '-'}°C</b></div>
                    <div>💧 Nem: <b>%${w.humidity || '-'}</b></div>
                    <div>☁️ Bulut: <b>%${w.clouds || '-'}</b></div>
                    <div>🌬️ Rüzgar: <b>${windInfo}</b></div>
                </div>
                <div style="margin-top:10px;padding-top:10px;border-top:1px solid #333">
                    🌅 Gün Doğumu: <b>${w.sunrise_time || '-'}</b><br>
                    🌇 Gün Batımı: <b>${w.sunset_time || '-'}</b>
                </div>
            </div>
            
            ${scoreInfo}
            
            <!-- Durum Mesajı -->
            <div style="background:#1a1a1a;padding:10px;border-radius:5px;margin-bottom:20px;border:2px solid ${statusColor};text-align:center">
                <span style="color:${statusColor};font-weight:bold;font-size:1.1em">
                    ${statusIcon} ${statusMessage}
                </span>
            </div>
            
            <!-- Ayarlar Formu -->
            <table>
                <tr><td>Aktif:</td><td><input type="checkbox" id="w_active" ${c.active?'checked':''}></td></tr>
                <tr><td>Cihaz:</td><td><select id="w_dev">${DEVICES.map(d=>`<option value="${d.name}" ${c.device_name===d.name?'selected':''}>${d.name}</option>`).join('')}</select></td></tr>
                <tr><td>Röle:</td><td><select id="w_rel"><option value="1" ${c.relay_id==1?'selected':''}>Röle 1</option><option value="2" ${c.relay_id==2?'selected':''}>Röle 2</option></select></td></tr>
                <tr><td>İşlem:</td><td><select id="w_act"><option value="ON" ${c.action=='ON'?'selected':''}>AÇ</option><option value="OFF" ${c.action=='OFF'?'selected':''}>KAPAT</option></select></td></tr>
                <tr><td>Eşik:</td><td><select id="w_thr"><option value="1" ${c.threshold==1?'selected':''}>1-Az Bulutlu</option><option value="2" ${c.threshold==2?'selected':''}>2-Parçalı</option><option value="3" ${c.threshold==3?'selected':''}>3-Kapalı/Gece</option></select></td></tr>
                <tr><td>Rüzgar Eşiği (Km/s):</td><td><input type="number" step="0.1" id="w_wind_threshold" placeholder="15.0" value="${c.wind_threshold||'15.0'}"></td></tr>
                <tr><td>Şehir:</td><td><input type="text" id="w_city" placeholder="İstanbul" value="${c.city||''}"></td></tr>
                <tr><td>İlçe:</td><td><input type="text" id="w_district" placeholder="Kadıköy" value="${c.district||''}"></td></tr>
                <tr><td>Mahalle:</td><td><input type="text" id="w_neigh" placeholder="Opsiyonel" value="${c.neighbourhood||''}"></td></tr>
            </table>
            
            <br>
            
            <!-- Butonlar -->
            <div style="display:flex;flex-direction:column;gap:10px">
                <button class="btn on" id="saveWeatherBtn" onclick="saveWeather()">💾 Ayarları Kaydet</button>
                <button class="btn" id="refreshWeatherBtn" onclick="refreshWeather()" style="background:#2196F3">🔄 Hava Durumunu Güncelle</button>
            </div>
        </div>`;
        
        document.getElementById("content").innerHTML = html;
    });
}

function saveWeather() {
    const cfg = {
        active: document.getElementById('w_active').checked,
        device_name: document.getElementById('w_dev').value,
        relay_id: parseInt(document.getElementById('w_rel').value),
        threshold: parseInt(document.getElementById('w_thr').value),
        action: document.getElementById('w_act').value,
        wind_threshold: parseFloat(document.getElementById('w_wind_threshold').value),
        city: document.getElementById('w_city').value.trim(),
        district: document.getElementById('w_district').value.trim(),
        neighbourhood: document.getElementById('w_neigh').value.trim(),
        latitude: "",
        longitude: ""
    };
    
    const saveBtn = document.getElementById('saveWeatherBtn');
    const originalText = saveBtn.textContent;
    
    saveBtn.disabled = true;
    saveBtn.textContent = "⏳ Kaydediliyor...";
    
    fetch("/save_weather_config", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(cfg)
    })
    .then(r => {
        if(r.ok) {
            saveBtn.textContent = "✅ Kaydedildi";
            saveBtn.style.background = "#4CAF50";
            setTimeout(() => {
                saveBtn.textContent = originalText;
                saveBtn.style.background = "";
                saveBtn.disabled = false;
                location.reload();
            }, 2000);
        } else {
            saveBtn.textContent = "❌ Hata";
            saveBtn.style.background = "#cf6679";
            setTimeout(() => {
                saveBtn.textContent = originalText;
                saveBtn.style.background = "";
                saveBtn.disabled = false;
            }, 3000);
        }
    })
    .catch(() => {
        saveBtn.textContent = "❌ Bağlantı Hatası";
        saveBtn.style.background = "#cf6679";
        setTimeout(() => {
            saveBtn.textContent = originalText;
            saveBtn.style.background = "";
            saveBtn.disabled = false;
        }, 3000);
    });
}

function refreshWeather() {
    const refreshBtn = document.getElementById('refreshWeatherBtn');
    const originalText = refreshBtn.textContent;
    
    refreshBtn.disabled = true;
    refreshBtn.textContent = "⏳ Güncelleniyor...";
    
    fetch("/save_weather_config", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({refresh: true})
    })
    .then(r => {
        if(r.ok) {
            refreshBtn.textContent = "✅ Güncellendi";
            refreshBtn.style.background = "#4CAF50";
            setTimeout(() => {
                refreshBtn.textContent = originalText;
                refreshBtn.style.background = "";
                refreshBtn.disabled = false;
                location.reload();
            }, 2000);
        } else {
            refreshBtn.textContent = "❌ Hata";
            refreshBtn.style.background = "#cf6679";
            setTimeout(() => {
                refreshBtn.textContent = originalText;
                refreshBtn.style.background = "";
                refreshBtn.disabled = false;
            }, 3000);
        }
    })
    .catch(() => {
        refreshBtn.textContent = "❌ Bağlantı Hatası";
        refreshBtn.style.background = "#cf6679";
        setTimeout(() => {
            refreshBtn.textContent = originalText;
            refreshBtn.style.background = "";
            refreshBtn.disabled = false;
        }, 3000);
    });
}

// --- NAVİGASYON ---
function navigate() {
    const p = location.pathname;
    
    // Aktif menüyü güncelle
    document.querySelectorAll('.nav a').forEach(a => a.className = '');
    if(p === "/") document.getElementById("nav1").className = "active";
    else if(p === "/settings") document.getElementById("nav2").className = "active";
    else if(p === "/weather") document.getElementById("nav3").className = "active";

    // İlgili sayfayı render et
    if(p === "/settings") {
        fetchDevices().then(renderSettings);
    } else if(p === "/weather") {
        fetchDevices().then(renderWeather);
    } else {
        fetchDevices().then(() => {
            renderControl();
            update();
        });
    }
}

// Sayfa Yüklendiğinde
window.onload = function() {
    window.onpopstate = navigate;
    navigate();
    setInterval(update, 2000); // Her 2 saniyede bir durum güncelle
};
</script>
</body>
</html>
)rawliteral";