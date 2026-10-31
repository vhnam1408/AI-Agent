"""Offline server only: assertions exercise the real C++ client and parser."""
import base64
import json
from pathlib import Path
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

probe, repo = Path(sys.argv[1]), Path(sys.argv[2])
image = repo / "assets/timetable.png"
expected = json.loads((repo / "assets/timetable_expected.json").read_text(encoding="utf-8"))
encoded = subprocess.check_output([str(probe), "image-check", str(image)], text=True).strip()
assert base64.b64decode(encoded, validate=True) == image.read_bytes()
records = []


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        records.append((self.path, body))
        raw = json.dumps({"message": {"content": json.dumps(expected, ensure_ascii=False)},
                          "model": "offline-vision", "done": True}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        output = subprocess.check_output([str(probe), "image", str(image),
            f"http://127.0.0.1:{server.server_port}", "offline-vision"], timeout=20)
        parsed = json.loads(output)
        assert json.loads(parsed["content"]) == expected
        assert len(records) == 1
        endpoint, request = records[0]
        assert endpoint == "/api/chat" and request["stream"] is False
        assert request["model"] == "offline-vision"
        assert request["options"] == {"temperature": 0, "num_predict": 4096}
        assert base64.b64decode(request["messages"][0]["images"][0]) == image.read_bytes()
        print("PASS real PNG bytes -> existing C++ client -> mock HTTP -> C++ parser")
    finally:
        server.shutdown()
        thread.join()
