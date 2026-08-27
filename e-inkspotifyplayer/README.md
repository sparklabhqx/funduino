# E-Ink Spotify Player

Spotify-„Now Playing“-Anzeige aus `video_1_august_e-inkspotify.mp4`. Das Projekt verwendet einen klassischen ESP32 DevKit und das **Seeed Studio Grove Triple Color E-Ink Display 1,54 Zoll**.

## Funktionen

- Aktueller Song über die Spotify Web API
- Songtitel in einer schmalen Leiste am oberen Rand
- Randfüllendes Albumcover
- Umwandlung des JPEG-Covers in Schwarz, Weiß und Rot
- Geordnetes Dithering für eine bessere Darstellung auf dem Dreifarb-Panel
- Spotify-Abfrage alle 15 Sekunden während der Wiedergabe und alle 30 Sekunden im Leerlauf
- Mindestens 180 Sekunden zwischen Display-Aktualisierungen gemäß Seeed-Empfehlung
- Eigene Anzeigen für Leerlauf, fehlende Wiedergabe und Verbindungsfehler

## Hardware

- ESP32 DevKit
- Seeed Studio Grove Triple Color E-Ink Display 1,54 Zoll
- Auflösung: 152 × 152 Pixel
- Farben: Schwarz, Weiß und Rot
- UART-Controller auf dem Displaymodul
- 3D-gedrucktes Tischgehäuse

## Verkabelung

| Grove-Signal | ESP32 |
|---|---|
| UART-Ausgang / gelb | GPIO25 (RX) |
| UART-Eingang / weiß | GPIO26 (TX) |
| VCC / rot | Versorgung des Grove-Displays |
| GND / schwarz | GND |

Das Display kommuniziert über UART2 mit 230400 Baud. Die USB-Diagnose verwendet UART0 mit 115200 Baud. Da die UART-Beschriftung bei manchen Grove-Revisionen aus Host-Sicht angegeben ist, testet die Firmware GPIO25 und GPIO26 automatisch in beiden Richtungen.

## Spotify einrichten

1. Die öffentliche Vorlage kopieren:

   ```sh
   cp src/secrets.example.h src/secrets.h
   ```

2. WLAN-Zugangsdaten in `src/secrets.h` eintragen.
3. Unter [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard) eine Spotify-App erstellen.
4. Als Redirect URI exakt `http://127.0.0.1:8888/callback` eintragen.
5. Client-ID, Client-Secret und Refresh-Token automatisch eintragen lassen:

   ```sh
   python3 auth/get_refresh_token.py \
     --client-id DEINE_CLIENT_ID \
     --client-secret DEIN_CLIENT_SECRET
   ```

`src/secrets.h` wird von Git ignoriert und darf nicht veröffentlicht werden.

## Kompilieren und Hochladen

[PlatformIO](https://platformio.org/) installieren, den ESP32 per USB verbinden und aus diesem Ordner ausführen:

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

Serielle Befehle:

- `u` – Spotify sofort abfragen
- `i` – aktuellen Status ausgeben
- `t` – Spotify-Zugriffstoken erneuern

Der Befehl `u` umgeht nicht das sichere Mindestintervall des E-Ink-Panels.

## Gehäuse

`EInkSpotifyPlayer_Case.stl` ist das im Video gezeigte klappbare Tischgehäuse.

- Außenmaße des STL-Modells: ungefähr 65 × 100 × 79 mm
- Aufnahme für das 1,54-Zoll-Grove-Display
- Innenraum für ESP32 und Verkabelung
- Aufgestellte Front für eine gut lesbare Musikanzeige

Vor dem Drucken Bauteilabstände, Kabeldurchführung und Drucktoleranzen im Slicer prüfen.
