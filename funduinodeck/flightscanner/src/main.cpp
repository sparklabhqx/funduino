#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

// WLAN-Zugangsdaten sind optional. Ohne include/secrets.h startet die Flights-App
// das Funduino-Deck-Captive-Portal für die Ersteinrichtung.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

// -----------------------------------------------------------------------------
// Neon Deck: ein Touch-Launcher mit vier Apps für den ESP32-2432S028R V3.
// Die Display-Pins sind in platformio.ini definiert. Der Touch-Controller
// verwendet den zweiten SPI-Bus des ESP32.
// -----------------------------------------------------------------------------

constexpr int TOUCH_IRQ  = 36;
constexpr int TOUCH_MOSI = 32;
constexpr int TOUCH_MISO = 39;
constexpr int TOUCH_CLK  = 25;
constexpr int TOUCH_CS   = 33;

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr int TOUCH_X_MIN = 200;
constexpr int TOUCH_X_MAX = 3700;
constexpr int TOUCH_Y_MIN = 240;
constexpr int TOUCH_Y_MAX = 3800;

// Fester Standort des Flugscanners (für die Flights-App und den Menükopf).
constexpr float SCANNER_LAT = 52.4310f;
constexpr float SCANNER_LON = 7.0690f;
constexpr char SCANNER_PLACE[] = "Nordhorn DE";

// Farbpalette: Weiß / Graphit #343434 / Anthrazit #292929 / Türkis #35827B.
constexpr uint16_t DECK_BG     = 0x2945;  // #292929 anthrazitfarbener Hintergrund
constexpr uint16_t PANEL_BG    = 0x31A6;  // #343434 graphitfarbene Flächen
constexpr uint16_t PANEL_LIGHT = 0x4228;  // #454545 hervorgehobene Schaltflächen
constexpr uint16_t GRID        = 0x4208;  // #404040 feine Linien
constexpr uint16_t TEXT_DIM    = 0x9CF3;  // #9E9E9E Sekundärtext
constexpr uint16_t TEAL        = 0x340F;  // #35827B Hauptakzent
constexpr uint16_t TEAL_LIGHT  = 0x7EB9;  // #7FD4CB mintfarbene Hervorhebung
constexpr uint16_t TEAL_DARK   = 0x1ACA;  // #1E5A54 dunkel-türkise Füllung
constexpr uint16_t SILVER      = 0xC638;  // #C6C6C6 weichweißer Akzent
constexpr uint16_t SLATE       = 0x4A49;  // #4A4A4A mittleres Grau

TFT_eSPI tft;
TFT_eSprite gameFrame(&tft);
SPIClass touchBus(VSPI);
XPT2046_Touchscreen touchController(TOUCH_CS, TOUCH_IRQ);
bool gameFrameReady = false;

struct TouchState {
  bool down = false;
  bool pressed = false;
  bool released = false;
  int x = 0;
  int y = 0;
  int pressure = 0;
};
TouchState touchState;
bool previousTouchDown = false;

enum AppId : uint8_t { APP_MENU, APP_PAINT, APP_PONG, APP_FLIGHTS, APP_DINO };
AppId currentApp = APP_MENU;

// Vorwärtsdeklarationen für App-Wechsel und Netzwerkfunktionen.
void showMenu();
void goHome();
void stopFlightNetworking();
void startFlightPortal();
void connectSavedWiFi();

// -----------------------------------------------------------------------------
// Gemeinsame Hilfsfunktionen für Display und Touch
// -----------------------------------------------------------------------------

void drawGradientRect(int x, int y, int w, int h,
                      uint8_t r1, uint8_t g1, uint8_t b1,
                      uint8_t r2, uint8_t g2, uint8_t b2) {
  for (int row = 0; row < h; ++row) {
    const int d = max(1, h - 1);
    const uint8_t r = r1 + (r2 - r1) * row / d;
    const uint8_t g = g1 + (g2 - g1) * row / d;
    const uint8_t b = b1 + (b2 - b1) * row / d;
    tft.drawFastHLine(x, y + row, w, tft.color565(r, g, b));
  }
}

uint16_t halfBright(uint16_t color) {
  return (color >> 1) & 0x7BEF;
}

bool readTouch(int &x, int &y, int &pressure) {
  if (!touchController.touched()) return false;
  TS_Point p = touchController.getPoint();
  pressure = p.z;
  x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W - 1);
  y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H - 1);
  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);
  return pressure > 100;
}

void updateTouch() {
  int x = touchState.x;
  int y = touchState.y;
  int pressure = 0;
  const bool down = readTouch(x, y, pressure);
  touchState.down = down;
  touchState.pressed = down && !previousTouchDown;
  touchState.released = !down && previousTouchDown;
  if (down) {
    touchState.x = x;
    touchState.y = y;
    touchState.pressure = pressure;
  }
  if (touchState.pressed) {
    Serial.printf("TOUCH app=%u x=%d y=%d z=%d\n", currentApp, x, y, pressure);
  }
  previousTouchDown = down;
}

void drawDirectHeader(const char *title, uint16_t accent) {
  tft.fillRect(0, 0, SCREEN_W, 32, PANEL_BG);
  tft.drawFastHLine(0, 31, SCREEN_W, accent);
  tft.fillRoundRect(4, 4, 47, 24, 6, PANEL_LIGHT);
  tft.drawRoundRect(4, 4, 47, 24, 6, accent);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_LIGHT);
  tft.drawString("HOME", 27, 16, 1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_BG);
  tft.drawString(title, 61, 16, 2);
}

void drawSpriteHeader(const char *title, uint16_t accent) {
  gameFrame.fillRect(0, 0, SCREEN_W, 32, PANEL_BG);
  gameFrame.drawFastHLine(0, 31, SCREEN_W, accent);
  gameFrame.fillRoundRect(4, 4, 47, 24, 6, PANEL_LIGHT);
  gameFrame.drawRoundRect(4, 4, 47, 24, 6, accent);
  gameFrame.setTextDatum(MC_DATUM);
  gameFrame.setTextColor(TFT_WHITE, PANEL_LIGHT);
  gameFrame.drawString("HOME", 27, 16, 1);
  gameFrame.setTextDatum(ML_DATUM);
  gameFrame.setTextColor(TFT_WHITE, PANEL_BG);
  gameFrame.drawString(title, 61, 16, 2);
}

bool homePressedAtTop() {
  return touchState.pressed && touchState.x <= 55 && touchState.y <= 34;
}

bool createGameFrame() {
  if (gameFrameReady) gameFrame.deleteSprite();
  gameFrame.setColorDepth(8);
  gameFrameReady = gameFrame.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  if (!gameFrameReady) {
    tft.fillScreen(DECK_BG);
    drawDirectHeader("GAME", SILVER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, DECK_BG);
    tft.drawString("Not enough memory", 160, 118, 2);
  }
  return gameFrameReady;
}

