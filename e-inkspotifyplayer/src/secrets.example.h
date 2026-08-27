#pragma once

// Nach secrets.h kopieren und mit den eigenen Zugangsdaten ausfüllen.
// secrets.h wird von Git ignoriert.
#define WIFI_SSID "dein-wlan"
#define WIFI_PASSWORD "dein-passwort"

// Einmalige Einrichtung der Spotify Web API:
//   1. Unter https://developer.spotify.com/dashboard eine App erstellen.
//      Redirect URI (muss exakt übereinstimmen): http://127.0.0.1:8888/callback
//      API: Web API
//   2. Folgenden Befehl ausführen:
//      python3 auth/get_refresh_token.py --client-id XXX --client-secret YYY
//      Der Browser öffnet die Freigabe. Danach trägt das Skript alle drei Werte ein.
#define SPOTIFY_CLIENT_ID "PASTE_CLIENT_ID"
#define SPOTIFY_CLIENT_SECRET "PASTE_CLIENT_SECRET"
#define SPOTIFY_REFRESH_TOKEN "PASTE_REFRESH_TOKEN"
