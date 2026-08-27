// Grove Spotify E-Ink Player
// Klassischer ESP32 + Seeed Grove Triple Color E-Ink Display 1,54" (152 × 152).
//
// Spotify wird regelmäßig abgefragt. Das Panel wird jedoch entsprechend der
// Seeed-Empfehlung höchstens alle 180 Sekunden neu aufgebaut.

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <time.h>

// Lokale Zugangsdaten sind optional, damit das öffentliche Projekt auch ohne
// src/secrets.h kompiliert. Ohne echte Werte zeigt das Display den Setup-Fehler.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define SPOTIFY_CLIENT_ID "PASTE_CLIENT_ID"
#define SPOTIFY_CLIENT_SECRET "PASTE_CLIENT_SECRET"
#define SPOTIFY_REFRESH_TOKEN "PASTE_REFRESH_TOKEN"
#endif

// --------------------------------------------------------------- Hardware ---

static constexpr int EINK_RX_PIN = 25;  // ESP32 RX <- gelber Grove-Ausgang
static constexpr int EINK_TX_PIN = 26;  // ESP32 TX -> weißer Grove-Eingang
static constexpr uint32_t EINK_BAUD = 230400;

static constexpr int SCREEN_W = 152;
static constexpr int SCREEN_H = 152;
static constexpr int ROW_BYTES = SCREEN_W / 8;
static constexpr int PLANE_BYTES = ROW_BYTES * SCREEN_H;
static constexpr int BLOCK_BYTES = 76;
static constexpr int BLOCK_COUNT = PLANE_BYTES / BLOCK_BYTES;

static constexpr uint32_t POLL_PLAYING_MS = 15UL * 1000UL;
static constexpr uint32_t POLL_IDLE_MS = 30UL * 1000UL;
static constexpr uint32_t MIN_PANEL_REFRESH_MS = 180UL * 1000UL;
static constexpr uint32_t PANEL_RETRY_MS = 30UL * 1000UL;

static constexpr int TITLE_HEIGHT = 16;
static constexpr int ART_X = 0;
static constexpr int ART_Y = TITLE_HEIGHT;
static constexpr int ART_W = SCREEN_W;
static constexpr int ART_H = SCREEN_H - TITLE_HEIGHT;
// Cover sind quadratisch: auf Displaybreite skalieren und oben/unten gleich beschneiden.
static constexpr int ART_CROP_Y = (ART_W - ART_H) / 2;
static constexpr size_t ART_MAX_BYTES = 120 * 1024;

HardwareSerial eink(2);
GFXcanvas1 blackCanvas(SCREEN_W, SCREEN_H);
GFXcanvas1 redCanvas(SCREEN_W, SCREEN_H);
uint8_t blackPlane[PLANE_BYTES];
uint8_t redPlane[PLANE_BYTES];

enum InkColor : uint8_t { INK_WHITE, INK_BLACK, INK_RED };

// ----------------------------------------------------------------- Zustand ---

struct NowPlaying {
  bool active = false;
  bool playing = false;
  char id[48] = "";
  char title[128] = "";
  char artist[128] = "";
  char device[64] = "";
  char artUrl[192] = "";
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
};

static NowPlaying current;
static bool haveApiData = false;
static int consecutiveFailures = 0;
static char lastError[80] = "";
static char updatedAt[8] = "--:--";

static char accessToken[512] = "";
static uint32_t tokenGoodUntilMs = 0;
static Preferences prefs;

static uint8_t* artData = nullptr;
static size_t artLength = 0;
static char cachedArtUrl[192] = "";

static bool panelHasContent = false;
static bool displayedActive = false;
static bool displayedPlaying = false;
static bool displayedOffline = false;
static char displayedId[48] = "";
static bool pendingRefresh = false;
static uint32_t lastPanelRefreshMs = 0;
static uint32_t lastPanelAttemptMs = 0;
static uint32_t lastPollMs = 0;
static bool authConfigured = false;

// ---------------------------------------------------------- Hilfsfunktionen ---