void splashScreen() {
  drawGradientRect(0, 0, SCREEN_W, SCREEN_H, 35, 35, 35, 52, 52, 52);
  for (int radius = 56; radius >= 47; --radius) {
    tft.drawCircle(160, 103, radius, (radius & 1) ? SILVER : TEAL);
  }
  tft.fillCircle(160, 103, 5, TEAL_LIGHT);
  tft.fillCircle(109, 83, 3, TEAL);
  tft.fillCircle(206, 129, 3, SILVER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("NEON DECK", 160, 103, 4);
  tft.setTextColor(TEAL);
  tft.drawString("FOUR TOUCH APPS", 160, 154, 2);
  tft.setTextColor(TEXT_DIM);
  tft.drawString("ESP32 SMART DISPLAY", 160, 191, 1);
  delay(1000);
}

// -----------------------------------------------------------------------------
// Startmenü
// -----------------------------------------------------------------------------

constexpr uint16_t MENU_SHADOW = 0x18E3;  // #1C1C1C Schlagschatten

struct MenuTile {
  int x, y;
  uint16_t accent;
  const char *title;
  const char *subtitle;
};
constexpr int MENU_TILE_W = 148;
constexpr int MENU_TILE_H = 88;
const MenuTile menuTiles[] = {
  {8,   46, TEAL,       "PAINT",    "canvas & brushes"},
  {164, 46, SILVER,     "PONG",     "vs computer"},
  {8,  142, TEAL_LIGHT, "FLIGHTS",  "live airspace"},
  {164, 142, TEAL,      "DINO RUN", "tap to jump"},
};
uint32_t menuPulseLast = 0;
bool menuPulseOn = true;

void drawPaintIcon(int x, int y) {
  tft.fillCircle(x - 5, y - 4, 5, TEAL);
  tft.fillCircle(x + 5, y - 7, 4, TEAL_LIGHT);
  tft.fillCircle(x + 8, y + 2, 4, SILVER);
  tft.drawLine(x - 10, y + 11, x + 5, y - 1, TFT_WHITE);
  tft.drawLine(x - 9, y + 12, x + 6, y, TFT_WHITE);
  tft.fillCircle(x - 10, y + 12, 2, TEAL_LIGHT);
}

void drawPongIcon(int x, int y) {
  tft.fillRoundRect(x - 14, y - 9, 4, 19, 2, TFT_WHITE);
  tft.fillRoundRect(x + 10, y - 7, 4, 16, 2, TEAL_LIGHT);
  tft.fillCircle(x - 1, y - 2, 3, TFT_WHITE);
  tft.fillCircle(x - 6, y + 3, 1, TEXT_DIM);
  tft.fillCircle(x - 9, y + 6, 1, SLATE);
}

void drawRadarIcon(int x, int y) {
  tft.fillTriangle(x, y, x + 9, y - 11, x + 13, y - 4, TEAL);
  tft.drawCircle(x, y, 14, TEAL_LIGHT);
  tft.drawCircle(x, y, 8, halfBright(TEAL_LIGHT));
  tft.drawLine(x, y, x + 10, y - 10, TEAL_LIGHT);
  tft.fillCircle(x - 5, y + 5, 1, TFT_WHITE);
  tft.fillCircle(x + 7, y + 7, 1, TFT_WHITE);
  tft.fillCircle(x - 8, y - 6, 1, TFT_WHITE);
}

void drawDinoIcon(int x, int y) {
  tft.fillRect(x - 9, y - 3, 12, 9, TEAL);
  tft.fillRect(x, y - 11, 10, 9, TEAL);
  tft.fillRect(x + 6, y - 9, 2, 2, DECK_BG);
  tft.fillRect(x - 7, y + 6, 3, 5, TEAL);
  tft.fillRect(x - 1, y + 6, 3, 5, TEAL);
  tft.fillTriangle(x - 9, y - 1, x - 14, y - 6, x - 9, y + 4, TEAL);
  tft.drawFastHLine(x - 12, y + 13, 25, SLATE);
}

void drawMenuTile(const MenuTile &t) {
  tft.fillRoundRect(t.x + 3, t.y + 4, MENU_TILE_W, MENU_TILE_H, 12, MENU_SHADOW);
  tft.fillRoundRect(t.x, t.y, MENU_TILE_W, MENU_TILE_H, 12, PANEL_BG);
  tft.drawFastHLine(t.x + 12, t.y + 1, MENU_TILE_W - 24, PANEL_LIGHT);  // oberer Glanzrand
  tft.drawFastHLine(t.x + 12, t.y + MENU_TILE_H - 2, MENU_TILE_W - 24, MENU_SHADOW);
  tft.drawRoundRect(t.x, t.y, MENU_TILE_W, MENU_TILE_H, 12, t.accent);
  tft.drawRoundRect(t.x + 1, t.y + 1, MENU_TILE_W - 2, MENU_TILE_H - 2, 11,
                    halfBright(t.accent));

  const int cx = t.x + 30;
  const int cy = t.y + MENU_TILE_H / 2;
  tft.fillCircle(cx, cy, 21, TEAL_DARK);
  tft.drawCircle(cx, cy, 22, halfBright(t.accent));
  tft.drawCircle(cx, cy, 21, t.accent);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_BG);
  tft.drawString(t.title, t.x + 58, t.y + 33, 2);
  tft.drawString(t.title, t.x + 59, t.y + 33, 2);  // doppelt gezeichnet = fett
  tft.setTextColor(TEXT_DIM, PANEL_BG);
  tft.drawString(t.subtitle, t.x + 58, t.y + 53, 1);

  const int ax = t.x + MENU_TILE_W - 13;
  tft.drawLine(ax - 4, cy - 5, ax, cy, t.accent);  // Pfeilspitze
  tft.drawLine(ax, cy, ax - 4, cy + 5, t.accent);
}

void flashMenuTile(int index) {
  const MenuTile &t = menuTiles[index];
  tft.drawRoundRect(t.x, t.y, MENU_TILE_W, MENU_TILE_H, 12, TFT_WHITE);
  tft.drawRoundRect(t.x + 1, t.y + 1, MENU_TILE_W - 2, MENU_TILE_H - 2, 11, TFT_WHITE);
  delay(90);
}

void showMenu() {
  currentApp = APP_MENU;
  tft.fillScreen(DECK_BG);
  for (int y = 48; y < 232; y += 16) {              // dezente Punktstruktur
    for (int x = 10 + ((y >> 4) & 1) * 8; x < SCREEN_W; x += 16) {
      tft.drawPixel(x, y, GRID);
    }
  }

  drawGradientRect(0, 0, SCREEN_W, 40, 46, 46, 46, 58, 58, 58);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TEAL_LIGHT);
  tft.drawString("FUNDUINO", 10, 19, 4);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("DECK", 12 + tft.textWidth("FUNDUINO", 4), 19, 4);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(SILVER);
  tft.drawString(SCANNER_PLACE, 304, 12, 1);
  tft.setTextColor(TEXT_DIM);
  tft.drawString("4 APPS READY", 304, 27, 1);
  menuPulseOn = true;
  tft.fillCircle(312, 19, 3, TEAL_LIGHT);
  tft.drawFastHLine(0, 40, SCREEN_W, TEAL);
  tft.drawFastHLine(0, 41, SCREEN_W, halfBright(TEAL));
  tft.drawFastHLine(0, 40, 92, TEAL_LIGHT);

  for (const MenuTile &t : menuTiles) drawMenuTile(t);
  drawPaintIcon(menuTiles[0].x + 30, menuTiles[0].y + MENU_TILE_H / 2);
  drawPongIcon(menuTiles[1].x + 30, menuTiles[1].y + MENU_TILE_H / 2);
  drawRadarIcon(menuTiles[2].x + 30, menuTiles[2].y + MENU_TILE_H / 2);
  drawDinoIcon(menuTiles[3].x + 30, menuTiles[3].y + MENU_TILE_H / 2);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TEXT_DIM, DECK_BG);
  tft.drawString("TAP A TILE TO LAUNCH", 160, 236, 1);
  Serial.println("NEON_DECK_MENU_READY");
}

