#!/usr/bin/env python3
"""Einmaliger Spotify-OAuth-Helfer für den E-Ink Spotify Player.

1. Unter https://developer.spotify.com/dashboard eine App erstellen.
     - Redirect URI (muss exakt übereinstimmen): http://127.0.0.1:8888/callback
     - API/SDK: Web API
2. Ausführen:
     python3 auth/get_refresh_token.py --client-id XXX --client-secret YYY
3. Der Browser öffnet die Freigabe. Anschließend gibt das Skript das
   Refresh-Token aus und schreibt alle drei Werte nach src/secrets.h.

Verwendet nur die Python-Standardbibliothek und läuft mit python3.
"""
import argparse
import base64
import http.server
import json
import re
import secrets
import sys
import threading
import urllib.parse
import urllib.request
import webbrowser
from pathlib import Path

PORT = 8888
REDIRECT = f"http://127.0.0.1:{PORT}/callback"
SCOPE = "user-read-playback-state user-read-currently-playing"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--client-id", required=True)
    ap.add_argument("--client-secret", required=True)
    ap.add_argument("--no-write", action="store_true",
                    help="nur ausgeben, src/secrets.h nicht verändern")
    args = ap.parse_args()

    state = secrets.token_urlsafe(16)
    auth_url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
        "client_id": args.client_id,
        "response_type": "code",
        "redirect_uri": REDIRECT,
        "scope": SCOPE,
        "state": state,
    })

    result = {}
    done = threading.Event()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path != "/callback":
                self.send_response(404)
                self.end_headers()
                return
            q = urllib.parse.parse_qs(parsed.query)
            ok = q.get("state", [""])[0] == state and "code" in q
            result["code"] = q.get("code", [""])[0] if ok else None
            result["error"] = q.get("error", [""])[0] if "error" in q else ""
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            msg = ("E-Ink Spotify Player: Autorisierung erfolgreich. Dieses Fenster kann geschlossen werden."
                   if ok else
                   f"E-Ink Spotify Player: FEHLER ({result['error'] or 'Status stimmt nicht überein'})")
            self.wfile.write(f"<h2 style='font-family:sans-serif'>{msg}</h2>".encode())
            done.set()

        def log_message(self, *a):
            pass

    srv = http.server.HTTPServer(("127.0.0.1", PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print(f"Browser für die Spotify-Freigabe öffnen (Weiterleitung: {REDIRECT}) ...")
    webbrowser.open(auth_url)
    if not done.wait(timeout=300):
        sys.exit("Zeitüberschreitung: Innerhalb von 5 Minuten kam kein OAuth-Rückruf an.")
    srv.shutdown()
    if not result.get("code"):
        sys.exit(f"Autorisierung fehlgeschlagen: {result.get('error') or 'Status stimmt nicht überein'}")

    basic = base64.b64encode(
        f"{args.client_id}:{args.client_secret}".encode()).decode()
    req = urllib.request.Request(
        "https://accounts.spotify.com/api/token",
        data=urllib.parse.urlencode({
            "grant_type": "authorization_code",
            "code": result["code"],
            "redirect_uri": REDIRECT,
        }).encode(),
        headers={"Authorization": f"Basic {basic}",
                 "Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req) as r:
            tok = json.load(r)
    except urllib.error.HTTPError as e:
        sys.exit(f"Token-Austausch fehlgeschlagen: HTTP {e.code}: {e.read().decode()}")

    refresh = tok.get("refresh_token")
    if not refresh:
        sys.exit(f"Kein refresh_token in der Antwort: {tok}")
    print("\nrefresh_token:\n" + refresh + "\n")

    if args.no_write:
        return
    sh = Path(__file__).resolve().parent.parent / "src" / "secrets.h"
    if not sh.exists():
        print("src/secrets.h wurde nicht gefunden – Werte bitte manuell eintragen.")
        return
    text = sh.read_text()
    for key, val in [("SPOTIFY_CLIENT_ID", args.client_id),
                     ("SPOTIFY_CLIENT_SECRET", args.client_secret),
                     ("SPOTIFY_REFRESH_TOKEN", refresh)]:
        text, n = re.subn(rf'(#define {key} ")[^"]*(")',
                          lambda m: m.group(1) + val + m.group(2), text)
        if not n:
            print(f"Warnung: {key} wurde in secrets.h nicht gefunden")
    sh.write_text(text)
    print(f"Alle drei Werte wurden nach {sh} geschrieben")
    print("Nächster Schritt: pio run -t upload")


if __name__ == "__main__":
    main()