static void utf8Fold(char* dst, size_t dstLen, const char* src) {
  size_t out = 0;
  auto put = [&](const char* text) {
    while (*text && out + 1 < dstLen) dst[out++] = *text++;
  };

  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
       *p && out + 1 < dstLen;) {
    if (*p < 0x80) {
      dst[out++] = static_cast<char>(*p++);
      continue;
    }
    if (*p == 0xC3 && p[1]) {
      const uint8_t c = 0xC0 | (p[1] & 0x3F);
      p += 2;
      switch (c) {
        case 0xC4: put("Ae"); break; case 0xD6: put("Oe"); break;
        case 0xDC: put("Ue"); break; case 0xE4: put("ae"); break;
        case 0xF6: put("oe"); break; case 0xFC: put("ue"); break;
        case 0xDF: put("ss"); break;
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC5: put("A"); break;
        case 0xC7: put("C"); break;
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: put("E"); break;
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: put("I"); break;
        case 0xD1: put("N"); break;
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD8: put("O"); break;
        case 0xD9: case 0xDA: case 0xDB: put("U"); break;
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE5: put("a"); break;
        case 0xE7: put("c"); break;
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: put("e"); break;
        case 0xEC: case 0xED: case 0xEE: case 0xEF: put("i"); break;
        case 0xF1: put("n"); break;
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF8: put("o"); break;
        case 0xF9: case 0xFA: case 0xFB: put("u"); break;
        default: break;
      }
      continue;
    }
    if (*p == 0xE2 && p[1] == 0x80 && p[2]) {
      const uint8_t c = p[2];
      p += 3;
      if (c == 0x93 || c == 0x94) put("-");
      else if (c == 0x98 || c == 0x99) put("'");
      else if (c == 0x9C || c == 0x9D) put("\"");
      else if (c == 0xA6) put("...");
      continue;
    }
    ++p;
    while ((*p & 0xC0) == 0x80) ++p;
  }
  dst[out] = 0;
}

static void truncateLine(char* text, size_t maxChars) {
  const size_t len = strlen(text);
  if (len <= maxChars) return;
  text[maxChars] = 0;
  if (maxChars >= 2) {
    text[maxChars - 2] = '.';
    text[maxChars - 1] = '.';
  }
}

static void wrapTwoLines(const char* text, char* line1, size_t n1,
                         char* line2, size_t n2, size_t maxChars) {
  line1[0] = line2[0] = 0;
  const size_t len = strlen(text);
  if (len <= maxChars) {
    strlcpy(line1, text, n1);
    return;
  }

  size_t split = maxChars;
  while (split > 0 && text[split] != ' ') --split;
  if (split < maxChars / 2) split = maxChars;
  snprintf(line1, n1, "%.*s", static_cast<int>(split), text);
  const char* rest = text + split;
  while (*rest == ' ') ++rest;
  strlcpy(line2, rest, n2);
  truncateLine(line2, maxChars);
}

static void formatDuration(char* out, size_t n, uint32_t ms) {
  const uint32_t seconds = ms / 1000;
  snprintf(out, n, "%u:%02u", static_cast<unsigned>(seconds / 60),
           static_cast<unsigned>(seconds % 60));
}

static void updateClock() {
  struct tm local;
  if (getLocalTime(&local, 10)) {
    snprintf(updatedAt, sizeof(updatedAt), "%02d:%02d", local.tm_hour, local.tm_min);
  }
}

// --------------------------------------------------------------- Zeichnen ---

static void clearCanvas() {
  blackCanvas.fillScreen(0);
  redCanvas.fillScreen(0);
}

static void setTriPixel(int x, int y, InkColor color) {
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
  blackCanvas.drawPixel(x, y, color == INK_BLACK ? 1 : 0);
  redCanvas.drawPixel(x, y, color == INK_RED ? 1 : 0);
}

static void fillColorRect(int x, int y, int w, int h, InkColor color) {
  blackCanvas.fillRect(x, y, w, h, color == INK_BLACK ? 1 : 0);
  redCanvas.fillRect(x, y, w, h, color == INK_RED ? 1 : 0);
}

static void drawColorRect(int x, int y, int w, int h, InkColor color) {
  blackCanvas.drawRect(x, y, w, h, color == INK_BLACK ? 1 : 0);
  redCanvas.drawRect(x, y, w, h, color == INK_RED ? 1 : 0);
}

static void fillColorCircle(int x, int y, int r, InkColor color) {
  blackCanvas.fillCircle(x, y, r, color == INK_BLACK ? 1 : 0);
  redCanvas.fillCircle(x, y, r, color == INK_RED ? 1 : 0);
}