// -----------------------------------------------------------------------------
// Paint-App
// -----------------------------------------------------------------------------

constexpr int PAINT_TOOLBAR_H = 44;
constexpr int PAINT_STATUS_Y = 220;
constexpr int PAINT_BOTTOM = 219;
constexpr uint16_t PAINT_CANVAS = 0x2104;  // #212121, eine Stufe dunkler als DECK_BG
const uint16_t paintPalette[] = {
  TEAL, TEAL_LIGHT, SILVER, 0x8C51, TEAL_DARK, SLATE, TFT_WHITE
};
const char *paintColorNames[] = {
  "TEAL", "MINT", "SILVER", "GREY", "DEEP", "SLATE", "WHITE"
};
constexpr int PAINT_COLORS = sizeof(paintPalette) / sizeof(paintPalette[0]);
const int paintBrushSizes[] = {2, 4, 7};
int paintColor = 0;
int paintBrush = 1;
bool paintDrawing = false;
bool paintWelcome = false;
int paintLastX = 0;
int paintLastY = 0;
uint32_t paintStrokes = 0;

void drawPaintStatus() {
  tft.fillRect(0, PAINT_STATUS_Y, SCREEN_W, 20, PANEL_BG);
  tft.drawFastHLine(0, PAINT_STATUS_Y, SCREEN_W, GRID);
  tft.fillRoundRect(4, 222, 50, 16, 4, PANEL_LIGHT);
  tft.drawRoundRect(4, 222, 50, 16, 4, TEAL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_LIGHT);
  tft.drawString("HOME", 29, 230, 1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(paintPalette[paintColor], PANEL_BG);
  tft.drawString(paintColorNames[paintColor], 61, 230, 1);
  char info[40];
  snprintf(info, sizeof(info), "BRUSH %d  STROKES %lu", paintBrushSizes[paintBrush],
           static_cast<unsigned long>(paintStrokes));
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TEXT_DIM, PANEL_BG);
  tft.drawString(info, 314, 230, 1);
}

void drawPaintToolbar() {
  tft.fillRect(0, 0, SCREEN_W, PAINT_TOOLBAR_H, PANEL_BG);
  tft.drawFastHLine(0, PAINT_TOOLBAR_H - 1, SCREEN_W, GRID);
  for (int i = 0; i < PAINT_COLORS; ++i) {
    const int x = 16 + i * 27;
    if (i == paintColor) {
      tft.fillCircle(x, 21, 14, TFT_WHITE);
      tft.fillCircle(x, 21, 12, PANEL_BG);
    }
    tft.fillCircle(x, 21, 9, paintPalette[i]);
    tft.drawCircle(x, 21, 9, TFT_WHITE);
  }
  tft.fillRoundRect(197, 6, 45, 31, 7, PANEL_LIGHT);
  tft.drawRoundRect(197, 6, 45, 31, 7, GRID);
  tft.fillCircle(210, 21, paintBrushSizes[paintBrush], paintPalette[paintColor]);
  tft.drawCircle(210, 21, paintBrushSizes[paintBrush] + 1, TEXT_DIM);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_LIGHT);
  tft.drawString("SIZE", 238, 21, 1);
  tft.fillRoundRect(247, 6, 69, 31, 7, PANEL_LIGHT);
  tft.drawRoundRect(247, 6, 69, 31, 7, SILVER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_LIGHT);
  tft.drawString("CLEAR", 281, 21, 2);
}

void clearPaintCanvas() {
  tft.fillRect(0, PAINT_TOOLBAR_H, SCREEN_W, PAINT_STATUS_Y - PAINT_TOOLBAR_H, PAINT_CANVAS);
  for (int y = PAINT_TOOLBAR_H + 12; y < PAINT_BOTTOM; y += 16) {
    for (int x = 8 + ((y / 16) & 1) * 8; x < SCREEN_W; x += 16) {
      tft.drawPixel(x, y, GRID);
    }
  }
  paintStrokes = 0;
  drawPaintStatus();
}

void drawPaintWelcome() {
  tft.fillRoundRect(52, 87, 216, 82, 12, PANEL_BG);
  tft.drawRoundRect(52, 87, 216, 82, 12, SILVER);
  tft.drawRoundRect(55, 90, 210, 76, 10, TEAL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_BG);
  tft.drawString("DRAW HERE!", 160, 113, 4);
  tft.setTextColor(TEAL, PANEL_BG);
  tft.drawString("finger or stylus", 160, 145, 2);
  paintWelcome = true;
}

void stampPaintBrush(int x, int y) {
  const int radius = paintBrushSizes[paintBrush];
  if (radius >= 4) tft.fillCircle(x, y, radius + 1, halfBright(paintPalette[paintColor]));
  tft.fillCircle(x, y, radius, paintPalette[paintColor]);
  if (radius >= 4 && paintColor != PAINT_COLORS - 1) {
    tft.fillCircle(x - radius / 3, y - radius / 3, max(1, radius / 3), TFT_WHITE);
  }
}

void drawPaintStroke(int x0, int y0, int x1, int y1) {
  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int distance = max(abs(dx), abs(dy));
  const int spacing = max(1, paintBrushSizes[paintBrush] / 2);
  const int samples = max(1, distance / spacing);
  for (int i = 0; i <= samples; ++i) {
    const int x = x0 + dx * i / samples;
    const int y = y0 + dy * i / samples;
    if (y >= PAINT_TOOLBAR_H + paintBrushSizes[paintBrush] &&
        y <= PAINT_BOTTOM - paintBrushSizes[paintBrush]) {
      stampPaintBrush(x, y);
    }
  }
}

void openPaint() {
  currentApp = APP_PAINT;
  paintDrawing = false;
  clearPaintCanvas();
  drawPaintToolbar();
  drawPaintStatus();
  drawPaintWelcome();
  Serial.println("APP_PAINT_READY");
}

void handlePaintToolbar(int x) {
  if (x < 190) {
    int nearest = (x - 16 + 13) / 27;
    paintColor = constrain(nearest, 0, PAINT_COLORS - 1);
    drawPaintToolbar();
    drawPaintStatus();
  } else if (x < 245) {
    paintBrush = (paintBrush + 1) % 3;
    drawPaintToolbar();
    drawPaintStatus();
  } else {
    paintWelcome = false;
    clearPaintCanvas();
    drawPaintToolbar();
  }
}

void loopPaint() {
  if (touchState.pressed && touchState.y >= PAINT_STATUS_Y && touchState.x < 59) {
    goHome();
    return;
  }
  if (touchState.pressed) {
    if (touchState.y < PAINT_TOOLBAR_H) {
      handlePaintToolbar(touchState.x);
      paintDrawing = false;
    } else if (touchState.y <= PAINT_BOTTOM) {
      if (paintWelcome) {
        paintWelcome = false;
        clearPaintCanvas();
      }
      paintDrawing = true;
      paintLastX = touchState.x;
      paintLastY = touchState.y;
      stampPaintBrush(paintLastX, paintLastY);
      ++paintStrokes;
    }
  } else if (touchState.down && paintDrawing && touchState.y >= PAINT_TOOLBAR_H &&
             touchState.y <= PAINT_BOTTOM) {
    drawPaintStroke(paintLastX, paintLastY, touchState.x, touchState.y);
    paintLastX = touchState.x;
    paintLastY = touchState.y;
  }
  if (touchState.released && paintDrawing) {
    paintDrawing = false;
    drawPaintStatus();
  }
}

