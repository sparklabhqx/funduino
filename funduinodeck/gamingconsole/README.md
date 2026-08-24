# Funduino Deck Gaming-Konsole

Firmware für die Touchscreen-Gaming-Konsole aus dem Funduino-Juli-Video 9. Sie ist für den ESP32-2432S028R V3 mit integriertem ST7789-Display und XPT2046-Touchscreen ausgelegt.

Dies ist die vollständige Funduino-Deck-Firmware. Im Startmenü stehen unter anderem diese Spiele zur Auswahl:

- **Dino Run** – zum Starten und Springen tippen, Hindernissen ausweichen und nach „Game Over“ zum Neustart erneut tippen.
- **Pong** – das linke Paddle vertikal ziehen, zum Aufschlag tippen und gegen den ESP32 spielen. Wer zuerst sieben Punkte erreicht, gewinnt.

Die Firmware enthält außerdem Paint und den Live-Flugscanner. Damit entspricht sie dem Gerät, das am Ende des Videos gezeigt wird.

## Kompilieren und Hochladen

[PlatformIO](https://platformio.org/) installieren, das Display per USB-C verbinden und folgende Befehle ausführen:

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

Serielle Prüfstand-Kurzbefehle:

- `1` – Paint
- `2` – Pong
- `3` – Flights
- `4` – Dino Run
- `h` – Home

## Optionales WLAN

Die Spiele funktionieren ohne WLAN. Damit sich die enthaltene Flights-App automatisch verbindet:

```sh
cp include/secrets.example.h include/secrets.h
```

Anschließend `include/secrets.h` bearbeiten. Fehlt die Datei, bietet Flights stattdessen die Einrichtung über ein Captive Portal an. Die ausgefüllte Zugangsdaten-Datei niemals committen.

## Hardwaredetails

Display und Touch-Controller verwenden getrennte SPI-Busse. Die vollständige Pin-Konfiguration steht in `platformio.ini` und am Anfang von `src/main.cpp`. Für dieses V3-ST7789-Panel benötigt die Firmware die Querformat-Rotation 3 und `tft.invertDisplay(false)`.
