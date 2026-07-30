#include <Arduino.h>
#include <FastLED.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "secrets.h"

// --------------------------- Lamp configuration ---------------------------
constexpr uint16_t NUM_LEDS = 65;             // Adjust this for your strip.
constexpr uint8_t LED_PIN = 16;
constexpr EOrder LED_COLOR_ORDER = GRB;
constexpr uint8_t BRIGHTNESS_CAP_PERCENT = 60; // USB-friendly global cap.
constexpr uint8_t BRIGHTNESS_CAP =
    (255UL * BRIGHTNESS_CAP_PERCENT) / 100UL;
constexpr uint16_t MAX_MILLIAMPS = 500;        // Additional FastLED power guard.
constexpr char MDNS_NAME[] = "moodlamp";
constexpr char FALLBACK_AP_NAME[] = "MoodLamp-Fallback";
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
// -------------------------------------------------------------------------

enum class LampMode : uint8_t { Solid, Breathe, Rainbow, Wipe, Fire };

CRGB leds[NUM_LEDS];
uint8_t heat[NUM_LEDS];
WebServer server(80);
Preferences preferences;

struct LampSettings {
  uint32_t color = 0xFF6A2A;
  uint8_t brightness = 100; // 0..100, then constrained by the global cap.
  uint8_t speed = 50;       // 1..100.
  LampMode mode = LampMode::Solid;
} settings;