// -----------------------------------------------------------------------------
// Pong-App
// -----------------------------------------------------------------------------

float pongBallX = 160;
float pongBallY = 130;
float pongVX = 150;
float pongVY = 105;
float pongPlayerY = 135;
float pongCpuY = 135;
int pongPlayerScore = 0;
int pongCpuScore = 0;
bool pongWaiting = true;
bool pongGameOver = false;
uint32_t pongLastTick = 0;
uint32_t pongLastFrame = 0;

void preparePongServe() {
  pongBallX = 160;
  pongBallY = 133;
  pongWaiting = true;
  pongVX = (random(0, 2) ? 150.0f : -150.0f);
  pongVY = random(-110, 111);
  if (abs(pongVY) < 55) pongVY = pongVY < 0 ? -75 : 75;
}

void resetPong() {
  pongPlayerScore = 0;
  pongCpuScore = 0;
  pongPlayerY = 135;
  pongCpuY = 135;
  pongGameOver = false;
  preparePongServe();
  pongLastTick = millis();
}

void renderPong() {
  if (!gameFrameReady) return;
  gameFrame.fillSprite(DECK_BG);
  drawSpriteHeader("NEON PONG", SILVER);
  gameFrame.setTextDatum(MC_DATUM);
  gameFrame.setTextColor(TFT_WHITE, PANEL_BG);
  char score[12];
  snprintf(score, sizeof(score), "%d  :  %d", pongPlayerScore, pongCpuScore);
  gameFrame.drawString(score, 252, 16, 2);

  for (int y = 38; y < SCREEN_H; y += 13) gameFrame.drawFastVLine(159, y, 7, GRID);
  gameFrame.drawRect(7, 37, 306, 198, GRID);
  gameFrame.fillRoundRect(13, static_cast<int>(pongPlayerY) - 23, 7, 46, 3, TEAL);
  gameFrame.fillRoundRect(300, static_cast<int>(pongCpuY) - 23, 7, 46, 3, SILVER);
  gameFrame.fillCircle(static_cast<int>(pongBallX), static_cast<int>(pongBallY), 5, TFT_WHITE);
  gameFrame.drawCircle(static_cast<int>(pongBallX), static_cast<int>(pongBallY), 7, 0x7BEF);

  if (pongWaiting && !pongGameOver) {
    gameFrame.fillRoundRect(87, 99, 146, 48, 9, PANEL_BG);
    gameFrame.drawRoundRect(87, 99, 146, 48, 9, TEAL);
    gameFrame.setTextColor(TFT_WHITE, PANEL_BG);
    gameFrame.drawString("TOUCH TO SERVE", 160, 117, 2);
    gameFrame.setTextColor(TEXT_DIM, PANEL_BG);
    gameFrame.drawString("drag to move", 160, 137, 1);
  }
  if (pongGameOver) {
    gameFrame.fillRoundRect(78, 91, 164, 69, 10, PANEL_BG);
    gameFrame.drawRoundRect(78, 91, 164, 69, 10, TEAL_LIGHT);
    gameFrame.setTextColor(TFT_WHITE, PANEL_BG);
    gameFrame.drawString(pongPlayerScore > pongCpuScore ? "YOU WIN!" : "CPU WINS", 160, 112, 4);
    gameFrame.setTextColor(TEAL, PANEL_BG);
    gameFrame.drawString("tap to play again", 160, 145, 1);
  }
  gameFrame.pushSprite(0, 0);
}

void openPong() {
  currentApp = APP_PONG;
  if (!createGameFrame()) return;
  resetPong();
  renderPong();
  Serial.println("APP_PONG_READY");
}

void loopPong() {
  if (homePressedAtTop()) {
    goHome();
    return;
  }
  if (!gameFrameReady) return;
  if (touchState.down && touchState.y > 35) {
    pongPlayerY = constrain(touchState.y, 62, 211);
  }
  if (touchState.pressed && touchState.y > 34) {
    if (pongGameOver) resetPong();
    pongWaiting = false;
  }

  const uint32_t now = millis();
  float dt = (now - pongLastTick) / 1000.0f;
  pongLastTick = now;
  if (dt > 0.05f) dt = 0.05f;

  if (!pongWaiting && !pongGameOver) {
    pongBallX += pongVX * dt;
    pongBallY += pongVY * dt;
    if (pongBallY < 43) {
      pongBallY = 43;
      pongVY = abs(pongVY);
    } else if (pongBallY > 229) {
      pongBallY = 229;
      pongVY = -abs(pongVY);
    }

    const float cpuTarget = pongBallY;
    const float cpuStep = 115.0f * dt;
    if (pongCpuY < cpuTarget - 3) pongCpuY += cpuStep;
    if (pongCpuY > cpuTarget + 3) pongCpuY -= cpuStep;
    pongCpuY = constrain(pongCpuY, 62.0f, 211.0f);

    if (pongVX < 0 && pongBallX <= 23 && pongBallX >= 12 &&
        abs(pongBallY - pongPlayerY) <= 29) {
      pongBallX = 24;
      pongVX = abs(pongVX) * 1.04f;
      pongVY += (pongBallY - pongPlayerY) * 4.0f;
    }
    if (pongVX > 0 && pongBallX >= 297 && pongBallX <= 309 &&
        abs(pongBallY - pongCpuY) <= 29) {
      pongBallX = 296;
      pongVX = -abs(pongVX) * 1.04f;
      pongVY += (pongBallY - pongCpuY) * 3.3f;
    }
    pongVY = constrain(pongVY, -230.0f, 230.0f);

    if (pongBallX < -8) {
      ++pongCpuScore;
      if (pongCpuScore >= 7) pongGameOver = true;
      else preparePongServe();
    } else if (pongBallX > 328) {
      ++pongPlayerScore;
      if (pongPlayerScore >= 7) pongGameOver = true;
      else preparePongServe();
    }
  }

  if (now - pongLastFrame >= 33) {
    pongLastFrame = now;
    renderPong();
  }
}

// -----------------------------------------------------------------------------
// Dino-Run-App
// -----------------------------------------------------------------------------

struct DinoObstacle {
  float x;
  int w;
  int h;
};
DinoObstacle dinoObstacles[2];
constexpr float DINO_GROUND = 207.0f;
constexpr int DINO_X = 42;
constexpr int DINO_W = 24;
constexpr int DINO_H = 31;
float dinoY = DINO_GROUND - DINO_H;
float dinoVelocity = 0;
float dinoSpeed = 122;
float dinoScoreFloat = 0;
uint32_t dinoBest = 0;
bool dinoRunning = false;
bool dinoGameOver = false;
uint32_t dinoLastTick = 0;
uint32_t dinoLastFrame = 0;

void resetDino(bool startNow) {
  dinoY = DINO_GROUND - DINO_H;
  dinoVelocity = 0;
  dinoSpeed = 122;
  dinoScoreFloat = 0;
  dinoObstacles[0] = {340.0f, 14, 30};
  dinoObstacles[1] = {535.0f, 20, 42};
  dinoRunning = startNow;
  dinoGameOver = false;
  dinoLastTick = millis();
}

bool dinoOnGround() {
  return dinoY >= DINO_GROUND - DINO_H - 0.5f;
}

void jumpDino() {
  if (dinoOnGround()) dinoVelocity = -378.0f;
}

