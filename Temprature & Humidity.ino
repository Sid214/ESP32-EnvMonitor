/*
=============================================================
  ESP32 Professional Environment Monitor v3.0
  Hardware : ESP32 | DHT11 on GPIO4 | SSD1306 OLED (I2C)
  Libraries: Adafruit SSD1306 | Adafruit GFX | DHT by Adafruit
=============================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include <Preferences.h>
#include <math.h>

// ── WiFi ──────────────────────────────────────────────────
const char* ssid     = "POCO F6";
const char* password = "8928800230";

// ── Hardware (DO NOT CHANGE — matches your wiring) ────────
#define DHTPIN  4
#define DHTTYPE DHT11

DHT              dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
WebServer        server(80);
Preferences      prefs;

// ── Sensor readings ───────────────────────────────────────
float tempC = 0.0, tempF = 0.0;
float humidity = 0.0;
float heatIdx  = 0.0, dewPt = 0.0;

// ── Session statistics ────────────────────────────────────
float sesMinT =  999.0, sesMaxT = -999.0;
float sesMinH =  999.0, sesMaxH = -999.0;

// ── Alert thresholds (saved to flash) ─────────────────────
float limMinT = 18.0, limMaxT = 27.0;
float limMinH = 40.0, limMaxH = 60.0;
String envMode = "serverroom";

// ── Ring-buffer history ───────────────────────────────────
#define HIST 30
float tHist[HIST], hHist[HIST];
int   hPtr = 0, hFill = 0;

// ── Flags & timing ────────────────────────────────────────
bool sensorOK = false, wifiOK = false, blinkOn = false;
unsigned long lastRead = 0, lastBlink = 0;
bool emaInit = false;
float emaT = 0.0, emaH = 0.0;
const float EMA_A = 0.3;  // smoothing factor (0.0-1.0, lower=smoother)

// ═════════════════════════════════════════════════════════
//  HELPER FUNCTIONS
// ═════════════════════════════════════════════════════════
float calcHeatIndex(float t, float h) {
  if (t < 27.0f) return t;
  return -8.78469475556f + 1.61139411f*t + 2.33854883889f*h
         - 0.14611605f*t*h   - 0.01230809050f*t*t
         - 0.01642482778f*h*h + 0.00221732770f*t*t*h
         + 0.00072546f*t*h*h  - 0.00000358f*t*t*h*h;
}

float calcDewPoint(float t, float h) {
  float a=17.27f, b=237.7f;
  float al = (a*t/(b+t)) + logf(h/100.0f);
  return (b*al)/(a-al);
}

bool isAlert() {
  return (tempC<limMinT || tempC>limMaxT || humidity<limMinH || humidity>limMaxH);
}

String getAlertMsg() {
  String m = "";
  if (tempC > limMaxT)    m += "HIGH TEMP: "   + String(tempC,1)    + "C  ";
  if (tempC < limMinT)    m += "LOW TEMP: "    + String(tempC,1)    + "C  ";
  if (humidity > limMaxH) m += "HIGH HUM: "    + String(humidity,1) + "%  ";
  if (humidity < limMinH) m += "LOW HUM: "     + String(humidity,1) + "%";
  m.trim();
  return m;
}

void applyPreset(const String& k) {
  if      (k=="serverroom"){limMinT=18;limMaxT=27;limMinH=40;limMaxH=60;}
  else if (k=="greenhouse"){limMinT=20;limMaxT=30;limMinH=60;limMaxH=80;}
  else if (k=="office")    {limMinT=20;limMaxT=25;limMinH=40;limMaxH=60;}
  else if (k=="winecellar"){limMinT=10;limMaxT=16;limMinH=60;limMaxH=70;}
  else if (k=="bedroom")   {limMinT=16;limMaxT=22;limMinH=40;limMaxH=60;}
}

void savePrefs() {
  prefs.begin("em", false);
  prefs.putString("env", envMode);
  prefs.putFloat("mnt", limMinT); prefs.putFloat("mxt", limMaxT);
  prefs.putFloat("mnh", limMinH); prefs.putFloat("mxh", limMaxH);
  prefs.end();
}

String envLabel() {
  if (envMode=="serverroom") return "Server Room";
  if (envMode=="greenhouse") return "Greenhouse";
  if (envMode=="office")     return "Office";
  if (envMode=="winecellar") return "Wine Cellar";
  if (envMode=="bedroom")    return "Bedroom";
  return "Custom";
}

// ═════════════════════════════════════════════════════════
//  OLED — all full words, both C and F
// ═════════════════════════════════════════════════════════
void oledMsg(const char* l1, const char* l2="", const char* l3="", const char* l4="", const char* l5="") {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  int y=0;
  display.setCursor(0,y); display.println(l1); y+=10;
  display.setCursor(0,y); display.println(l2); y+=10;
  display.setCursor(0,y); display.println(l3); y+=10;
  display.setCursor(0,y); display.println(l4); y+=10;
  display.setCursor(0,y); display.println(l5);
  display.display();
}

void oledUpdate() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  char buf[22];

  // ── Row 0: IP address (centered) ──
  display.setTextSize(1);
  String ipStr = wifiOK ? WiFi.localIP().toString() : "No WiFi";
  int ipW = ipStr.length() * 6;
  display.setCursor((128 - ipW) / 2, 0);
  display.print(ipStr);

  // ── Divider line ──
  display.drawLine(0, 10, 127, 10, WHITE);

  if (sensorOK) {
    // ── Left side: Temperature ──
    display.setCursor(0, 14);
    display.print("Temperature");

    snprintf(buf, 12, "%.1f C", tempC);
    display.setCursor(0, 25);
    display.print(buf);

    snprintf(buf, 12, "%.1f F", tempF);
    display.setCursor(0, 35);
    display.print(buf);

    // ── Vertical divider ──
    display.drawLine(68, 12, 68, 46, WHITE);

    // ── Right side: Humidity ──
    display.setCursor(72, 14);
    display.print("Humidity");

    snprintf(buf, 12, "%.1f %%", humidity);
    display.setCursor(72, 25);
    display.print(buf);
  } else {
    display.setCursor(0, 20);
    display.print("Sensor: ERROR!");
  }

  // ── Bottom: Alert if out of range ──
  if (sensorOK && isAlert()) {
    display.drawLine(0, 48, 127, 48, WHITE);
    if (blinkOn) {
      display.setCursor(30, 52);
      display.print("!! ALERT !!");
    }
  } else if (sensorOK) {
    display.drawLine(0, 48, 127, 48, WHITE);
    display.setCursor(28, 52);
    display.print("All Normal");
  }

  display.display();
}

// ═════════════════════════════════════════════════════════
//  HTML PAGE  (stored in flash)
// ═════════════════════════════════════════════════════════
const char PAGE[] PROGMEM = R"ESP32MON(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Temperature & Humidity Monitoring</title>
<style>
  :root {
    --bg: #f4f7f6;
    --card: #ffffff;
    --text: #2d3436;
    --text-muted: #636e72;
    --accent: #0984e3;
    --danger: #d63031;
    --danger-bg: #ff7675;
    --warning: #e1b12c;
    --success: #00b894;
    --border: #dfe6e9;
    --shadow: 0 10px 20px rgba(0,0,0,0.05);
  }
  [data-theme="dark"] {
    --bg: #0f111a;
    --card: #1e212b;
    --text: #f5f6fa;
    --text-muted: #a4b0be;
    --accent: #3742fa;
    --danger: #ff4757;
    --danger-bg: #ff6b81;
    --warning: #feca57;
    --success: #1dd1a1;
    --border: #2f3542;
    --shadow: 0 10px 30px rgba(0,0,0,0.5);
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: 'Segoe UI', system-ui, sans-serif; background: var(--bg); color: var(--text); transition: background 0.3s, color 0.3s; padding-bottom: 40px; }
  .header { display: flex; justify-content: space-between; align-items: center; padding: 20px 5%; background: var(--card); box-shadow: var(--shadow); border-bottom: 1px solid var(--border); transition: 0.3s; }
  .title { font-size: 1.4rem; font-weight: 800; display: flex; align-items: center; gap: 10px; letter-spacing: -0.5px; }
  .header-controls { display: flex; align-items: center; gap: 15px; }
  .badge { background: var(--accent); color: white; padding: 6px 14px; border-radius: 20px; font-size: 0.75rem; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; }
  .theme-btn { background: var(--bg); border: 1px solid var(--border); color: var(--text); border-radius: 50%; width: 40px; height: 40px; cursor: pointer; font-size: 1.2rem; display: flex; align-items: center; justify-content: center; padding: 0; line-height: 1; transition: 0.3s; }
  .theme-btn:hover { background: var(--border); }
  
  .alert-banner { display: none; background: var(--danger); color: white; padding: 15px 5%; font-weight: bold; justify-content: space-between; align-items: center; animation: pulse-danger 1s infinite alternate; }
  @keyframes pulse-danger { from { background: var(--danger); } to { background: var(--danger-bg); } }
  .dismiss-btn { background: rgba(0,0,0,0.3); border: none; color: white; padding: 8px 16px; border-radius: 8px; cursor: pointer; font-weight: 700; transition: 0.2s; }
  .dismiss-btn:hover { background: rgba(0,0,0,0.5); transform: scale(1.05); }

  .container { max-width: 1000px; margin: 30px auto; padding: 0 20px; display: grid; gap: 25px; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); }
  .card { background: var(--card); border-radius: 20px; padding: 25px; box-shadow: var(--shadow); border: 1px solid var(--border); transition: 0.3s; }
  .card:hover { transform: translateY(-3px); box-shadow: 0 15px 35px rgba(0,0,0,0.1); }
  [data-theme="dark"] .card:hover { box-shadow: 0 15px 35px rgba(0,0,0,0.6); }
  .card-title { font-size: 0.9rem; color: var(--text-muted); font-weight: 700; margin-bottom: 20px; display: flex; justify-content: space-between; align-items: center; text-transform: uppercase; letter-spacing: 1.5px; border-bottom: 2px solid var(--bg); padding-bottom: 12px; }
  
  .gauges-wrapper { display: flex; gap: 20px; flex-wrap: wrap; }
  .gauge-box { flex: 1; min-width: 150px; background: var(--bg); border-radius: 16px; padding: 20px 10px; display: flex; flex-direction: column; align-items: center; border: 1px solid var(--border); position: relative; transition: 0.3s; }
  .gauge-svg { width: 100%; max-width: 180px; overflow: visible; }
  .gauge-val-overlay { position: absolute; top: 55%; left: 50%; transform: translate(-50%, -50%); text-align: center; width: 100%; }
  .main-val { font-size: 2.2rem; font-weight: 900; color: var(--text); letter-spacing: -1px; display: flex; justify-content: center; align-items: center; gap: 4px; }
  .trend-icon { font-size: 1.2rem; }
  .trend-up { color: var(--danger); }
  .trend-down { color: var(--accent); }
  .sub-val { font-size: 0.85rem; color: var(--text-muted); font-weight: 600; margin-top: 2px; }
  .gauge-label { font-size: 0.8rem; font-weight: 700; color: var(--text-muted); margin-bottom: 10px; text-transform: uppercase; letter-spacing: 1px; }

  .stats-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top: 20px; }
  .stat-item { background: var(--bg); border: 1px solid var(--border); border-radius: 12px; padding: 15px; text-align: center; transition: 0.2s; }
  .stat-item:hover { border-color: var(--accent); transform: translateY(-2px); }
  .stat-label { font-size: 0.7rem; color: var(--text-muted); font-weight: 700; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 5px; }
  .stat-value { font-size: 1.3rem; font-weight: 800; color: var(--text); }
  
  .env-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(100px, 1fr)); gap: 10px; margin-bottom: 20px; }
  .env-btn { background: var(--bg); border: 2px solid var(--border); border-radius: 12px; padding: 12px 5px; color: var(--text); font-weight: 700; cursor: pointer; transition: 0.2s; display: flex; flex-direction: column; align-items: center; gap: 5px; font-family: inherit; }
  .env-icon { font-size: 1.5rem; }
  .env-btn span.lbl { font-size: 0.8rem; }
  .env-limits { font-size: 0.65rem; color: var(--text-muted); font-weight: 600; margin-top: 2px; }
  .env-btn.active { border-color: var(--accent); background: rgba(55, 66, 250, 0.1); color: var(--accent); transform: scale(1.02); }
  .env-btn:hover:not(.active) { border-color: var(--text-muted); transform: translateY(-2px); }

  .custom-panel { display: none; background: var(--bg); padding: 20px; border-radius: 16px; border: 1px solid var(--border); animation: slideDown 0.3s ease-out; }
  .custom-panel.open { display: block; }
  @keyframes slideDown { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }
  .slider-group { margin-bottom: 20px; }
  .slider-header { display: flex; justify-content: space-between; font-size: 0.8rem; font-weight: 700; color: var(--text-muted); margin-bottom: 10px; text-transform: uppercase; letter-spacing: 0.5px; }
  .slider-val-badge { background: var(--accent); color: white; padding: 3px 8px; border-radius: 6px; font-size: 0.8rem; }
  input[type=range] { width: 100%; height: 6px; background: var(--border); border-radius: 3px; outline: none; appearance: none; }
  input[type=range]::-webkit-slider-thumb { appearance: none; width: 18px; height: 18px; border-radius: 50%; background: var(--accent); cursor: pointer; transition: 0.2s; box-shadow: 0 2px 5px rgba(0,0,0,0.3); }
  input[type=range]::-webkit-slider-thumb:hover { transform: scale(1.2); }
  .btn-primary { width: 100%; background: var(--accent); color: white; border: none; padding: 14px; border-radius: 12px; font-weight: 800; cursor: pointer; font-size: 1rem; transition: 0.3s; box-shadow: 0 4px 15px rgba(55, 66, 250, 0.3); text-transform: uppercase; letter-spacing: 1px; font-family: inherit; }
  .btn-primary:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(55, 66, 250, 0.4); filter: brightness(1.1); }

  .controls-bar { display: flex; justify-content: space-between; align-items: center; margin-top: 20px; background: var(--bg); padding: 15px; border-radius: 12px; border: 1px solid var(--border); }
  .btn-danger { background: rgba(255, 71, 87, 0.1); color: var(--danger); border: 2px solid rgba(255, 71, 87, 0.2); padding: 10px 15px; border-radius: 10px; font-weight: 700; cursor: pointer; transition: 0.2s; font-family: inherit; }
  .btn-danger:hover { background: var(--danger); color: white; transform: translateY(-1px); }
  
  .toggle-wrap { display: flex; align-items: center; gap: 10px; font-weight: 700; color: var(--text-muted); font-size: 0.85rem; cursor: pointer; user-select: none; }
  .switch { position: relative; width: 44px; height: 24px; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: var(--border); transition: .3s; border-radius: 34px; }
  .slider:before { position: absolute; content: ""; height: 16px; width: 16px; left: 4px; bottom: 4px; background-color: white; transition: .3s; border-radius: 50%; box-shadow: 0 2px 5px rgba(0,0,0,0.2); }
  input:checked + .slider { background-color: var(--accent); }
  input:checked + .slider:before { transform: translateX(20px); }

  .footer { text-align: center; padding: 20px; color: var(--text-muted); font-size: 0.85rem; font-weight: 600; }
  
  .live-indicator { display: flex; align-items: center; gap: 8px; font-size: 0.8rem; font-weight: 700; color: var(--success); background: rgba(29, 209, 161, 0.1); padding: 6px 12px; border-radius: 20px; }
  .dot { width: 8px; height: 8px; background: var(--success); border-radius: 50%; box-shadow: 0 0 8px var(--success); animation: pulse-live 1.5s infinite alternate; }
  @keyframes pulse-live { from { opacity: 1; transform: scale(1); } to { opacity: 0.5; transform: scale(0.8); } }
  .offline { color: var(--danger); background: rgba(255, 71, 87, 0.1); }
  .offline .dot { background: var(--danger); box-shadow: 0 0 8px var(--danger); animation: none; }
  
  .sparkline-svg { width: 100%; height: 60px; border-radius: 8px; background: rgba(0,0,0,0.02); display: block; }
  [data-theme="dark"] .sparkline-svg { background: rgba(255,255,255,0.02); }
</style>
</head>
<body>

<header class="header">
  <div class="title">&#127777; Temperature & Humidity Monitoring</div>
  <div class="header-controls">
    <span class="badge" id="envBadge">LOADING...</span>
    <button class="theme-btn" id="themeBtn" onclick="toggleTheme()" title="Toggle Theme">&#127769;</button>
  </div>
</header>

<div class="alert-banner" id="alertBar">
  <div>&#9888; <span id="alertTxt">CRITICAL ALERT</span></div>
  <button class="dismiss-btn" onclick="dismissAlert()">SILENCE ALARM</button>
</div>

<div class="container">
  
  <!-- SENSORS CARD -->
  <div class="card">
    <div class="card-title">
      <span>Live Readings</span>
      <div class="live-indicator" id="liveInd"><div class="dot" id="liveDot"></div><span id="liveTxt">LIVE</span></div>
    </div>
    
    <div class="gauges-wrapper">
      <div class="gauge-box">
        <div class="gauge-label">Temperature</div>
        <div class="gauge-container">
          <svg class="gauge-svg" viewBox="0 0 100 60">
            <path d="M 10 50 A 40 40 0 0 1 90 50" fill="none" stroke="var(--border)" stroke-width="12" stroke-linecap="round"/>
            <path id="tArc" d="M 10 50 A 40 40 0 0 1 90 50" fill="none" stroke="var(--accent)" stroke-width="12" stroke-linecap="round" stroke-dasharray="0 200"/>
          </svg>
          <div class="gauge-val-overlay">
            <div class="main-val"><span id="tVal">--.-</span><span class="trend-icon" id="tTrend">-</span></div>
            <div class="sub-val" id="tValF">-- °F</div>
          </div>
        </div>
        <div class="stat-label" style="margin-top:15px; color:var(--accent)" id="tStatus">NORMAL</div>
      </div>

      <div class="gauge-box">
        <div class="gauge-label">Humidity</div>
        <div class="gauge-container">
          <svg class="gauge-svg" viewBox="0 0 100 60">
            <path d="M 10 50 A 40 40 0 0 1 90 50" fill="none" stroke="var(--border)" stroke-width="12" stroke-linecap="round"/>
            <path id="hArc" d="M 10 50 A 40 40 0 0 1 90 50" fill="none" stroke="var(--success)" stroke-width="12" stroke-linecap="round" stroke-dasharray="0 200"/>
          </svg>
          <div class="gauge-val-overlay">
            <div class="main-val"><span id="hVal">--.-</span><span class="trend-icon" id="hTrend">-</span></div>
            <div class="sub-val">% RELATIVE</div>
          </div>
        </div>
        <div class="stat-label" style="margin-top:15px; color:var(--success)" id="hStatus">NORMAL</div>
      </div>
    </div>

    <div class="stats-grid">
      <div class="stat-item"><div class="stat-label">Heat Index</div><div class="stat-value" style="color:var(--warning)" id="vHI">-- °C</div></div>
      <div class="stat-item"><div class="stat-label">Dew Point</div><div class="stat-value" style="color:var(--accent)" id="vDP">-- °C</div></div>
      <div class="stat-item"><div class="stat-label">Session High</div><div class="stat-value" style="color:var(--danger)" id="vMaxT">-- °C</div></div>
      <div class="stat-item"><div class="stat-label">Session Low</div><div class="stat-value" style="color:var(--accent)" id="vMinT">-- °C</div></div>
    </div>
  </div>

  <!-- SETTINGS CARD -->
  <div class="card">
    <div class="card-title">Environment Controls</div>
    
    <div class="env-grid">
      <button class="env-btn active" id="b-serverroom" onclick="selEnv('serverroom')"><span class="env-icon">&#128187;</span><span class="lbl">Server</span><span class="env-limits">18-27°C</span></button>
      <button class="env-btn" id="b-greenhouse" onclick="selEnv('greenhouse')"><span class="env-icon">&#127807;</span><span class="lbl">Plants</span><span class="env-limits">20-30°C</span></button>
      <button class="env-btn" id="b-office" onclick="selEnv('office')"><span class="env-icon">&#128188;</span><span class="lbl">Office</span><span class="env-limits">20-25°C</span></button>
      <button class="env-btn" id="b-winecellar" onclick="selEnv('winecellar')"><span class="env-icon">&#127863;</span><span class="lbl">Wine Cellar</span><span class="env-limits">10-16°C</span></button>
      <button class="env-btn" id="b-bedroom" onclick="selEnv('bedroom')"><span class="env-icon">&#128716;</span><span class="lbl">Room</span><span class="env-limits">16-22°C</span></button>
      <button class="env-btn" id="b-custom" onclick="selEnv('custom')"><span class="env-icon">&#9881;</span><span class="lbl">Custom</span><span class="env-limits">Profile</span></button>
    </div>

    <div class="custom-panel" id="cpan">
      <div class="slider-group">
        <div class="slider-header"><span>Min Temp</span><span class="slider-val-badge" id="svMinT">18 °C</span></div>
        <input type="range" id="rMinT" min="0" max="50" value="18">
      </div>
      <div class="slider-group">
        <div class="slider-header"><span>Max Temp</span><span class="slider-val-badge" id="svMaxT">27 °C</span></div>
        <input type="range" id="rMaxT" min="0" max="50" value="27">
      </div>
      <div class="slider-group">
        <div class="slider-header"><span>Min Humidity</span><span class="slider-val-badge" id="svMinH">40%</span></div>
        <input type="range" id="rMinH" min="0" max="100" value="40">
      </div>
      <div class="slider-group">
        <div class="slider-header"><span>Max Humidity</span><span class="slider-val-badge" id="svMaxH">60%</span></div>
        <input type="range" id="rMaxH" min="0" max="100" value="60">
      </div>
      <button class="btn-primary" onclick="applyCustom()">Update Thresholds</button>
    </div>

    <div class="controls-bar">
      <label class="toggle-wrap">
        <label class="switch"><input type="checkbox" id="audEn" checked><span class="slider"></span></label>
        Enable Siren
      </label>
      <button class="btn-danger" onclick="doReset()">&#8635; Reset Stats</button>
    </div>
  </div>

  <!-- HISTORY GRAPHS (Now spans full width at the bottom) -->
  <div class="card" style="grid-column: 1 / -1;">
    <div class="card-title">Trend History</div>
    <div class="stats-grid" style="grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 20px;">
      <div class="stat-item" style="padding: 15px 10px;">
        <div class="stat-label" style="margin-bottom: 10px;">Temperature History (°C)</div>
        <div id="tChrt" class="sparkline-svg"></div>
      </div>
      <div class="stat-item" style="padding: 15px 10px;">
        <div class="stat-label" style="margin-bottom: 10px;">Humidity History (%)</div>
        <div id="hChrt" class="sparkline-svg"></div>
      </div>
    </div>
  </div>

</div>

<div class="footer">
  Temperature & Humidity Monitoring &bull; <span id="lastUpd">--</span>
</div>

<script>
// Theme Toggle
function toggleTheme() {
  var html = document.documentElement;
  var isDark = html.getAttribute('data-theme') === 'dark';
  var newTheme = isDark ? 'light' : 'dark';
  html.setAttribute('data-theme', newTheme);
  document.getElementById('themeBtn').innerHTML = (newTheme === 'dark') ? '&#127769;' : '&#9728;&#65039;';
}

// Emergency Alarm Audio
var AX = null;
var alarmInterval = null;
function initAudio() {
  if (!AX) AX = new (window.AudioContext || window.webkitAudioContext)();
  if (AX.state === 'suspended') AX.resume();
}
document.addEventListener('click', initAudio, {once:true});
document.addEventListener('touchstart', initAudio, {once:true});

function playAlert() {
  if (!document.getElementById('audEn').checked) return;
  initAudio();
  if (!AX) return;
  var now = AX.currentTime;
  [[1200, 0.0], [900, 0.2], [1200, 0.4], [900, 0.6], [1200, 0.8], [900, 1.0]].forEach(function(p) {
    var o = AX.createOscillator(), g = AX.createGain();
    o.connect(g); g.connect(AX.destination);
    o.type = 'square';
    o.frequency.value = p[0];
    g.gain.setValueAtTime(0.001, now + p[1]);
    g.gain.linearRampToValueAtTime(0.4, now + p[1] + 0.02);
    g.gain.linearRampToValueAtTime(0.4, now + p[1] + 0.18);
    g.gain.exponentialRampToValueAtTime(0.001, now + p[1] + 0.2);
    o.start(now + p[1]);
    o.stop(now + p[1] + 0.2);
  });
}

// Custom Sliders - bind on input so it updates text immediately and sets editing flag
var editing = false;
function bindSlider(rid, did, unit) {
  var el = document.getElementById(rid);
  el.addEventListener('input', function() {
    editing = true;
    document.getElementById(did).textContent = this.value + unit;
  });
}
bindSlider('rMinT', 'svMinT', ' °C');
bindSlider('rMaxT', 'svMaxT', ' °C');
bindSlider('rMinH', 'svMinH', '%');
bindSlider('rMaxH', 'svMaxH', '%');

// Env Selection
var envNames = {serverroom:'SERVER ROOM', greenhouse:'GREENHOUSE', office:'OFFICE', winecellar:'WINE CELLAR', bedroom:'BEDROOM', custom:'CUSTOM PROFILE'};
function selEnv(env) {
  document.querySelectorAll('.env-btn').forEach(function(b){ b.classList.remove('active'); });
  var btn = document.getElementById('b-'+env);
  if (btn) btn.classList.add('active');
  var cp = document.getElementById('cpan');
  if (env === 'custom') { editing = true; cp.classList.add('open'); return; }
  editing = false;
  cp.classList.remove('open');
  fetch('/setenv?env='+env).then(poll);
}

function applyCustom() {
  var url = '/setcustom?minT=' + document.getElementById('rMinT').value
          + '&maxT='           + document.getElementById('rMaxT').value
          + '&minH='           + document.getElementById('rMinH').value
          + '&maxH='           + document.getElementById('rMaxH').value;
  fetch(url).then(function(){ editing = false; poll(); });
}

function doReset() {
  if (confirm('Clear session memory?')) fetch('/reset').then(poll);
}

// UI Updaters
var ARC = 125.6;
function drawGauge(arcId, valId, v, lo, hi, isT) {
  var p = Math.max(0, Math.min(1, (v - lo) / (hi - lo)));
  var el = document.getElementById(arcId);
  var html = document.documentElement;
  var isDark = html.getAttribute('data-theme') === 'dark';
  var color = isT ? (isDark ? '#3742fa' : '#0984e3') : (isDark ? '#1dd1a1' : '#00b894');
  if (v > hi) color = (isDark ? '#ff4757' : '#d63031');
  else if (v < lo) color = (isDark ? '#3742fa' : '#0984e3');
  
  if (el) {
    el.setAttribute('stroke-dasharray', (p*ARC).toFixed(1) + ' 200');
    el.setAttribute('stroke', color);
  }
  document.getElementById(valId).textContent = v.toFixed(1);
}

function drawSparkline(id, data, isT) {
  var el = document.getElementById(id);
  if (!el || !data || data.length < 2) return;
  var W = 300, H = 60, P = 5;
  var mn = Math.min.apply(null, data), mx = Math.max.apply(null, data);
  var rng = (mx - mn) || 1;
  var pts = data.map(function(v, i) {
    var x = (i / (data.length - 1)) * W;
    var y = H - P - ((v - mn) / rng) * (H - 2*P);
    return x.toFixed(1) + ',' + y.toFixed(1);
  }).join(' ');
  var color = isT ? 'var(--accent)' : 'var(--success)';
  el.innerHTML = '<svg width="100%" height="100%" viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="none"><polyline fill="none" stroke="' + color + '" stroke-width="2" points="' + pts + '"/></svg>';
}

// Memory
var lastT = null, lastH = null;
var alertDismissed = false;

function dismissAlert() {
  alertDismissed = true;
  document.getElementById('alertBar').style.display = 'none';
  if (alarmInterval) { clearInterval(alarmInterval); alarmInterval = null; }
}

function poll() {
  fetch('/data')
    .then(function(r) { return r.json(); })
    .then(function(d) {
      document.getElementById('liveInd').className = 'live-indicator';
      document.getElementById('liveTxt').textContent = 'LIVE';
      document.getElementById('envBadge').textContent = envNames[d.env] || 'SYSTEM';

      document.querySelectorAll('.env-btn').forEach(function(b){ b.classList.remove('active'); });
      var eb2 = document.getElementById('b-'+d.env);
      if (eb2) eb2.classList.add('active');

      if (lastT !== null) {
        var tT = document.getElementById('tTrend');
        if (d.temp > lastT + 0.1) { tT.innerHTML = '&#8593;'; tT.className = 'trend-icon trend-up'; }
        else if (d.temp < lastT - 0.1) { tT.innerHTML = '&#8595;'; tT.className = 'trend-icon trend-down'; }
        else { tT.innerHTML = ''; }
        
        var tH = document.getElementById('hTrend');
        if (d.hum > lastH + 0.5) { tH.innerHTML = '&#8593;'; tH.className = 'trend-icon trend-up'; }
        else if (d.hum < lastH - 0.5) { tH.innerHTML = '&#8595;'; tH.className = 'trend-icon trend-down'; }
        else { tH.innerHTML = ''; }
      }
      lastT = d.temp; lastH = d.hum;

      drawGauge('tArc', 'tVal', d.temp, d.minT, d.maxT, true);
      document.getElementById('tValF').textContent = d.tempF.toFixed(1) + ' °F';
      drawGauge('hArc', 'hVal', d.hum, d.minH, d.maxH, false);
      
      document.getElementById('tStatus').textContent = (d.temp > d.maxT) ? 'TOO HOT' : (d.temp < d.minT) ? 'TOO COLD' : 'OPTIMAL';
      document.getElementById('tStatus').style.color = (d.temp > d.maxT || d.temp < d.minT) ? 'var(--danger)' : 'var(--success)';
      document.getElementById('hStatus').textContent = (d.hum > d.maxH) ? 'TOO HUMID' : (d.hum < d.minH) ? 'TOO DRY' : 'OPTIMAL';
      document.getElementById('hStatus').style.color = (d.hum > d.maxH || d.hum < d.minH) ? 'var(--danger)' : 'var(--success)';

      document.getElementById('vHI').textContent = d.heatIndex.toFixed(1) + ' °C';
      document.getElementById('vDP').textContent = d.dewPoint.toFixed(1) + ' °C';
      document.getElementById('vMaxT').textContent = d.maxTemp.toFixed(1) + ' °C';
      document.getElementById('vMinT').textContent = d.minTemp.toFixed(1) + ' °C';

      if (d.th && d.th.length > 1) drawSparkline('tChrt', d.th, true);
      if (d.hh && d.hh.length > 1) drawSparkline('hChrt', d.hh, false);

      var ab = document.getElementById('alertBar');
      if (d.alert) {
        if (!alertDismissed) {
          ab.style.display = 'flex';
          document.getElementById('alertTxt').textContent = d.alertMsg;
          if (!alarmInterval && document.getElementById('audEn').checked) {
            playAlert();
            alarmInterval = setInterval(playAlert, 1500);
          }
        }
      } else {
        ab.style.display = 'none';
        alertDismissed = false;
        if (alarmInterval) { clearInterval(alarmInterval); alarmInterval = null; }
      }

      var cp = document.getElementById('cpan');
      if (d.env === 'custom') {
        if (!editing) {
          cp.classList.add('open');
          document.getElementById('rMinT').value = d.minT;
          document.getElementById('rMaxT').value = d.maxT;
          document.getElementById('rMinH').value = d.minH;
          document.getElementById('rMaxH').value = d.maxH;
          document.getElementById('svMinT').textContent = d.minT + ' °C';
          document.getElementById('svMaxT').textContent = d.maxT + ' °C';
          document.getElementById('svMinH').textContent = d.minH + '%';
          document.getElementById('svMaxH').textContent = d.maxH + '%';
        }
      } else {
        if (!editing) cp.classList.remove('open');
      }

      document.getElementById('lastUpd').textContent = new Date().toLocaleTimeString();
    })
    .catch(function() {
      document.getElementById('liveInd').className = 'live-indicator offline';
      document.getElementById('liveTxt').textContent = 'OFFLINE';
      if (alarmInterval) { clearInterval(alarmInterval); alarmInterval = null; }
    });
}

poll();
setInterval(poll, 1000);
</script>
</body>
</html>

)ESP32MON";

// ═════════════════════════════════════════════════════════
//  HTTP HANDLERS
// ═════════════════════════════════════════════════════════
void handleRoot() {
  server.send_P(200, "text/html", PAGE);
}

void handleData() {
  String tArr = "[", hArr = "[";
  int cnt = min(hFill, HIST);
  for (int i = 0; i < cnt; i++) {
    int ix = (hPtr - cnt + i + HIST) % HIST;
    if (i > 0) { tArr += ","; hArr += ","; }
    tArr += String(tHist[ix], 1);
    hArr += String(hHist[ix], 1);
  }
  tArr += "]"; hArr += "]";

  String json = "{";
  json += "\"temp\":"       + String(tempC,    1) + ",";
  json += "\"tempF\":"      + String(tempF,    1) + ",";
  json += "\"hum\":"        + String(humidity, 1) + ",";
  json += "\"heatIndex\":"  + String(heatIdx,  1) + ",";
  json += "\"dewPoint\":"   + String(dewPt,    1) + ",";
  json += "\"maxTemp\":"    + String(sesMaxT,  1) + ",";
  json += "\"minTemp\":"    + String(sesMinT,  1) + ",";
  json += "\"maxHum\":"     + String(sesMaxH,  1) + ",";
  json += "\"minHum\":"     + String(sesMinH,  1) + ",";
  json += "\"minT\":"       + String(limMinT,  1) + ",";
  json += "\"maxT\":"       + String(limMaxT,  1) + ",";
  json += "\"minH\":"       + String(limMinH,  1) + ",";
  json += "\"maxH\":"       + String(limMaxH,  1) + ",";
  json += "\"alert\":"      + String(isAlert() ? "true" : "false") + ",";
  json += "\"alertMsg\":\"" + getAlertMsg() + "\",";
  json += "\"env\":\""      + envMode + "\",";
  json += "\"th\":"         + tArr + ",";
  json += "\"hh\":"         + hArr;
  json += "}";

  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", json);
}

void handleSetEnv() {
  if (server.hasArg("env")) {
    envMode = server.arg("env");
    if (envMode != "custom") applyPreset(envMode);
    savePrefs();
  }
  server.send(200, "text/plain", "ok");
}

void handleSetCustom() {
  if (server.hasArg("minT")) limMinT = server.arg("minT").toFloat();
  if (server.hasArg("maxT")) limMaxT = server.arg("maxT").toFloat();
  if (server.hasArg("minH")) limMinH = server.arg("minH").toFloat();
  if (server.hasArg("maxH")) limMaxH = server.arg("maxH").toFloat();
  envMode = "custom";
  savePrefs();
  server.send(200, "text/plain", "ok");
}

void handleReset() {
  sesMinT =  999.0; sesMaxT = -999.0;
  sesMinH =  999.0; sesMaxH = -999.0;
  hPtr = 0; hFill = 0;
  memset(tHist, 0, sizeof(tHist));
  memset(hHist, 0, sizeof(hHist));
  server.send(200, "text/plain", "ok");
}

// ═════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  dht.begin();

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
    for (;;);
  }

  // Load saved prefs
  prefs.begin("em", true);
  envMode  = prefs.getString("env",  "serverroom");
  limMinT  = prefs.getFloat ("mnt",  18.0f);
  limMaxT  = prefs.getFloat ("mxt",  27.0f);
  limMinH  = prefs.getFloat ("mnh",  40.0f);
  limMaxH  = prefs.getFloat ("mxh",  60.0f);
  prefs.end();
  if (envMode != "custom") applyPreset(envMode);

  oledMsg("Connecting to WiFi", "SSID: POCO F6", "Please wait...");

  // WiFi with 10-second timeout
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    tries++;
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    wifiOK = false;
    Serial.println("\nWiFi FAILED");
    oledMsg("!! WiFi ERROR !!",
            "Cannot connect to",
            "SSID: POCO F6",
            "Check password.",
            "Sensor still reads.");
    delay(4000);
  } else {
    wifiOK = true;
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0,0);  display.println("WiFi Connected!");
    display.setCursor(0,10); display.println("Open browser at:");
    display.setCursor(0,20); display.println(WiFi.localIP());
    display.setCursor(0,32); display.println("Dashboard ready.");
    display.display();
    delay(3000);

    server.on("/",          handleRoot);
    server.on("/data",      handleData);
    server.on("/setenv",    handleSetEnv);
    server.on("/setcustom", handleSetCustom);
    server.on("/reset",     handleReset);
    server.begin();
    Serial.println("HTTP server started.");
  }
}

// ═════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════
void loop() {
  if (wifiOK) server.handleClient();

  // Read DHT every 2 seconds (DHT11 minimum interval)
  if (millis() - lastRead >= 2000) {
    lastRead = millis();

    float nt = dht.readTemperature();
    float nh = dht.readHumidity();

    // Retry once on failure
    if (isnan(nt) || isnan(nh)) {
      delay(100);
      nt = dht.readTemperature();
      nh = dht.readHumidity();
    }

    if (!isnan(nt) && !isnan(nh)) {
      // Apply EMA filter for stable readings
      if (!emaInit) { emaT = nt; emaH = nh; emaInit = true; }
      else { emaT = EMA_A * nt + (1.0f - EMA_A) * emaT;
             emaH = EMA_A * nh + (1.0f - EMA_A) * emaH; }

      tempC    = roundf(emaT * 10.0f) / 10.0f;
      tempF    = roundf((tempC * 9.0f / 5.0f + 32.0f) * 10.0f) / 10.0f;
      humidity = roundf(emaH * 10.0f) / 10.0f;
      heatIdx  = calcHeatIndex(tempC, humidity);
      dewPt    = calcDewPoint(tempC, humidity);
      sensorOK = true;

      if (tempC    > sesMaxT) sesMaxT = tempC;
      if (tempC    < sesMinT) sesMinT = tempC;
      if (humidity > sesMaxH) sesMaxH = humidity;
      if (humidity < sesMinH) sesMinH = humidity;

      tHist[hPtr] = tempC;
      hHist[hPtr] = humidity;
      hPtr = (hPtr + 1) % HIST;
      if (hFill < HIST) hFill++;

      Serial.printf("Temp: %.1fC / %.1fF  Hum: %.1f%%  HI: %.1f  DP: %.1f  Alert: %s\n",
                    tempC, tempF, humidity, heatIdx, dewPt, isAlert() ? "YES" : "no");
    } else {
      sensorOK = false;
      Serial.println("DHT read failed.");
    }
  }

  // OLED blink every 500 ms
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    blinkOn = !blinkOn;
    oledUpdate();
  }
}