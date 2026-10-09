"""Імітація лампи для тестів: ті самі адреси, що в прошивці, а події розбирає той самий код (tests/hookparse).

Як модуль:   with MockLamp() as lamp: ...  — lamp.url, lamp.events, lamp.agents
Окремо:      python3 tests/mock_lamp.py --port 8099
"""
import argparse
import gzip
import hashlib
import hmac
import http.server
import json
import pathlib
import subprocess
import tempfile
import threading

ROOT = pathlib.Path(__file__).resolve().parent.parent
HOOKS = ROOT / "hooks"
_binary = None


def hookparse_binary():
    """Збирає tests/hookparse.cpp один раз на запуск."""
    global _binary
    if _binary is None:
        out = pathlib.Path(tempfile.mkdtemp(prefix="agentlight-")) / "hookparse"
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", "-o", str(out),
                        str(ROOT / "tests" / "hookparse.cpp")], check=True)
        _binary = str(out)
    return _binary


def parse_hook(tool, state, body: bytes):
    out = subprocess.run([hookparse_binary(), tool, state], input=body, capture_output=True, check=True).stdout
    return json.loads(out)


class MockLamp:
    def __init__(self, port=0):
        self.events = []        # усе, що лампа прийняла б як зміну стану, у порядку надходження
        self.agents = {}        # agent_id -> остання подія; idle прибирає запис, як у прошивці
        # «Дозволити»: тест сам задає, що «власник» зробить із лампою
        self.pair_answer = "accepted"   # accepted | rejected | expired
        self.allow_answer = "allow"     # allow | deny | expired | pending
        self.refuse = None              # причина відмови, щоб лампа не пропонувала запит зовсім
        self.forge_signature = False    # відповідати з чужим підписом, ніби це не лампа
        self.keys = {}                  # спаровані комп'ютери: key -> secret
        self.asks = []                  # запити дозволу, що дійшли до лампи
        self.cancelled = []
        lamp = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def reply(self, code, body: bytes, ctype="application/json", gz=False):
                self.send_response(code)
                self.send_header("Content-Type", ctype)
                if gz:                      # лампа віддає файли /setup/ стиснутими завжди, не питаючи клієнта
                    body = gzip.compress(body)
                    self.send_header("Content-Encoding", "gzip")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                if self.path == "/install.sh":
                    script = f"LAMP='{lamp.url}'\n".encode() + (HOOKS / "install.sh").read_bytes()
                    return self.reply(200, script, "text/plain; charset=utf-8")
                if self.path.startswith("/setup/"):
                    file = HOOKS / pathlib.PurePosixPath(self.path).name
                    if file.suffix in (".json", ".js", ".sh") and file.name != "install.sh" and file.is_file():
                        return self.reply(200, file.read_bytes(), gz=True)
                    return self.reply(404, b"Not found", "text/plain")
                if self.path == "/api/status":
                    return self.reply(200, json.dumps({"agents": list(lamp.agents.values())}).encode())
                if self.path.startswith("/api/allow/pair"):
                    return self.reply(200, json.dumps({"state": lamp.pair_answer}).encode())
                if self.path.startswith("/api/allow/ask"):
                    ask = lamp.asks[int(self.path.split("id=")[1]) - 1]
                    answer = {"state": lamp.allow_answer}
                    if lamp.allow_answer in ("allow", "deny"):
                        secret = "not-the-real-secret" if lamp.forge_signature else ask["secret"]
                        answer["sig"] = hmac.new(secret.encode(), f"{ask['nonce']}:{lamp.allow_answer}".encode(), hashlib.sha256).hexdigest()
                    return self.reply(200, json.dumps(answer).encode())
                if self.path == "/events":      # лише в імітації: усе прийняте по порядку, для перевірок ззовні
                    return self.reply(200, json.dumps(lamp.events, ensure_ascii=False).encode())
                self.reply(404, b"Not found", "text/plain")

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
                parts = self.path.strip("/").split("/")
                if self.path == "/api/allow/pair":
                    data = json.loads(body)
                    if lamp.pair_answer == "accepted":
                        lamp.keys[data["key"]] = data["secret"]
                    return self.reply(200, b'{"pairing":true}')
                if self.path.startswith("/api/allow/ask"):
                    query = dict(pair.split("=", 1) for pair in self.path.partition("?")[2].split("&") if "=" in pair)
                    if "agent" in query:                    # подія хука як є
                        event = parse_hook(query["agent"], "waiting", body)
                        ask = {"key": query.get("key"), "nonce": query.get("nonce"), "agent_id": event["agent_id"], "raw": body.decode()}
                    else:
                        ask = json.loads(body)
                    if lamp.refuse or ask.get("key") not in lamp.keys:
                        return self.reply(200, json.dumps({"refused": lamp.refuse or "not paired"}).encode())
                    ask["secret"] = lamp.keys[ask["key"]]
                    lamp.asks.append(ask)
                    return self.reply(200, json.dumps({"id": len(lamp.asks)}).encode())
                if self.path == "/api/allow/cancel":
                    lamp.cancelled.append(json.loads(body)["id"])
                    return self.reply(200, b'{"ok":true}')
                if len(parts) == 3 and parts[0] == "hook":
                    event = parse_hook(parts[1], parts[2], body)
                elif self.path == "/api/status":
                    data = json.loads(body or b"{}")
                    event = {"state": data.get("state", ""), "agent_id": data.get("agent_id", "default"),
                             "name": data.get("name", ""), "message": data.get("message", ""), "task": data.get("task", "")}
                else:
                    return self.reply(404, b"Not found", "text/plain")
                if not event.get("skip"):
                    lamp.events.append(event)
                    if event["state"] == "idle":
                        lamp.agents.pop(event["agent_id"], None)
                    else:
                        lamp.agents[event["agent_id"]] = event
                self.reply(200, b"{}")

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
        self.host = f"127.0.0.1:{self.server.server_address[1]}"
        self.url = f"http://{self.host}"

    def __enter__(self):
        hookparse_binary()
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        return self

    def __exit__(self, *exc):
        self.server.shutdown()
        self.server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--port", type=int, default=8099)
    with MockLamp(parser.parse_args().port) as lamp:
        print(f"Імітація лампи: {lamp.url}  (Ctrl+C — зупинити)")
        try:
            threading.Event().wait()
        except KeyboardInterrupt:
            pass