void drawDinoSprite(int x, int y) {
  gameFrame.fillRect(x, y + 10, 18, 15, TEAL);
  gameFrame.fillRect(x + 12, y, 12, 14, TEAL);
  gameFrame.fillRect(x + 20, y + 4, 2, 2, DECK_BG);
  gameFrame.fillRect(x + 19, y + 11, 7, 3, TEAL);
  gameFrame.fillTriangle(x, y + 13, x - 10, y + 7, x, y + 21, TEAL);
  gameFrame.fillRect(x + 2, y + 24, 5, 7, TEAL);
  gameFrame.fillRect(x + 13, y + 24, 5, 7, TEAL);
  gameFrame.drawPixel(x + 23, y + 4, TFT_WHITE);
}

void renderDino() {
  if (!gameFrameReady) return;
  gameFrame.fillSprite(TFT_WHITE);
  drawSpriteHeader("DINO RUN", TEAL_LIGHT);
  gameFrame.setTextDatum(MR_DATUM);
  gameFrame.setTextColor(TFT_WHITE, PANEL_BG);
  char score[34];
  snprintf(score, sizeof(score), "SCORE %04lu  BEST %04lu",
           static_cast<unsigned long>(dinoScoreFloat),
           static_cast<unsigned long>(dinoBest));
  gameFrame.drawString(score, 314, 16, 1);

  // Bewegte Wolken und Wüstendetails.
  const int cloudShift = static_cast<int>(dinoScoreFloat * 2) % 360;
  for (int i = 0; i < 3; ++i) {
    int cx = (70 + i * 145 - cloudShift + 720) % 430 - 30;
    int cy = 62 + (i & 1) * 24;
    gameFrame.fillCircle(cx, cy, 8, 0xE71C);
    gameFrame.fillCircle(cx + 9, cy - 4, 10, 0xE71C);
    gameFrame.fillCircle(cx + 19, cy, 7, 0xE71C);
    gameFrame.fillRect(cx, cy, 20, 8, 0xE71C);
  }
  gameFrame.drawFastHLine(0, static_cast<int>(DINO_GROUND), SCREEN_W, 0x6B4D);
  for (int x = -static_cast<int>(dinoScoreFloat * 5) % 30; x < SCREEN_W; x += 30) {
    gameFrame.drawFastHLine(x, 219 + (x & 3), 11, 0xB596);
  }

  for (const DinoObstacle &o : dinoObstacles) {
    const int ox = static_cast<int>(o.x);
    const int oy = static_cast<int>(DINO_GROUND) - o.h;
    gameFrame.fillRoundRect(ox, oy, o.w, o.h, 3, SLATE);
    gameFrame.fillRect(ox - 5, oy + 9, 6, 5, SLATE);
    gameFrame.fillRect(ox - 5, oy + 4, 4, 10, SLATE);
    if (o.w > 16) {
      gameFrame.fillRect(ox + o.w - 1, oy + 15, 6, 5, SLATE);
      gameFrame.fillRect(ox + o.w + 2, oy + 8, 4, 12, SLATE);
    }
  }
  drawDinoSprite(DINO_X, static_cast<int>(dinoY));

  gameFrame.setTextDatum(MC_DATUM);
  if (!dinoRunning && !dinoGameOver) {
    gameFrame.fillRoundRect(83, 97, 154, 54, 9, PANEL_BG);
    gameFrame.drawRoundRect(83, 97, 154, 54, 9, TEAL_LIGHT);
    gameFrame.setTextColor(TFT_WHITE, PANEL_BG);
    gameFrame.drawString("TAP TO RUN", 160, 116, 2);
    gameFrame.setTextColor(TEXT_DIM, PANEL_BG);
    gameFrame.drawString("tap again to jump", 160, 138, 1);
  }
  if (dinoGameOver) {
    gameFrame.fillRoundRect(77, 90, 166, 71, 10, PANEL_BG);
    gameFrame.drawRoundRect(77, 90, 166, 71, 10, TEAL);
    gameFrame.setTextColor(TFT_WHITE, PANEL_BG);
    gameFrame.drawString("GAME OVER", 160, 113, 4);
    gameFrame.setTextColor(TEAL, PANEL_BG);
    gameFrame.drawString("tap to restart", 160, 146, 1);
  }
  gameFrame.pushSprite(0, 0);
}

void openDino() {
  currentApp = APP_DINO;
  if (!createGameFrame()) return;
  resetDino(false);
  renderDino();
  Serial.println("APP_DINO_READY");
}

void loopDino() {
  if (homePressedAtTop()) {
    goHome();
    return;
  }
  if (!gameFrameReady) return;
  if (touchState.pressed && touchState.y > 34) {
    if (dinoGameOver) {
      resetDino(true);
      jumpDino();
    } else if (!dinoRunning) {
      dinoRunning = true;
      jumpDino();
    } else {
      jumpDino();
    }
  }

  const uint32_t now = millis();
  float dt = (now - dinoLastTick) / 1000.0f;
  dinoLastTick = now;
  if (dt > 0.05f) dt = 0.05f;

  if (dinoRunning && !dinoGameOver) {
    dinoVelocity += 940.0f * dt;
    dinoY += dinoVelocity * dt;
    if (dinoY > DINO_GROUND - DINO_H) {
      dinoY = DINO_GROUND - DINO_H;
      dinoVelocity = 0;
    }
    dinoSpeed = min(235.0f, dinoSpeed + dt * 2.4f);
    dinoScoreFloat += dt * dinoSpeed / 9.0f;

    for (int i = 0; i < 2; ++i) {
      DinoObstacle &o = dinoObstacles[i];
      o.x -= dinoSpeed * dt;
      if (o.x + o.w < 0) {
        const float otherX = dinoObstacles[1 - i].x;
        o.x = max(330.0f, otherX + static_cast<float>(random(145, 245)));
        o.w = random(0, 2) ? 14 : 20;
        o.h = random(27, 45);
      }
      const float dinoLeft = DINO_X + 3;
      const float dinoRight = DINO_X + DINO_W - 2;
      const float dinoTop = dinoY + 3;
      const float dinoBottom = dinoY + DINO_H - 1;
      const float obstacleTop = DINO_GROUND - o.h;
      if (dinoRight > o.x && dinoLeft < o.x + o.w &&
          dinoBottom > obstacleTop && dinoTop < DINO_GROUND) {
        dinoRunning = false;
        dinoGameOver = true;
        dinoBest = max(dinoBest, static_cast<uint32_t>(dinoScoreFloat));
        Serial.printf("DINO_GAME_OVER score=%lu\n",
                      static_cast<unsigned long>(dinoScoreFloat));
      }
    }
  }

  if (now - dinoLastFrame >= 33) {
    dinoLastFrame = now;
    renderDino();
  }
}

// -----------------------------------------------------------------------------
// Live-Flugscanner (ADS-B-Daten von adsb.lol)
// -----------------------------------------------------------------------------

WebServer portalServer(80);
DNSServer dnsServer;
Preferences preferences;
bool portalRoutesConfigured = false;
bool portalRunning = false;
bool portalConnectPending = false;
String pendingSsid;
String pendingPassword;

enum FlightState : uint8_t {
  FLIGHT_CONNECTING,
  FLIGHT_PORTAL,
  FLIGHT_LOCATING,
  FLIGHT_FETCHING,
  FLIGHT_READY,
  FLIGHT_ERROR
};
FlightState flightState = FLIGHT_CONNECTING;
uint32_t flightStateStarted = 0;
uint32_t flightLastFetch = 0;
float scannerLatitude = 0;
float scannerLongitude = 0;
char scannerLocation[32] = "your area";
char flightError[80] = "Unknown error";
int flightReportedTotal = 0;

