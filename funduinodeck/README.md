# Funduino Deck

Touchscreen-App-Launcher für das Funduino ESP32 2,8-Zoll Smart Display V3. Dies sind die Firmware und das Gehäuse aus den Funduino-Videos zur Gaming-Konsole und zum Flugscanner im Juli.

## Hardware

- Funduino ESP32 2,8-Zoll Smart Display V3
- Board-Familie: ESP32-2432S028R V3 („Cheap Yellow Display“)
- ESP32-D0WD-V3 mit 4 MB Flash-Speicher
- Integriertes ST7789-TFT mit 240 × 320 Pixeln
- Integrierter resistiver XPT2046-Touchscreen
- USB-C mit CH340-USB-Seriell-Wandler
- Displayausrichtung in diesem Projekt: 320 × 240 im Querformat

Produktseite: [Funduino ESP32 2,8-Zoll Smart Display V3](https://funduinoshop.com/elektronische-module/wireless-iot/bluetooth/esp32-2-8-zoll-smart-display-v3-wifi-bluetooth-usb-c-st7789-arduino-lvgl?number=F23107806)

## Apps

Dieselbe All-in-One-Firmware steuert beide Videoprojekte:

- **Paint** – sieben Farben, drei Pinselgrößen und eine Löschfunktion
- **Pong** – Touchscreen-Steuerung gegen den ESP32
- **Flights** – Live-ADS-B-Radar für Nordhorn
- **Dino Run** – Endlos-Laufspiel mit Touchscreen-Steuerung

Jede App besitzt eine HOME-Schaltfläche, die zum Startmenü zurückführt.

## Repository-Struktur

```text
funduinodeck/
├── gamingconsole/   # Video 9: Dino Run und Pong
├── flightscanner/   # Video 10: Live-Flugzeugradar
└── 3d-files/        # Aktuelle Gehäuseversion
```

`gamingconsole` und `flightscanner` sind eigenständige PlatformIO-Projekte. Beide enthalten absichtlich dieselbe vollständige Firmware mit vier Apps, da in beiden Videos dieselbe Funduino-Deck-Software auf demselben ESP32-Display verwendet wird.

## Schnellstart

Einen der beiden Firmware-Ordner auswählen und folgende Befehle ausführen:

```sh
cd gamingconsole       # alternativ: cd flightscanner
pio run
pio run -t upload
pio device monitor -b 115200
```

PlatformIO erkennt den angeschlossenen seriellen Port normalerweise automatisch. Falls mehrere Boards verbunden sind, unter macOS/Linux `--upload-port /dev/cu...` oder unter Windows den passenden `COM`-Port ergänzen.

## WLAN

WLAN-Zugangsdaten sind optional. Ohne eine lokale Datei `include/secrets.h` startet die Flights-App das Captive Portal **NeonDeck-Setup**. Mit diesem WLAN verbinden und `http://192.168.4.1` öffnen.

Für eine automatische Verbindung `include/secrets.example.h` nach `include/secrets.h` kopieren und die eigenen Zugangsdaten eintragen. Die echte Zugangsdaten-Datei wird von Git ignoriert.

## Gehäuse

Im Ordner [`3d-files/`](3d-files/) befindet sich die aktuelle, 94 × 61 × 13 mm große Gehäuseversion für das Board mit integriertem Display.