static void drawColorCircle(int x, int y, int r, InkColor color) {
  blackCanvas.drawCircle(x, y, r, color == INK_BLACK ? 1 : 0);
  redCanvas.drawCircle(x, y, r, color == INK_RED ? 1 : 0);
}

static void drawText(const char* text, int x, int y, InkColor color, int size = 1) {
  blackCanvas.setTextWrap(false);
  redCanvas.setTextWrap(false);
  blackCanvas.setTextSize(size);
  redCanvas.setTextSize(size);
  blackCanvas.setTextColor(color == INK_BLACK ? 1 : 0);
  redCanvas.setTextColor(color == INK_RED ? 1 : 0);
  blackCanvas.setCursor(x, y);
  redCanvas.setCursor(x, y);
  blackCanvas.print(text);
  redCanvas.print(text);
}

static void drawCentered(const char* text, int y, InkColor color, int size = 1) {
  const int width = static_cast<int>(strlen(text)) * 6 * size;
  drawText(text, max(0, (SCREEN_W - width) / 2), y, color, size);
}

static void drawHeader(const char* state) {
  fillColorRect(0, 0, SCREEN_W, 11, INK_RED);
  drawText("SPOTIFY", 3, 2, INK_WHITE);
  const int x = SCREEN_W - static_cast<int>(strlen(state)) * 6 - 3;
  drawText(state, max(3, x), 2, INK_WHITE);
}

static void drawVinylPlaceholder() {
  fillColorRect(ART_X, ART_Y, ART_W, ART_H, INK_WHITE);
  const int centerY = ART_Y + ART_H / 2;
  fillColorCircle(SCREEN_W / 2, centerY, 58, INK_BLACK);
  drawColorCircle(SCREEN_W / 2, centerY, 43, INK_WHITE);
  drawColorCircle(SCREEN_W / 2, centerY, 30, INK_WHITE);
  fillColorCircle(SCREEN_W / 2, centerY, 16, INK_RED);
  fillColorCircle(SCREEN_W / 2, centerY, 3, INK_WHITE);
}

static const uint8_t BAYER4[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static int decodedArtW = 0;
static int decodedArtH = 0;

static InkColor quantizeRgb(uint8_t r, uint8_t g, uint8_t b, int x, int y) {
  const int dither = static_cast<int>(BAYER4[y & 3][x & 3]) - 7;
  const int redScore = static_cast<int>(r) * 2 - g - b;
  if (r > 85 && redScore > 65 + dither * 7) return INK_RED;
  const int luminance = (r * 54 + g * 183 + b * 19) >> 8;
  return luminance < 128 + dither * 7 ? INK_BLACK : INK_WHITE;
}

static bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h,
                      uint16_t* bitmap) {
  if (decodedArtW <= 0 || decodedArtH <= 0) return false;
  for (int py = 0; py < h; ++py) {
    const int sy = y + py;
    int oy0 = ART_Y + sy * ART_W / decodedArtH - ART_CROP_Y;
    int oy1 = ART_Y + (sy + 1) * ART_W / decodedArtH - ART_CROP_Y;
    oy0 = max(oy0, ART_Y);
    oy1 = min(oy1, SCREEN_H);
    if (oy1 <= oy0) continue;
    for (int px = 0; px < w; ++px) {
      const int sx = x + px;
      const int ox0 = ART_X + sx * ART_W / decodedArtW;
      const int ox1 = ART_X + (sx + 1) * ART_W / decodedArtW;
      if (ox1 <= ox0) continue;
      const uint16_t pixel = bitmap[py * w + px];
      const uint8_t r = ((pixel >> 11) & 0x1F) * 255 / 31;
      const uint8_t g = ((pixel >> 5) & 0x3F) * 255 / 63;
      const uint8_t b = (pixel & 0x1F) * 255 / 31;
      const InkColor color = quantizeRgb(r, g, b, ox0, oy0);
      for (int oy = oy0; oy < oy1; ++oy) {
        for (int ox = ox0; ox < ox1; ++ox) setTriPixel(ox, oy, color);
      }
    }
  }
  return true;
}

