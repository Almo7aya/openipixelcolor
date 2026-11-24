"""Serve the panel control page on http://localhost:8321 and open it.
Web Bluetooth needs a secure context; http://localhost counts as one. Use Chrome or Edge.

usage: python serve.py [--port 8321] [--no-browser]
"""
import argparse
import functools
import http.server
import os
import threading
import webbrowser

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=8321)
ap.add_argument("--no-browser", action="store_true", help="don't open a browser tab")
a = ap.parse_args()

here = os.path.dirname(os.path.abspath(__file__))
handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=here)
server = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), handler)
url = f"http://localhost:{a.port}/"
print(f"Panel control: {url}  (Ctrl+C to stop)", flush=True)
if not a.no_browser:
    threading.Timer(0.5, lambda: webbrowser.open(url)).start()
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
