# Funduino Deck

Touchscreen app launcher for the Funduino ESP32 2.8-inch Smart Display V3. This is the firmware and enclosure shown in the July Funduino Gaming Console and Flight Scanner videos.

## Hardware

- Funduino ESP32 2.8-inch Smart Display V3
- Board family: ESP32-2432S028R V3 ("Cheap Yellow Display")
- ESP32-D0WD-V3, 4 MB flash
- Integrated 240 x 320 ST7789 TFT
- Integrated XPT2046 resistive touchscreen
- USB-C with CH340 USB-to-serial bridge
- Display orientation in this project: 320 x 240 landscape

Product page: [Funduino ESP32 2.8-inch Smart Display V3](https://funduinoshop.com/elektronische-module/wireless-iot/bluetooth/esp32-2-8-zoll-smart-display-v3-wifi-bluetooth-usb-c-st7789-arduino-lvgl?number=F23107806)

## Apps

The same all-in-one firmware powers both video projects:

- **Paint** - seven colors, three brush sizes and Clear
- **Pong** - touchscreen paddle against the ESP32
- **Flights** - live ADS-B radar centered on Nordhorn, Germany
- **Dino Run** - touchscreen endless runner

Every app has a HOME button that returns to the launcher.

## Repository layout

```text
funduinodeck/
├── gamingconsole/   # Video 9: Dino Run and Pong
├── flightscanner/   # Video 10: live aircraft radar
└── 3d-files/        # Shallow and deep enclosure variants
```

`gamingconsole` and `flightscanner` are self-contained PlatformIO downloads. They intentionally contain the same complete four-app firmware because both videos use the same Funduino Deck software and ESP32 display.

## Quick start

Choose either firmware folder and run:

```sh
cd gamingconsole       # or: cd flightscanner
pio run
pio run -t upload
pio device monitor -b 115200
```

PlatformIO normally detects the connected serial port automatically. If more than one board is connected, append `--upload-port /dev/cu...` on macOS/Linux or the appropriate `COM` port on Windows.

## Wi-Fi

Wi-Fi credentials are optional. Without a local `include/secrets.h`, opening Flights starts the **NeonDeck-Setup** captive portal. Join that network and open `http://192.168.4.1`.

For automatic connection, copy `include/secrets.example.h` to `include/secrets.h` and enter local credentials. The real secrets file is ignored by Git.

## Enclosure

See [`3d-files/`](3d-files/) for the 94 x 61 mm case exports used with the integrated display board.
