# Funduino Deck Gaming Console

Firmware for the touchscreen gaming console shown in Funduino July video 9. It targets the ESP32-2432S028R V3 with its integrated ST7789 display and XPT2046 touchscreen.

This is the complete Funduino Deck image. From the launcher, select:

- **Dino Run** - tap to start, tap to jump, avoid the obstacles and tap after game over to restart.
- **Pong** - drag the left paddle vertically, tap to serve and play against the ESP32. First to seven wins.

The same image also includes Paint and the live Flight Scanner, matching the device shown at the end of the video.

## Build and upload

Install [PlatformIO](https://platformio.org/), connect the display by USB-C and run:

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

Serial bench shortcuts:

- `1` - Paint
- `2` - Pong
- `3` - Flights
- `4` - Dino Run
- `h` - Home

## Optional Wi-Fi

Games work without Wi-Fi. To let the included Flights app connect automatically:

```sh
cp include/secrets.example.h include/secrets.h
```

Then edit `include/secrets.h`. If it is omitted, Flights offers captive-portal setup instead. Never commit the completed secrets file.

## Hardware details

The display and touch controllers use separate SPI buses. Their complete pin configuration is in `platformio.ini` and at the top of `src/main.cpp`. The firmware requires landscape rotation 3 and `tft.invertDisplay(false)` for this V3 ST7789 panel.
