# ESP32 WiFi Mood Lamp

Mobile-friendly, self-hosted mood-lamp controller for an ESP32 DevKit/WROOM-32 and a WS2812B strip.

## Hardware

- ESP32 DevKit (WROOM-32)
- WS2812B data input: GPIO 16
- Common ground between ESP32 and strip
- Prefer a separate 5 V supply for larger/brighter strips; do not feed strip current through the ESP32 board
- A 330–470 ohm series resistor on data and a large capacitor across strip power are recommended

## Build

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

Copy `include/secrets.example.h` to `include/secrets.h` and fill in WiFi credentials before building. The real secrets file is gitignored.

The LED count and brightness/power limits are configuration constants at the top of `src/main.cpp`.

## Portal/API

Open `http://moodlamp.local/` from a device on the same network.

- `GET /api/state`
- `GET|POST /api/set?color=%23ff0000&brightness=100&mode=solid&speed=50`

Modes: `solid`, `breathe`, `rainbow`, `wipe`, `fire`.