bool settingsSavePending = false;
uint32_t settingsChangedAt = 0;
uint32_t animationStartedAt = 0;
uint32_t lastFrameAt = 0;
uint16_t wipeStep = 0;
uint8_t rainbowHue = 0;

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#292929">
<title>Funduino Stimmungslicht</title>
<style>
:root{font-family:"Trebuchet MS",Avenir,ui-rounded,system-ui,-apple-system,sans-serif;color:#fff;background:#292929}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;background-color:#292929;background-image:radial-gradient(#353534 1.4px,transparent 1.4px);background-size:21px 21px;color:#fff}
main{width:min(100%,540px);margin:auto;padding:0 16px calc(28px + env(safe-area-inset-bottom))}
.hero{position:relative;margin:0 -16px 18px;padding:calc(22px + env(safe-area-inset-top)) 20px 28px;overflow:hidden;background:#fff;color:#292929;border-radius:0 0 28px 28px;box-shadow:0 10px 0 #347f78}
.hero:before,.hero:after{content:"";position:absolute;border:4px solid #347f78;border-radius:50%}.hero:before{width:110px;height:110px;right:-42px;top:-34px}.hero:after{width:54px;height:54px;right:48px;bottom:-34px}
.brand{display:flex;align-items:center;gap:11px;position:relative;z-index:1}.brandmark{display:grid;place-items:center;width:48px;height:48px;border-radius:13px;background:#347f78;color:#fff;font:bold 1.75rem Arial,sans-serif;box-shadow:4px 4px 0 #292929;transform:rotate(-2deg)}
.wordmark{font:900 1.28rem Arial,sans-serif;letter-spacing:.08em}
h1{position:relative;z-index:1;margin:19px 0 4px;font-size:1.7rem;letter-spacing:-.025em}.hero p{position:relative;z-index:1;margin:0;color:#353534;font-size:.9rem}
.lampview{display:flex;align-items:center;gap:14px;background:#353534;border:2px solid #347f78;border-radius:18px;padding:12px 14px;margin-bottom:14px;box-shadow:4px 5px 0 #292929}.bulb{position:relative;width:54px;height:54px;flex:0 0 54px;border:5px solid #fff;border-radius:50%;background:#ff6a2a;box-shadow:0 0 0 3px #347f78,0 0 20px #ff6a2aaa;transition:.2s}.lampview strong{display:block;color:#fff}.lampview span{font-size:.79rem;color:#fff;opacity:.78}
.card{position:relative;background:#353534;border:2px solid #347f78;border-radius:18px;padding:17px;margin-bottom:13px;box-shadow:4px 5px 0 #292929}.card:before{content:"";position:absolute;left:17px;top:-2px;width:42px;height:4px;border-radius:0 0 5px 5px;background:#347f78}
label,.labelrow{display:flex;justify-content:space-between;align-items:center;gap:10px;font-weight:800;color:#fff;margin-bottom:12px}.value,.hex{padding:4px 8px;border:1px solid #347f78;border-radius:7px;background:#292929;color:#fff;font-size:.84rem;font-variant-numeric:tabular-nums}.hex{font:bold .9rem ui-monospace,SFMono-Regular,Menlo,monospace;text-transform:uppercase}
.colorrow{display:grid;grid-template-columns:74px 1fr;gap:14px;align-items:center}.colorrow strong{color:#fff}
input[type=color]{width:74px;height:74px;padding:0;border:4px solid #347f78;border-radius:15px;background:none;overflow:hidden;box-shadow:3px 3px 0 #292929}input[type=color]::-webkit-color-swatch-wrapper{padding:0}input[type=color]::-webkit-color-swatch{border:0;border-radius:9px}
.help{display:block;color:#fff;opacity:.7;line-height:1.4;font-size:.79rem;margin-top:4px}.presets{display:flex;gap:9px;margin-top:14px;flex-wrap:wrap}.preset{width:31px;height:31px;border:3px solid #fff;border-radius:50%;box-shadow:0 0 0 2px #347f78;cursor:pointer}.preset:active{transform:scale(.9)}
input[type=range]{width:100%;height:32px;margin:0;accent-color:#347f78;touch-action:pan-y}
.scale{display:flex;justify-content:space-between;color:#fff;opacity:.62;font-size:.7rem;margin-top:-2px}
select{width:100%;font-family:inherit;font-size:1rem;font-weight:700;color:#fff;background:#292929;border:2px solid #347f78;border-radius:11px;padding:12px 40px 12px 13px;outline:none}
.status{text-align:center;color:#fff;font-size:.82rem;min-height:1.2em;padding:6px}.online{color:#347f78;font-weight:700}.footer{text-align:center;color:#fff;opacity:.65;font-size:.69rem;margin-top:7px}.footer b{color:#fff}
@media(min-width:500px){main{padding-left:20px;padding-right:20px}.hero{margin-left:-20px;margin-right:-20px;padding-left:26px}.card{padding:20px}}
</style>
</head>
<body><main>
<header class="hero"><div class="brand"><div class="brandmark">F</div><div class="wordmark">FUNDUINO</div></div><h1>Mein Stimmungslicht</h1><p id="device">Verbindung zur Lampe wird hergestellt…</p></header>
<div class="lampview"><div class="bulb" id="logo"></div><div><strong><span id="ledCount">65</span> LEDs bereit!</strong><span>Farbe und Effekte direkt mit deinem Smartphone steuern.</span></div></div>
<section class="card"><div class="labelrow"><span>🎨 Deine Farbe</span><span class="hex" id="hex">#FF6A2A</span></div><div class="colorrow"><input id="color" type="color" value="#ff6a2a" aria-label="Lichtfarbe auswählen"><div><strong>Farbe auswählen</strong><span class="help">Für Dauerlicht, Atmen und Farbwischer.</span></div></div><div class="presets" aria-label="Schnellauswahl"><button class="preset" data-color="#ff2d2d" style="background:#ff2d2d" aria-label="Rot"></button><button class="preset" data-color="#ff9f1c" style="background:#ff9f1c" aria-label="Orange"></button><button class="preset" data-color="#ffe02e" style="background:#ffe02e" aria-label="Gelb"></button><button class="preset" data-color="#22c55e" style="background:#22c55e" aria-label="Grün"></button><button class="preset" data-color="#168cff" style="background:#168cff" aria-label="Blau"></button><button class="preset" data-color="#a855f7" style="background:#a855f7" aria-label="Violett"></button></div></section>
<section class="card"><label for="brightness"><span>☀️ Helligkeit</span><span class="value" id="brightnessValue">100%</span></label><input id="brightness" type="range" min="0" max="100" value="100"><div class="scale"><span>Dunkel</span><span>Hell</span></div><span class="help">Zum Schutz der USB-Stromversorgung auf maximal <span id="cap">60</span>% begrenzt.</span></section>
<section class="card"><label for="mode"><span>✨ Animation</span></label><select id="mode"><option value="solid">Dauerlicht</option><option value="breathe">Sanftes Atmen</option><option value="rainbow">Regenbogen</option><option value="wipe">Farbwischer</option><option value="fire">Kerzen- und Feuerflackern</option></select></section>
<section class="card"><label for="speed"><span>⚡ Geschwindigkeit</span><span class="value" id="speedValue">50%</span></label><input id="speed" type="range" min="1" max="100" value="50"><div class="scale"><span>Langsam</span><span>Schnell</span></div></section>
<div class="status" id="status">Gespeicherte Einstellungen werden geladen…</div><div class="footer"><b>FUNDUINO</b> · Technik macht Spaß!</div>
</main>
<script>
const $=id=>document.getElementById(id);
const controls=['color','brightness','mode','speed'];
let timer;
function paint(){const c=$('color').value;$('hex').textContent=c;$('logo').style.background=c;$('logo').style.boxShadow=`0 0 0 3px #347f78,0 0 22px ${c}aa`;$('brightnessValue').textContent=$('brightness').value+'%';$('speedValue').textContent=$('speed').value+'%'}
async function send(){clearTimeout(timer);const q=new URLSearchParams({color:$('color').value,brightness:$('brightness').value,mode:$('mode').value,speed:$('speed').value});$('status').textContent='Wird übertragen…';$('status').className='status';try{const r=await fetch('/api/set?'+q,{method:'POST'});if(!r.ok)throw Error(r.status);await r.json();$('status').textContent='✓ Lampe aktualisiert';$('status').className='status online'}catch(e){$('status').textContent='Lampe ist nicht erreichbar';$('status').className='status'}}
function changed(e){paint();clearTimeout(timer);timer=setTimeout(send,e.target.tagName==='SELECT'?0:120)}
controls.forEach(id=>$(id).addEventListener(id==='mode'?'change':'input',changed));
document.querySelectorAll('.preset').forEach(b=>b.addEventListener('click',()=>{$('color').value=b.dataset.color;paint();send()}));
(async()=>{try{const r=await fetch('/api/state');if(!r.ok)throw Error(r.status);const s=await r.json();$('color').value=s.color;$('brightness').value=s.brightness;$('mode').value=s.mode;$('speed').value=s.speed;$('cap').textContent=s.brightnessCap;$('ledCount').textContent=s.leds;$('device').textContent=s.hostname+'.local';paint();$('status').textContent='✓ Verbunden';$('status').className='status online'}catch(e){$('device').textContent='Lampe nicht verfügbar';$('status').textContent='Bitte Seite neu laden'}})();
</script></body></html>
)HTML";

const char *modeName(LampMode mode) {
  switch (mode) {
  case LampMode::Solid:
    return "solid";
  case LampMode::Breathe:
    return "breathe";
  case LampMode::Rainbow:
    return "rainbow";
  case LampMode::Wipe:
    return "wipe";
  case LampMode::Fire:
    return "fire";
  }
  return "solid";
}

bool parseMode(const String &value, LampMode &result) {
  if (value == "solid")
    result = LampMode::Solid;
  else if (value == "breathe")
    result = LampMode::Breathe;
  else if (value == "rainbow")
    result = LampMode::Rainbow;
  else if (value == "wipe" || value == "colorwipe")
    result = LampMode::Wipe;
  else if (value == "fire" || value == "candle")
    result = LampMode::Fire;
  else
    return false;
  return true;
}

CRGB selectedColor() {
  return CRGB((settings.color >> 16) & 0xFF, (settings.color >> 8) & 0xFF,
              settings.color & 0xFF);
}

String colorString() {
  char value[8];
  snprintf(value, sizeof(value), "#%06lX",
           static_cast<unsigned long>(settings.color));
  return String(value);
}

void setOutputBrightness(uint8_t animationScale = 255) {
  const uint16_t userLimited =
      (static_cast<uint16_t>(BRIGHTNESS_CAP) * settings.brightness + 50) / 100;
  const uint8_t output =
      (static_cast<uint32_t>(userLimited) * animationScale + 127) / 255;
  FastLED.setBrightness(output);
}

uint32_t frameIntervalMs() {
  switch (settings.mode) {
  case LampMode::Solid:
    return 1000;
  case LampMode::Breathe:
    return 20;
  case LampMode::Rainbow:
    return map(settings.speed, 1, 100, 90, 8);
  case LampMode::Wipe:
    return map(settings.speed, 1, 100, 220, 18);
  case LampMode::Fire:
    return map(settings.speed, 1, 100, 150, 25);
  }
  return 30;
}

void renderSolid() {
  fill_solid(leds, NUM_LEDS, selectedColor());
  setOutputBrightness();
}

void renderBreathe(uint32_t now) {
  const uint32_t period = map(settings.speed, 1, 100, 7000, 900);
  const uint8_t phase = ((now - animationStartedAt) * 256UL / period + 64) & 0xFF;
  const uint8_t wave = map(sin8(phase), 0, 255, 8, 255);
  fill_solid(leds, NUM_LEDS, selectedColor());
  setOutputBrightness(wave);
}

void renderRainbow() {
  fill_rainbow(leds, NUM_LEDS, rainbowHue++, 255 / max<uint16_t>(NUM_LEDS, 1));
  setOutputBrightness();
}

void renderWipe() {
  const uint16_t cycleLength = max<uint16_t>(NUM_LEDS * 2, 2);
  const uint16_t phase = wipeStep++ % cycleLength;
  const CRGB color = selectedColor();
  if (phase < NUM_LEDS) {
    for (uint16_t i = 0; i < NUM_LEDS; ++i)
      leds[i] = (i <= phase) ? color : CRGB::Black;
  } else {
    const uint16_t clearing = phase - NUM_LEDS;
    for (uint16_t i = 0; i < NUM_LEDS; ++i)
      leds[i] = (i <= clearing) ? CRGB::Black : color;
  }
  setOutputBrightness();
}

void renderFire() {
  for (uint16_t i = 0; i < NUM_LEDS; ++i) {
    heat[i] = qsub8(heat[i], random8(0, ((55 * 10) / max<uint16_t>(NUM_LEDS, 1)) + 2));
  }
  for (int i = NUM_LEDS - 1; i >= 2; --i)
    heat[i] = (heat[i - 1] + heat[i - 2] + heat[i - 2]) / 3;
  if (random8() < 125) {
    const uint8_t y = random8(min<uint16_t>(NUM_LEDS, 7));
    heat[y] = qadd8(heat[y], random8(160, 255));
  }
  for (uint16_t i = 0; i < NUM_LEDS; ++i)
    leds[i] = ColorFromPalette(HeatColors_p, scale8(heat[i], 240));
  setOutputBrightness();
}

void renderAnimation(bool force = false) {
  const uint32_t now = millis();
  if (!force && now - lastFrameAt < frameIntervalMs())
    return;
  lastFrameAt = now;

  switch (settings.mode) {
  case LampMode::Solid:
    renderSolid();
    break;
  case LampMode::Breathe:
    renderBreathe(now);
    break;
  case LampMode::Rainbow:
    renderRainbow();
    break;
  case LampMode::Wipe:
    renderWipe();
    break;
  case LampMode::Fire:
    renderFire();
    break;
  }
  FastLED.show();
}

void resetAnimation() {
  animationStartedAt = millis();
  lastFrameAt = 0;
  wipeStep = 0;
  rainbowHue = 0;
  if (settings.mode == LampMode::Fire) {
    for (uint16_t i = 0; i < NUM_LEDS; ++i)
      heat[i] = random8(60, 180);
  }
  renderAnimation(true);
}

void scheduleSettingsSave() {
  settingsSavePending = true;
  settingsChangedAt = millis();
}

void saveSettingsIfDue() {
  if (!settingsSavePending || millis() - settingsChangedAt < 1500)
    return;
  preferences.putUInt("color", settings.color);
  preferences.putUChar("brightness", settings.brightness);
  preferences.putUChar("speed", settings.speed);
  preferences.putUChar("mode", static_cast<uint8_t>(settings.mode));
  settingsSavePending = false;
  Serial.println("[NVS] Settings saved");
}

void loadSettings() {
  settings.color = preferences.getUInt("color", settings.color) & 0xFFFFFF;
  settings.brightness =
      constrain(preferences.getUChar("brightness", settings.brightness), 0, 100);
  settings.speed = constrain(preferences.getUChar("speed", settings.speed), 1, 100);
  const uint8_t storedMode =
      preferences.getUChar("mode", static_cast<uint8_t>(settings.mode));
  settings.mode = storedMode <= static_cast<uint8_t>(LampMode::Fire)
                      ? static_cast<LampMode>(storedMode)
                      : LampMode::Solid;
}

void sendState() {
  char json[240];
  snprintf(json, sizeof(json),
           "{\"color\":\"%s\",\"brightness\":%u,\"speed\":%u,"
           "\"mode\":\"%s\",\"brightnessCap\":%u,\"hostname\":\"%s\","
           "\"leds\":%u}",
           colorString().c_str(), settings.brightness, settings.speed,
           modeName(settings.mode), BRIGHTNESS_CAP_PERCENT, MDNS_NAME, NUM_LEDS);
  server.send(200, "application/json", json);
}

bool parseColor(const String &input, uint32_t &result) {
  String value = input;
  if (value.startsWith("#"))
    value.remove(0, 1);
  if (value.length() != 6)
    return false;
  for (char c : value) {
    if (!isxdigit(static_cast<unsigned char>(c)))
      return false;
  }
  result = strtoul(value.c_str(), nullptr, 16) & 0xFFFFFF;
  return true;
}

void handleSet() {
  bool changed = false;
  String error;

  if (server.hasArg("color")) {
    uint32_t parsed;
    if (!parseColor(server.arg("color"), parsed))
      error = "color must be a 6-digit hex value";
    else if (settings.color != parsed) {
      settings.color = parsed;
      changed = true;
    }
  }
  if (error.isEmpty() && server.hasArg("brightness")) {
    const int value = server.arg("brightness").toInt();
    if (value < 0 || value > 100)
      error = "brightness must be 0..100";
    else if (settings.brightness != value) {
      settings.brightness = value;
      changed = true;
    }
  }
  if (error.isEmpty() && server.hasArg("speed")) {
    const int value = server.arg("speed").toInt();
    if (value < 1 || value > 100)
      error = "speed must be 1..100";
    else if (settings.speed != value) {
      settings.speed = value;
      changed = true;
    }
  }
  if (error.isEmpty() && server.hasArg("mode")) {
    LampMode parsed;
    if (!parseMode(server.arg("mode"), parsed))
      error = "unknown mode";
    else if (settings.mode != parsed) {
      settings.mode = parsed;
      changed = true;
    }
  }

  if (!error.isEmpty()) {
    server.send(400, "application/json",
                String("{\"ok\":false,\"error\":\"") + error + "\"}");
    return;
  }

  if (changed) {
    resetAnimation();
    scheduleSettingsSave();
    Serial.printf("[API] color=%s brightness=%u mode=%s speed=%u\n",
                  colorString().c_str(), settings.brightness,
                  modeName(settings.mode), settings.speed);
  }
  sendState();
}

void startWebServer() {
  server.on("/", HTTP_GET,
            []() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/set", HTTP_GET, handleSet);
  server.on("/api/set", HTTP_POST, handleSet);
  server.onNotFound([]() {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
  });
  server.begin();
  Serial.println("[HTTP] Web server started on port 80");
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_NAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_TIMEOUT_MS) {
    delay(350);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected, RSSI %d dBm\n", WiFi.RSSI());
  } else {
    Serial.printf("[WiFi] Connection failed (status %d)\n", WiFi.status());
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(FALLBACK_AP_NAME);
    Serial.printf("[WiFi] Fallback AP started: %s\n", FALLBACK_AP_NAME);
  }

  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] http://%s.local/\n", MDNS_NAME);
  } else {
    Serial.println("[mDNS] Failed to start");
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== ESP32 WiFi Mood Lamp ===");
  Serial.printf("[LED] GPIO %u, count %u, brightness cap %u%%, power guard %u mA\n",
                LED_PIN, NUM_LEDS, BRIGHTNESS_CAP_PERCENT, MAX_MILLIAMPS);

  randomSeed(esp_random());
  FastLED.addLeds<WS2812B, LED_PIN, LED_COLOR_ORDER>(leds, NUM_LEDS)
      .setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
  FastLED.clear(true);

  preferences.begin("moodlamp", false);
  loadSettings();
  resetAnimation();
  Serial.printf("[NVS] Restored color=%s brightness=%u mode=%s speed=%u\n",
                colorString().c_str(), settings.brightness,
                modeName(settings.mode), settings.speed);

  connectWiFi();
  startWebServer();
  Serial.printf("[Ready] Portal: http://%s.local/\n", MDNS_NAME);
}

void loop() {
  server.handleClient();
  renderAnimation();
  saveSettingsIfDue();
  delay(1);
}