static bool drawAlbumArt() {
  if (!artData || artLength == 0) return false;
  uint16_t jpgW = 0, jpgH = 0;
  if (TJpgDec.getJpgSize(&jpgW, &jpgH, artData, artLength) != JDR_OK ||
      jpgW == 0 || jpgH == 0) {
    Serial.println("[art] invalid JPEG");
    return false;
  }
  decodedArtW = jpgW;
  decodedArtH = jpgH;
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(jpgOutput);
  fillColorRect(ART_X, ART_Y, ART_W, ART_H, INK_WHITE);
  const JRESULT result = TJpgDec.drawJpg(0, 0, artData, artLength);
  if (result != JDR_OK) {
    Serial.printf("[art] JPEG decode error %d\n", static_cast<int>(result));
    return false;
  }
  return true;
}

static void renderNowPlaying() {
  clearCanvas();
  if (!drawAlbumArt()) drawVinylPlaceholder();

  // Einzige Einblendung: eine klare Titelleiste über dem randfüllenden Cover.
  fillColorRect(0, 0, SCREEN_W, TITLE_HEIGHT, INK_WHITE);
  fillColorRect(0, TITLE_HEIGHT - 2, SCREEN_W, 2, INK_RED);
  char title[32];
  strlcpy(title, current.title, sizeof(title));
  truncateLine(title, 24);
  drawCentered(title, 4, INK_BLACK);
}

static void renderIdle() {
  clearCanvas();
  drawHeader("IDLE");
  fillColorCircle(76, 59, 36, INK_BLACK);
  drawColorCircle(76, 59, 26, INK_WHITE);
  drawColorCircle(76, 59, 18, INK_WHITE);
  fillColorCircle(76, 59, 10, INK_RED);
  fillColorCircle(76, 59, 2, INK_WHITE);
  drawCentered("NOTHING PLAYING", 102, INK_BLACK);
  drawCentered("START SPOTIFY", 116, INK_RED);
  drawCentered(updatedAt, 140, INK_BLACK);
}

static void renderError() {
  clearCanvas();
  drawHeader("ERROR");
  drawColorRect(9, 26, 134, 83, INK_RED);
  drawCentered("SPOTIFY OFFLINE", 39, INK_BLACK);
  drawCentered("CHECK WIFI/AUTH", 57, INK_RED);
  char e1[26], e2[26];
  wrapTwoLines(lastError[0] ? lastError : "No Spotify data", e1, sizeof(e1),
               e2, sizeof(e2), 23);
  drawCentered(e1, 78, INK_BLACK);
  if (e2[0]) drawCentered(e2, 88, INK_BLACK);
  drawCentered("RETRYING", 125, INK_RED);
  drawCentered(updatedAt, 140, INK_BLACK);
}

// --------------------------------------------------------------- Netzwerk ---

static bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.printf("[wifi] connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 50; ++i) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[wifi] connected ip=%s rssi=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
      return true;
    }
    delay(500);
  }
  strlcpy(lastError, "WiFi connection failed", sizeof(lastError));
  Serial.println("[wifi] connection failed");
  return false;
}

static String storedRefreshToken() {
  const String saved = prefs.getString("rtok", "");
  return saved.length() ? saved : String(SPOTIFY_REFRESH_TOKEN);
}

static bool refreshAccessToken() {
  if (!connectWiFi()) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setTimeout(18000);
  http.setConnectTimeout(10000);
  http.useHTTP10(true);
  if (!http.begin(client, "https://accounts.spotify.com/api/token")) {
    strlcpy(lastError, "Token connection failed", sizeof(lastError));
    return false;
  }

  const String basic = base64::encode(String(SPOTIFY_CLIENT_ID) + ":" +
                                      SPOTIFY_CLIENT_SECRET);
  http.addHeader("Authorization", "Basic " + basic);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const String body = "grant_type=refresh_token&refresh_token=" + storedRefreshToken();
  const int code = http.POST(body);
  const String payload = http.getString();
  http.end();
  if (code != HTTP_CODE_OK) {
    snprintf(lastError, sizeof(lastError), "Spotify auth HTTP %d", code);
    Serial.printf("[auth] HTTP %d: %.160s\n", code, payload.c_str());
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    strlcpy(lastError, "Bad token response", sizeof(lastError));
    return false;
  }
  const char* token = doc["access_token"];
  if (!token || !token[0]) {
    strlcpy(lastError, "No access token", sizeof(lastError));
    return false;
  }
  strlcpy(accessToken, token, sizeof(accessToken));
  const uint32_t expires = doc["expires_in"] | 3600;
  tokenGoodUntilMs = millis() + (expires - 60) * 1000UL;
  const char* rotated = doc["refresh_token"];
  if (rotated && rotated[0]) prefs.putString("rtok", rotated);
  Serial.printf("[auth] access token valid for %u seconds\n", static_cast<unsigned>(expires));
  return true;
}

