# Funduino Deck Flugscanner

Live-Flugzeugradar aus dem Funduino-Juli-Video 10 auf dem ESP32-2432S028R V3 mit integriertem Touchscreen-Display.

Dies ist die vollständige Funduino-Deck-Firmware mit vier Apps. Im Startmenü **Flights** auswählen, um den Scanner zu öffnen.

## Funktionsweise des Flugscanners

- Öffentliche Live-ADS-B-Daten von `adsb.lol`; kein API-Schlüssel erforderlich
- Fester Mittelpunkt: Nordhorn (`52.4310`, `7.0690`)
- Radius von 50 nautischen Meilen
- Bis zu 18 Flugzeuge auf dem Radar
- Liste der fünf nächsten Flugzeuge mit Rufzeichen, Flugzeugtyp, Höhe und Entfernung
- Kursmarkierungen und Entfernungsringe
- Automatische Aktualisierung alle 30 Sekunden
- Schaltfläche für die manuelle Aktualisierung
- WLAN-Einrichtung über ein Captive Portal; Zugangsdaten werden lokal im NVS des ESP32 gespeichert

## Kompilieren und Hochladen

[PlatformIO](https://platformio.org/) installieren, das Display per USB-C verbinden und folgende Befehle ausführen:

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

## WLAN-Einrichtung

Für eine automatische Verbindung:

```sh
cp include/secrets.example.h include/secrets.h
```

In die kopierte Datei die eigenen WLAN-Zugangsdaten eintragen. `include/secrets.h` wird von Git ignoriert.

Ohne diese Datei Flights öffnen und den Anweisungen auf dem Display folgen:

1. Mit einem Smartphone oder Computer dem WLAN **NeonDeck-Setup** beitreten.
2. `http://192.168.4.1` öffnen, falls die Einrichtungsseite nicht automatisch erscheint.
3. Das lokale WLAN auswählen und das Passwort eingeben.

Über das Portal gespeicherte Zugangsdaten haben Vorrang vor den einkompilierten Standardwerten.

## Radarstandort ändern

Diese Konstanten am Anfang von `src/main.cpp` bearbeiten:

```cpp
constexpr float SCANNER_LAT = 52.4310f;
constexpr float SCANNER_LON = 7.0690f;
constexpr char SCANNER_PLACE[] = "Nordhorn DE";
```

Die weiteren Apps Paint, Pong und Dino Run sind ebenfalls enthalten, da das veröffentlichte Video das vollständige Funduino Deck zeigt.
