#!/usr/bin/env python3
"""Локальний сервер сайту AgentLight: налаштування WiFi по Bluetooth і 3D-превью корпусу.

Запуск із будь-якої теки:  python3 web/serve.py            -> http://localhost:8791/web/
                           python3 web/serve.py --lan      -> ще й для інших пристроїв у мережі

Chrome дає сторінкам Bluetooth лише з localhost або через HTTPS. Тому налаштування по Bluetooth працює
на тій машині, де запущено сервер; з інших пристроїв по http://<ip>:8791 відкриється все, крім Bluetooth.
Щоб Bluetooth працював звідусіль, виклади репозиторій на хостинг із HTTPS (див. docs/site.md).
"""
import argparse
import functools
import http.server
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent   # корінь репозиторію: сайт посилається на ../case/

parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
parser.add_argument("--port", type=int, default=8791)
parser.add_argument("--lan", action="store_true", help="слухати на всіх інтерфейсах, а не лише на localhost")
args = parser.parse_args()

handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(ROOT))
host = "0.0.0.0" if args.lan else "127.0.0.1"
with http.server.ThreadingHTTPServer((host, args.port), handler) as server:
    print(f"AgentLight: http://localhost:{args.port}/web/   (Ctrl+C — зупинити)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