static bool ensureAccessToken(bool force = false) {
  if (!force && accessToken[0] &&
      static_cast<int32_t>(tokenGoodUntilMs - millis()) > 0) return true;
  return refreshAccessToken();
}

static bool fetchNowPlaying() {
  if (!ensureAccessToken()) return false;

  String payload;
  int code = -1;
  for (int attempt = 0; attempt < 2; ++attempt) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(15);
    HTTPClient http;
    http.setTimeout(18000);
    http.setConnectTimeout(10000);
    http.useHTTP10(true);
    if (!http.begin(client,
        "https://api.spotify.com/v1/me/player?additional_types=episode")) {
      strlcpy(lastError, "Player connection failed", sizeof(lastError));
      return false;
    }
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    code = http.GET();
    if (code == HTTP_CODE_OK) payload = http.getString();
    http.end();
    if (code != 401 || attempt == 1) break;
    accessToken[0] = 0;
    if (!ensureAccessToken(true)) return false;
  }

  if (code == HTTP_CODE_NO_CONTENT) {
    current.active = false;
    current.playing = false;
    haveApiData = true;
    lastError[0] = 0;
    updateClock();
    Serial.println("[player] nothing playing");
    return true;
  }
  if (code != HTTP_CODE_OK) {
    snprintf(lastError, sizeof(lastError), "Player HTTP %d", code);
    Serial.printf("[player] HTTP %d\n", code);
    return false;
  }

  JsonDocument filter;
  filter["is_playing"] = true;
  filter["progress_ms"] = true;
  filter["device"]["name"] = true;
  JsonObject itemFilter = filter["item"].to<JsonObject>();
  itemFilter["id"] = true;
  itemFilter["name"] = true;
  itemFilter["duration_ms"] = true;
  itemFilter["artists"][0]["name"] = true;
  itemFilter["show"]["name"] = true;
  itemFilter["album"]["images"][0]["url"] = true;
  itemFilter["album"]["images"][0]["width"] = true;
  itemFilter["images"][0]["url"] = true;
  itemFilter["images"][0]["width"] = true;

  JsonDocument doc;
  const DeserializationError error =
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    snprintf(lastError, sizeof(lastError), "JSON: %s", error.c_str());
    return false;
  }

  JsonObject item = doc["item"];
  if (item.isNull()) {
    current.active = false;
    current.playing = doc["is_playing"] | false;
    haveApiData = true;
    lastError[0] = 0;
    updateClock();
    return true;
  }

  NowPlaying next;
  next.active = true;
  next.playing = doc["is_playing"] | false;
  next.progressMs = doc["progress_ms"] | 0;
  next.durationMs = item["duration_ms"] | 0;
  strlcpy(next.id, item["id"] | "?", sizeof(next.id));
  utf8Fold(next.title, sizeof(next.title), item["name"] | "Unknown");
  utf8Fold(next.device, sizeof(next.device), doc["device"]["name"] | "");

  char artists[160] = "";
  JsonArray artistArray = item["artists"].as<JsonArray>();
  if (!artistArray.isNull()) {
    int count = 0;
    for (JsonObject artist : artistArray) {
      const char* name = artist["name"];
      if (!name) continue;
      if (count++) strlcat(artists, ", ", sizeof(artists));
      strlcat(artists, name, sizeof(artists));
      if (count >= 3) break;
    }
  } else {
    strlcpy(artists, item["show"]["name"] | "Podcast", sizeof(artists));
  }
  utf8Fold(next.artist, sizeof(next.artist), artists);

  JsonArray images = item["album"]["images"].as<JsonArray>();
  if (images.isNull() || images.size() == 0) images = item["images"].as<JsonArray>();
  int bestDistance = INT_MAX;
  if (!images.isNull()) {
    for (JsonObject image : images) {
      const char* url = image["url"];
      const int width = image["width"] | 0;
      if (!url || width <= 0) continue;
      const int distance = abs(width - 300);
      if (distance < bestDistance) {
        bestDistance = distance;
        strlcpy(next.artUrl, url, sizeof(next.artUrl));
      }
    }
  }

  current = next;
  haveApiData = true;
  lastError[0] = 0;
  updateClock();
  Serial.printf("[player] %s '%s' - '%s' (%u/%u s)\n",
                current.playing ? "PLAYING" : "PAUSED", current.title,
                current.artist, static_cast<unsigned>(current.progressMs / 1000),
                static_cast<unsigned>(current.durationMs / 1000));
  return true;
}