struct Aircraft {
  char callsign[10];
  char type[7];
  int altitude;
  int speed;
  int track;
  float distance;
  float bearing;
};
constexpr int MAX_AIRCRAFT = 18;
Aircraft aircraft[MAX_AIRCRAFT];
int aircraftCount = 0;

String htmlEscape(const String &input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (c == '&') output += F("&amp;");
    else if (c == '<') output += F("&lt;");
    else if (c == '>') output += F("&gt;");
    else if (c == '\"') output += F("&quot;");
    else output += c;
  }
  return output;
}

String buildPortalPage() {
  String page;
  page.reserve(4500);
  page += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Neon Deck Wi-Fi</title><style>body{margin:0;background:#292929;color:#f2f2f2;font-family:system-ui}"
            ".c{max-width:430px;margin:30px auto;padding:24px}h1{color:#58b8ae}"
            ".box{background:#343434;border:1px solid #35827b;border-radius:18px;padding:22px}"
            "label{display:block;margin:16px 0 6px;color:#9e9e9e}input{box-sizing:border-box;width:100%;padding:13px;"
            "border-radius:10px;border:1px solid #4a4a4a;background:#212121;color:white;font-size:16px}"
            "button{width:100%;margin-top:22px;padding:14px;border:0;border-radius:11px;background:#35827b;"
            "color:#fff;font-weight:800;font-size:16px}.note{font-size:13px;color:#8a8a8a;margin-top:18px}</style></head>"
            "<body><div class='c'><h1>NEON DECK</h1><div class='box'><h2>Connect Flight Radar</h2>"
            "<form method='post' action='/save'><label>Wi-Fi network</label>"
            "<input name='ssid' list='nets' placeholder='Network name' required><datalist id='nets'>");
  const int networks = WiFi.scanComplete();
  if (networks > 0) {
    for (int i = 0; i < networks && i < 20; ++i) {
      page += F("<option value='");
      page += htmlEscape(WiFi.SSID(i));
      page += F("'>");
    }
  }
  page += F("</datalist><label>Password</label><input name='password' type='password' placeholder='Wi-Fi password'>"
            "<button type='submit'>CONNECT</button></form>"
            "<div class='note'>Credentials stay on your ESP32. Flight Radar is centered on Nordhorn and uses public ADS-B data.</div>"
            "</div></div></body></html>");
  return page;
}

