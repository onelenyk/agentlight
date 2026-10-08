"""Імітація API моделі Anthropic для тестів: Claude Code проходить справжній хід без акаунта й без витрат.

Що відповісти, вирішує текст останнього повідомлення користувача:
  містить USE_TOOL — спершу виклик інструмента Bash, після його результату — текст
  містить FAIL     — помилка 400 «невірний запит»: її Claude Code не повторює, а одразу завершує хід помилкою
                     (401, 429 і 5xx він повторює хвилинами)
  інакше           — текст "ok"
"""
import http.server
import json
import threading
import time

TOOL_NAME = "Bash"         # який інструмент «модель» викликає на USE_TOOL; тест може підмінити на інший
TOOL_INPUT = {"command": "echo canary", "description": "Print canary"}


def last_user_text(request):
    """Текст останнього повідомлення користувача і чи є серед повідомлень результат інструмента."""
    text, has_result = "", False
    for message in request.get("messages") or []:
        if message.get("role") != "user":
            continue
        content = message.get("content")
        blocks = content if isinstance(content, list) else [{"type": "text", "text": content or ""}]
        for block in blocks:
            if block.get("type") == "tool_result":
                has_result = True
            elif block.get("type") == "text":
                text = block.get("text", "")
    return text, has_result


class MockModel:
    def __init__(self, port=0):
        self.requests = []
        model = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def send_json(self, code, data):
                body = json.dumps(data).encode()
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                self.send_json(404, {"type": "error", "error": {"type": "not_found_error", "message": "not found"}})

            def do_POST(self):
                request = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
                model.requests.append(self.path)
                if not self.path.startswith("/v1/messages"):
                    return self.do_GET()
                # Справжня модель думає щонайменше секунду. Без паузи хуки «запит надіслано» і «хід завершено»
                # йдуть майже одночасно і, бувши фоновими, можуть дійти до лампи в зворотному порядку.
                time.sleep(0.5)
                text, has_result = last_user_text(request)
                all_text = json.dumps(request.get("messages") or [])
                if "FAIL" in all_text:
                    return self.send_json(400, {"type": "error", "error": {"type": "invalid_request_error", "message": "canary failure"}})
                tool = "USE_TOOL" in all_text and not has_result and request.get("tools")
                message = {"id": "msg_canary", "type": "message", "role": "assistant", "model": request.get("model", "mock"),
                           "content": [], "stop_reason": None, "stop_sequence": None, "usage": {"input_tokens": 1, "output_tokens": 1}}
                block = ({"type": "tool_use", "id": "toolu_canary", "name": TOOL_NAME, "input": TOOL_INPUT} if tool
                         else {"type": "text", "text": "ok"})
                stop = "tool_use" if tool else "end_turn"
                if not request.get("stream"):
                    return self.send_json(200, dict(message, content=[block], stop_reason=stop))
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.end_headers()

                def event(name, data):
                    self.wfile.write(f"event: {name}\ndata: {json.dumps(dict(data, type=name))}\n\n".encode())
                    self.wfile.flush()

                event("message_start", {"message": message})
                if tool:
                    event("content_block_start", {"index": 0, "content_block": dict(block, input={})})
                    event("content_block_delta", {"index": 0, "delta": {"type": "input_json_delta", "partial_json": json.dumps(TOOL_INPUT)}})
                else:
                    event("content_block_start", {"index": 0, "content_block": {"type": "text", "text": ""}})
                    event("content_block_delta", {"index": 0, "delta": {"type": "text_delta", "text": "ok"}})
                event("content_block_stop", {"index": 0})
                event("message_delta", {"delta": {"stop_reason": stop, "stop_sequence": None}, "usage": {"output_tokens": 1}})
                event("message_stop", {})

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"

    def __enter__(self):
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        return self

    def __exit__(self, *exc):
        self.server.shutdown()
        self.server.server_close()