static bool fetchAlbumArt() {
  if (!current.artUrl[0]) return false;
  if (artData && strcmp(current.artUrl, cachedArtUrl) == 0) return true;

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setTimeout(20000);
  http.setConnectTimeout(10000);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, current.artUrl)) return false;
  const int code = http.GET();
  const int length = http.getSize();
  if (code != HTTP_CODE_OK || length <= 0 ||
      static_cast<size_t>(length) > ART_MAX_BYTES) {
    Serial.printf("[art] HTTP %d length=%d\n", code, length);
    http.end();
    return false;
  }

  uint8_t* buffer = static_cast<uint8_t*>(malloc(length));
  if (!buffer) {
    http.end();
    strlcpy(lastError, "Album art out of memory", sizeof(lastError));
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int received = 0;
  const uint32_t started = millis();
  while (received < length && millis() - started < 20000) {
    const int available = stream->available();
    if (available <= 0) {
      if (!http.connected()) break;
      delay(10);
      continue;
    }
    const int count = stream->read(buffer + received, length - received);
    if (count > 0) received += count;
  }
  http.end();
  if (received != length) {
    Serial.printf("[art] short read %d/%d\n", received, length);
    free(buffer);
    return false;
  }

  if (artData) free(artData);
  artData = buffer;
  artLength = length;
  strlcpy(cachedArtUrl, current.artUrl, sizeof(cachedArtUrl));
  Serial.printf("[art] downloaded %u bytes\n", static_cast<unsigned>(artLength));
  return true;
}

// --------------------------------------------------------------- Display ---

static void buildPanelPlanes() {
  const uint8_t* black = blackCanvas.getBuffer();
  const uint8_t* red = redCanvas.getBuffer();
  for (int i = 0; i < PLANE_BYTES; ++i) {
    blackPlane[i] = static_cast<uint8_t>(~black[i]);
    redPlane[i] = static_cast<uint8_t>(~red[i]);
  }
}

static bool requestPanelTransfer(int rxPin, int txPin, uint32_t timeoutMs) {
  eink.end();
  delay(50);
  eink.begin(EINK_BAUD, SERIAL_8N1, rxPin, txPin);
  while (eink.available()) eink.read();

  Serial.printf("[panel] probing RX=GPIO%d TX=GPIO%d\n", rxPin, txPin);
  const uint32_t started = millis();
  uint32_t lastRequest = 0;
  while (millis() - started < timeoutMs) {
    if (lastRequest == 0 || millis() - lastRequest >= 750) {
      eink.write('a');
      eink.flush();
      lastRequest = millis();
    }
    while (eink.available()) {
      if (eink.read() == 'b') {
        Serial.printf("[panel] acknowledged on RX=GPIO%d TX=GPIO%d\n", rxPin, txPin);
        return true;
      }
    }
    delay(5);
  }
  return false;
}

static bool beginPanelTransfer() {
  // Der funktionierende Test-Sketch probierte beide Richtungen. Auch hier beide
  // testen, da Grove die UART-Pins bei manchen Revisionen aus Host-Sicht beschriftet.
  if (requestPanelTransfer(EINK_RX_PIN, EINK_TX_PIN, 4000)) return true;
  return requestPanelTransfer(EINK_TX_PIN, EINK_RX_PIN, 7000);
}

static void sendPanelPlane(const uint8_t* plane) {
  for (int block = 0; block < BLOCK_COUNT; ++block) {
    eink.write(plane + block * BLOCK_BYTES, BLOCK_BYTES);
    eink.flush();
    delay(70);
  }
  delay(70);
}