void configurePortalRoutes() {
  if (portalRoutesConfigured) return;
  portalRoutesConfigured = true;
  portalServer.on("/save", HTTP_POST, []() {
    const String ssid = portalServer.arg("ssid");
    const String password = portalServer.arg("password");
    if (ssid.isEmpty()) {
      portalServer.send(400, "text/plain", "Wi-Fi name is required");
      return;
    }
    preferences.begin("neondeck", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    preferences.end();
    pendingSsid = ssid;
    pendingPassword = password;
    portalServer.send(200, "text/html",
      "<!doctype html><meta name='viewport' content='width=device-width'><body style='background:#292929;color:white;font-family:system-ui;text-align:center;padding:40px'><h1 style='color:#58b8ae'>Saved!</h1><p>Neon Deck is connecting. You may return to the display.</p></body>");
    portalConnectPending = true;
  });
  portalServer.onNotFound([]() {
    portalServer.send(200, "text/html", buildPortalPage());
  });
  portalServer.on("/", HTTP_GET, []() {
    portalServer.send(200, "text/html", buildPortalPage());
  });
}

void drawFlightMessage(const char *title, const char *line1, const char *line2,
                       uint16_t color) {
  tft.fillScreen(DECK_BG);
  drawDirectHeader("LIVE FLIGHT RADAR", TEAL);
  tft.drawCircle(160, 104, 39, color);
  tft.drawCircle(160, 104, 25, GRID);
  tft.drawCircle(160, 104, 11, GRID);
  tft.drawLine(160, 104, 186, 78, color);
  tft.fillTriangle(174, 86, 183, 88, 179, 96, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, DECK_BG);
  tft.drawString(title, 160, 158, 2);
  tft.setTextColor(TEXT_DIM, DECK_BG);
  tft.drawString(line1, 160, 183, 1);
  tft.drawString(line2, 160, 198, 1);
}

void drawFlightPortal() {
  drawFlightMessage("WI-FI SETUP", "Join: NeonDeck-Setup", "then open 192.168.4.1", TEAL);
  tft.fillRoundRect(66, 213, 188, 21, 6, PANEL_BG);
  tft.drawRoundRect(66, 213, 188, 21, 6, SILVER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_BG);
  tft.drawString("use your phone or computer", 160, 223, 1);
}

void drawFlightError() {
  tft.fillScreen(DECK_BG);
  drawDirectHeader("LIVE FLIGHT RADAR", SILVER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(SILVER, DECK_BG);
  tft.drawString("RADAR OFFLINE", 160, 86, 4);
  tft.setTextColor(TFT_WHITE, DECK_BG);
  tft.drawString(flightError, 160, 124, 1);
  tft.fillRoundRect(15, 174, 137, 38, 8, PANEL_BG);
  tft.drawRoundRect(15, 174, 137, 38, 8, TEAL);
  tft.fillRoundRect(168, 174, 137, 38, 8, PANEL_BG);
  tft.drawRoundRect(168, 174, 137, 38, 8, SILVER);
  tft.setTextColor(TFT_WHITE, PANEL_BG);
  tft.drawString("RETRY", 83, 193, 2);
  tft.drawString("WI-FI SETUP", 236, 193, 2);
}

void stopPortal() {
  if (!portalRunning) return;
  dnsServer.stop();
  portalServer.stop();
  WiFi.scanDelete();
  WiFi.softAPdisconnect(true);
  portalRunning = false;
  portalConnectPending = false;
}

void beginWiFiConnection(const String &ssid, const String &password) {
  stopPortal();
  pendingSsid = ssid;
  pendingPassword = password;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  flightState = FLIGHT_CONNECTING;
  flightStateStarted = millis();
  char line[48];
  snprintf(line, sizeof(line), "Network: %.30s", ssid.c_str());
  drawFlightMessage("CONNECTING TO WI-FI", line, "please wait...", TEAL);
  Serial.printf("FLIGHT_WIFI_CONNECT ssid=%s\n", ssid.c_str());
}

void connectSavedWiFi() {
  preferences.begin("neondeck", true);
  // Zuerst isKey(): getString() protokolliert bei einem fehlenden Schlüssel einen NVS-Fehler.
  String ssid = preferences.isKey("ssid") ? preferences.getString("ssid", "") : "";
  String password = preferences.isKey("pass") ? preferences.getString("pass", "") : "";
  preferences.end();
  if (ssid.isEmpty()) {
    // Auf das in secrets.h hinterlegte WLAN zurückfallen.
    ssid = WIFI_SSID;
    password = WIFI_PASSWORD;
  }
  if (ssid.isEmpty()) startFlightPortal();
  else beginWiFiConnection(ssid, password);
}

void startFlightPortal() {
  stopPortal();
  WiFi.disconnect(true);
  delay(80);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("NeonDeck-Setup");
  WiFi.scanNetworks(true);
  dnsServer.start(53, "*", WiFi.softAPIP());
  portalServer.begin();
  portalRunning = true;
  portalConnectPending = false;
  flightState = FLIGHT_PORTAL;
  flightStateStarted = millis();
  drawFlightPortal();
  Serial.printf("FLIGHT_PORTAL_READY ip=%s\n", WiFi.softAPIP().toString().c_str());
}

void stopFlightNetworking() {
  stopPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  pendingSsid = "";
  pendingPassword = "";
}

bool locateScanner() {
  preferences.begin("neondeck", true);
  const bool manual = preferences.getBool("manual", false);
  if (manual) {
    scannerLatitude = preferences.getFloat("lat", 0);
    scannerLongitude = preferences.getFloat("lon", 0);
    const String label = preferences.getString("place", "Saved location");
    label.substring(0, 31).toCharArray(scannerLocation, sizeof(scannerLocation));
    preferences.end();
    return true;
  }
  preferences.end();

  scannerLatitude = SCANNER_LAT;
  scannerLongitude = SCANNER_LON;
  strncpy(scannerLocation, SCANNER_PLACE, sizeof(scannerLocation));
  Serial.printf("FLIGHT_LOCATION place=%s lat=%.3f lon=%.3f\n",
                scannerLocation, scannerLatitude, scannerLongitude);
  return true;
}

float aircraftDistanceNm(float lat, float lon, float &bearing) {
  const float lat1 = scannerLatitude * DEG_TO_RAD;
  const float lat2 = lat * DEG_TO_RAD;
  const float dLat = (lat - scannerLatitude) * DEG_TO_RAD;
  const float dLon = (lon - scannerLongitude) * DEG_TO_RAD;
  const float a = sinf(dLat / 2) * sinf(dLat / 2) +
                  cosf(lat1) * cosf(lat2) * sinf(dLon / 2) * sinf(dLon / 2);
  const float c = 2 * atan2f(sqrtf(a), sqrtf(max(0.0f, 1.0f - a)));
  const float y = sinf(dLon) * cosf(lat2);
  const float x = cosf(lat1) * sinf(lat2) - sinf(lat1) * cosf(lat2) * cosf(dLon);
  bearing = fmodf(atan2f(y, x) / DEG_TO_RAD + 360.0f, 360.0f);
  return 3440.065f * c;
}

void copyAircraftText(char *destination, size_t size, const char *primary,
                      const char *fallback) {
  String text = primary ? primary : "";
  text.trim();
  if (text.isEmpty()) text = fallback ? fallback : "UNKNOWN";
  text.toUpperCase();
  text.substring(0, size - 1).toCharArray(destination, size);
}

bool fetchAircraft() {
  char url[150];
  snprintf(url, sizeof(url),
           "https://api.adsb.lol/v2/lat/%.4f/lon/%.4f/dist/50",
           scannerLatitude, scannerLongitude);
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  secureClient.setTimeout(10000);
  secureClient.setHandshakeTimeout(10);  // Sekunden; bei schwachem RSSI kann TLS sonst hängen bleiben
  HTTPClient http;
  http.setTimeout(12000);
  http.useHTTP10(true);
  if (!http.begin(secureClient, url)) {
    strncpy(flightError, "ADS-B service could not start", sizeof(flightError));
    return false;
  }
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("User-Agent", "NeonDeck-ESP32/1.0");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(flightError, sizeof(flightError), "ADS-B service returned HTTP %d", code);
    http.end();
    return false;
  }

  DynamicJsonDocument filter(1024);
  filter["total"] = true;
  filter["ac"][0]["flight"] = true;
  filter["ac"][0]["hex"] = true;
  filter["ac"][0]["t"] = true;
  filter["ac"][0]["alt_baro"] = true;
  filter["ac"][0]["gs"] = true;
  filter["ac"][0]["track"] = true;
  filter["ac"][0]["dst"] = true;
  filter["ac"][0]["dir"] = true;
  filter["ac"][0]["lat"] = true;
  filter["ac"][0]["lon"] = true;
  DynamicJsonDocument doc(32768);
  const DeserializationError parseError = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (parseError) {
    snprintf(flightError, sizeof(flightError), "ADS-B data error: %.35s", parseError.c_str());
    return false;
  }

  aircraftCount = 0;
  flightReportedTotal = doc["total"] | 0;
  for (JsonObject item : doc["ac"].as<JsonArray>()) {
    if (aircraftCount >= MAX_AIRCRAFT) break;
    Aircraft &plane = aircraft[aircraftCount++];
    copyAircraftText(plane.callsign, sizeof(plane.callsign), item["flight"], item["hex"]);
    copyAircraftText(plane.type, sizeof(plane.type), item["t"], "----");
    JsonVariant altitude = item["alt_baro"];
    plane.altitude = altitude.is<int>() || altitude.is<float>() ? altitude.as<int>() : 0;
    plane.speed = static_cast<int>(item["gs"] | 0.0f);
    plane.track = static_cast<int>(item["track"] | 0.0f);
    plane.distance = item["dst"] | -1.0f;
    plane.bearing = item["dir"] | -1.0f;
    if ((plane.distance < 0 || plane.bearing < 0) &&
        !item["lat"].isNull() && !item["lon"].isNull()) {
      float calculatedBearing = 0;
      plane.distance = aircraftDistanceNm(item["lat"].as<float>(), item["lon"].as<float>(),
                                          calculatedBearing);
      plane.bearing = calculatedBearing;
    }
    if (plane.distance < 0) plane.distance = 50;
    if (plane.bearing < 0) plane.bearing = plane.track;
  }

  for (int i = 0; i < aircraftCount - 1; ++i) {
    for (int j = i + 1; j < aircraftCount; ++j) {
      if (aircraft[j].distance < aircraft[i].distance) {
        Aircraft temp = aircraft[i];
        aircraft[i] = aircraft[j];
        aircraft[j] = temp;
      }
    }
  }
  Serial.printf("FLIGHT_DATA_READY shown=%d total=%d\n", aircraftCount, flightReportedTotal);
  return true;
}

void drawAircraftMarker(const Aircraft &plane, int cx, int cy, int radius) {
  const float distance = min(50.0f, max(0.0f, plane.distance));
  const float r = radius * distance / 50.0f;
  const float angle = plane.bearing * DEG_TO_RAD;
  const int x = cx + static_cast<int>(sinf(angle) * r);
  const int y = cy - static_cast<int>(cosf(angle) * r);
  const uint16_t color = plane.altitude == 0 ? SILVER : TEAL_LIGHT;
  tft.fillCircle(x, y, 2, TFT_WHITE);
  tft.drawCircle(x, y, 4, color);
  const float heading = plane.track * DEG_TO_RAD;
  tft.drawLine(x, y, x + static_cast<int>(sinf(heading) * 7),
               y - static_cast<int>(cosf(heading) * 7), color);
}

void drawFlightRadar() {
  tft.fillScreen(DECK_BG);
  drawDirectHeader("LIVE FLIGHT RADAR", TEAL);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TEAL, PANEL_BG);
  char countText[30];
  snprintf(countText, sizeof(countText), "%d AIRCRAFT", flightReportedTotal);
  tft.drawString(countText, 314, 16, 1);

  constexpr int cx = 103;
  constexpr int cy = 131;
  constexpr int radius = 83;
  tft.fillCircle(cx, cy, radius + 2, TEAL_DARK);
  tft.drawCircle(cx, cy, radius, TEAL);
  tft.drawCircle(cx, cy, 55, GRID);
  tft.drawCircle(cx, cy, 28, GRID);
  tft.drawFastHLine(cx - radius, cy, radius * 2, GRID);
  tft.drawFastVLine(cx, cy - radius, radius * 2, GRID);
  tft.drawLine(cx, cy, cx + 58, cy - 58, TEAL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TEXT_DIM, TEAL_DARK);
  tft.drawString("N", cx, cy - radius + 8, 1);
  tft.drawString("50 nm", cx, cy + radius - 7, 1);
  for (int i = 0; i < aircraftCount; ++i) drawAircraftMarker(aircraft[i], cx, cy, radius - 8);

  tft.fillRoundRect(197, 39, 119, 164, 8, PANEL_BG);
  tft.drawRoundRect(197, 39, 119, 164, 8, GRID);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_BG);
  tft.drawString("NEAREST", 205, 50, 1);
  const int rows = min(5, aircraftCount);
  for (int i = 0; i < rows; ++i) {
    const Aircraft &plane = aircraft[i];
    const int y = 69 + i * 25;
    tft.setTextColor(i == 0 ? TEAL_LIGHT : TFT_WHITE, PANEL_BG);
    tft.drawString(plane.callsign, 205, y, 2);
    char details[28];
    snprintf(details, sizeof(details), "%s %dft %.0fnm", plane.type, plane.altitude,
             plane.distance);
    tft.setTextColor(TEXT_DIM, PANEL_BG);
    tft.drawString(details, 205, y + 12, 1);
  }
  if (aircraftCount == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TEXT_DIM, PANEL_BG);
    tft.drawString("clear sky", 256, 115, 2);
  }

  tft.fillRoundRect(6, 211, 228, 24, 6, PANEL_BG);
  tft.drawRoundRect(6, 211, 228, 24, 6, GRID);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TEAL, PANEL_BG);
  tft.drawString(scannerLocation, 14, 223, 1);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TEXT_DIM, PANEL_BG);
  tft.drawString("50 NM", 226, 223, 1);
  tft.fillRoundRect(242, 211, 74, 24, 6, PANEL_LIGHT);
  tft.drawRoundRect(242, 211, 74, 24, 6, TEAL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, PANEL_LIGHT);
  tft.drawString("REFRESH", 279, 223, 1);
}

