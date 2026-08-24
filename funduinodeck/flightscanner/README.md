# Funduino Deck Flight Scanner

Live aircraft radar shown in Funduino July video 10, running on the ESP32-2432S028R V3 integrated touchscreen display.

This is the complete four-app Funduino Deck firmware. Select **Flights** from the launcher to open the scanner.

## Flight Scanner behavior

- Public live ADS-B data from `adsb.lol`; no API key required
- Fixed center: Nordhorn, Germany (`52.4310`, `7.0690`)
- 50 nautical-mile radius
- Up to 18 aircraft plotted on the radar
- Nearest five listed with callsign, aircraft type, altitude and distance
- Heading markers and distance rings
- Automatic refresh every 30 seconds
- Manual Refresh button
- Captive-portal Wi-Fi setup and credentials stored locally in ESP32 NVS

## Build and upload

Install [PlatformIO](https://platformio.org/), connect the display by USB-C and run:

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

## Wi-Fi setup

For automatic connection:

```sh
cp include/secrets.example.h include/secrets.h
```

Edit the copied file with local Wi-Fi credentials. `include/secrets.h` is ignored by Git.

Without that file, open Flights and use the display instructions:

1. Join **NeonDeck-Setup** from a phone or computer.
2. Open `http://192.168.4.1` if the setup page does not appear automatically.
3. Select the local network and enter its password.

Credentials saved through the portal override compiled-in defaults.

## Change the radar location

Edit these constants near the top of `src/main.cpp`:

```cpp
constexpr float SCANNER_LAT = 52.4310f;
constexpr float SCANNER_LON = 7.0690f;
constexpr char SCANNER_PLACE[] = "Nordhorn DE";
```

The other launcher apps—Paint, Pong and Dino Run—are included because the published video demonstrates the complete Funduino Deck.