static bool refreshPanel() {
  lastPanelAttemptMs = millis();
  buildPanelPlanes();
  Serial.println("[panel] requesting transfer");
  if (!beginPanelTransfer()) {
    Serial.println("[panel] no acknowledgement; will retry");
    return false;
  }
  delay(2000);
  Serial.println("[panel] sending black plane");
  sendPanelPlane(blackPlane);
  Serial.println("[panel] sending red plane");
  sendPanelPlane(redPlane);
  lastPanelRefreshMs = millis();
  panelHasContent = true;
  Serial.println("[panel] image sent");
  return true;
}

static bool panelRefreshAllowed() {
  return !panelHasContent || millis() - lastPanelRefreshMs >= MIN_PANEL_REFRESH_MS;
}

static void renderDesiredScreen() {
  if (!haveApiData) {
    renderError();
  } else if (!current.active) {
    renderIdle();
  } else {
    fetchAlbumArt();
    renderNowPlaying();
  }
}

static void recordDisplayedState() {
  displayedActive = current.active;
  displayedPlaying = current.playing;
  displayedOffline = consecutiveFailures >= 3;
  strlcpy(displayedId, current.id, sizeof(displayedId));
}

static void tryPendingPanelRefresh() {
  if (!pendingRefresh || !panelRefreshAllowed()) return;
  if (lastPanelAttemptMs && millis() - lastPanelAttemptMs < PANEL_RETRY_MS) return;
  renderDesiredScreen();
  if (refreshPanel()) {
    recordDisplayedState();
    pendingRefresh = false;
  }
}

// ------------------------------------------------------------------ Zyklus ---

static void pollSpotify() {
  const bool ok = fetchNowPlaying();
  if (!ok) {
    ++consecutiveFailures;
    Serial.printf("[cycle] failure %d: %s\n", consecutiveFailures, lastError);
  } else {
    if (consecutiveFailures >= 3 && displayedOffline) pendingRefresh = true;
    consecutiveFailures = 0;
  }

  const bool offline = consecutiveFailures >= 3;
  if (!panelHasContent || current.active != displayedActive ||
      current.playing != displayedPlaying ||
      strcmp(current.id, displayedId) != 0 || offline != displayedOffline) {
    pendingRefresh = true;
  }
  lastPollMs = millis();
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n[boot] Grove Spotify E-Ink Player");
  Serial.printf("[heap] free=%u\n", static_cast<unsigned>(ESP.getFreeHeap()));

  eink.begin(EINK_BAUD, SERIAL_8N1, EINK_RX_PIN, EINK_TX_PIN);
  prefs.begin("grovespot");
  authConfigured = strncmp(SPOTIFY_CLIENT_ID, "PASTE", 5) != 0 &&
                   (prefs.getString("rtok", "").length() > 0 ||
                    strncmp(SPOTIFY_REFRESH_TOKEN, "PASTE", 5) != 0);

  if (!authConfigured) {
    strlcpy(lastError, "Spotify setup needed", sizeof(lastError));
    pendingRefresh = true;
    tryPendingPanelRefresh();
    return;
  }

  pollSpotify();
  tryPendingPanelRefresh();
  Serial.println("[setup] commands: u=update i=info t=refresh-token");
}

void loop() {
  while (Serial.available()) {
    const int command = Serial.read();
    if (command == 'u') {
      pollSpotify();
    } else if (command == 't') {
      ensureAccessToken(true);
    } else if (command == 'i') {
      const uint32_t waitSeconds = panelRefreshAllowed()
          ? 0 : (MIN_PANEL_REFRESH_MS - (millis() - lastPanelRefreshMs)) / 1000;
      Serial.printf("[info] wifi=%d rssi=%d api=%d active=%d playing=%d "
                    "track='%s' artist='%s' pending=%d panelWait=%us heap=%u\n",
                    WiFi.status() == WL_CONNECTED, WiFi.RSSI(), haveApiData,
                    current.active, current.playing, current.title, current.artist,
                    pendingRefresh, static_cast<unsigned>(waitSeconds),
                    static_cast<unsigned>(ESP.getFreeHeap()));
    }
  }

  if (authConfigured) {
    const uint32_t interval = current.playing ? POLL_PLAYING_MS : POLL_IDLE_MS;
    if (millis() - lastPollMs >= interval) pollSpotify();
    if (current.playing && panelRefreshAllowed()) pendingRefresh = true;
  }
  tryPendingPanelRefresh();
  delay(50);
}