void openFlights() {
  currentApp = APP_FLIGHTS;
  aircraftCount = 0;
  flightReportedTotal = 0;
  connectSavedWiFi();
  Serial.println("APP_FLIGHTS_OPEN");
}

void requestFlightRefresh() {
  flightState = FLIGHT_FETCHING;
  flightStateStarted = millis();
  drawFlightMessage("SCANNING AIRSPACE", scannerLocation, "receiving live ADS-B data...", TEAL);
}

void loopFlights() {
  if (homePressedAtTop()) {
    goHome();
    return;
  }
  if (flightState == FLIGHT_PORTAL) {
    dnsServer.processNextRequest();
    portalServer.handleClient();
    if (portalConnectPending) {
      delay(120);
      beginWiFiConnection(pendingSsid, pendingPassword);
      return;
    }
  }

  if (flightState == FLIGHT_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      flightState = FLIGHT_LOCATING;  // wird durch den festen Standort sofort aufgelöst
      return;
    }
    if (millis() - flightStateStarted > 16000) {
      startFlightPortal();
      return;
    }
  } else if (flightState == FLIGHT_LOCATING) {
    if (locateScanner()) requestFlightRefresh();
    else {
      flightState = FLIGHT_ERROR;
      drawFlightError();
    }
    return;
  } else if (flightState == FLIGHT_FETCHING) {
    if (fetchAircraft()) {
      flightState = FLIGHT_READY;
      flightLastFetch = millis();
      drawFlightRadar();
    } else {
      flightState = FLIGHT_ERROR;
      drawFlightError();
    }
    return;
  } else if (flightState == FLIGHT_READY) {
    if (touchState.pressed && touchState.y >= 205 && touchState.x >= 238) {
      requestFlightRefresh();
      return;
    }
    if (millis() - flightLastFetch > 30000) {
      requestFlightRefresh();
      return;
    }
  } else if (flightState == FLIGHT_ERROR && touchState.pressed &&
             touchState.y >= 165 && touchState.y <= 220) {
    if (touchState.x < 160) {
      if (WiFi.status() == WL_CONNECTED) {
        flightState = FLIGHT_LOCATING;
        drawFlightMessage("RETRYING", "checking location and airspace", "please wait...", TEAL);
      } else connectSavedWiFi();
    } else startFlightPortal();
  }
}

// -----------------------------------------------------------------------------
// App-Wechsel und Arduino-Einstiegspunkte
// -----------------------------------------------------------------------------

void goHome() {
  Serial.printf("APP_HOME from=%u\n", currentApp);
  if (currentApp == APP_FLIGHTS) stopFlightNetworking();
  if ((currentApp == APP_PONG || currentApp == APP_DINO) && gameFrameReady) {
    gameFrame.deleteSprite();
    gameFrameReady = false;
  }
  showMenu();
}

void loopMenu() {
  const uint32_t now = millis();
  if (now - menuPulseLast >= 600) {  // „Live“-Anzeige neben den Kopfinformationen
    menuPulseLast = now;
    menuPulseOn = !menuPulseOn;
    tft.fillCircle(312, 19, 3, menuPulseOn ? TEAL_LIGHT : TEAL_DARK);
  }
  if (!touchState.pressed) return;
  if (touchState.y >= 42 && touchState.y <= 136) {
    if (touchState.x < 160) { flashMenuTile(0); openPaint(); }
    else { flashMenuTile(1); openPong(); }
  } else if (touchState.y >= 138 && touchState.y <= 234) {
    if (touchState.x < 160) { flashMenuTile(2); openFlights(); }
    else { flashMenuTile(3); openDino(); }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("NEON_DECK_BOOT");
  randomSeed(esp_random());

  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);

  tft.init();
  tft.invertDisplay(false);  // Für das V3-ST7789-Panel erforderlich.
  tft.setRotation(3);        // Querformat, aufrecht mit USB-Kabel auf der linken Seite.
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  ledcSetup(0, 12000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, 190);

  touchBus.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touchController.begin(touchBus);
  touchController.setRotation(3);

  configurePortalRoutes();
  preferences.begin("neondeck", false);  // Namensraum anlegen, damit Lesezugriffe kein NOT_FOUND protokollieren
  preferences.end();
  WiFi.mode(WIFI_OFF);
  splashScreen();
  showMenu();
  Serial.printf("NEON_DECK_READY display=%dx%d touch=XPT2046 apps=4\n",
                tft.width(), tft.height());
}

void loop() {
  // Serielle Kurzbefehle ermöglichen Kamera- und Prüfstandtests ohne Berührung
  // des Displays: 1=Paint, 2=Pong, 3=Flights, 4=Dino, h=Home.
  if (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if ((command == 'h' || command == 'H') && currentApp != APP_MENU) goHome();
    else if (currentApp == APP_MENU) {
      if (command == '1') openPaint();
      else if (command == '2') openPong();
      else if (command == '3') openFlights();
      else if (command == '4') openDino();
    }
  }
  updateTouch();
  switch (currentApp) {
    case APP_MENU:    loopMenu(); break;
    case APP_PAINT:   loopPaint(); break;
    case APP_PONG:    loopPong(); break;
    case APP_FLIGHTS: loopFlights(); break;
    case APP_DINO:    loopDino(); break;
  }
  delay(3);
}
