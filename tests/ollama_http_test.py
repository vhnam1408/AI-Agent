"""Kiểm tra HTTP và CLI bằng server cục bộ, không cần model/Ollama thật."""
import base64
import json
import socket
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

records = []
result = "15 + 17 = 32."

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        records.append((self.path, request))
        model = request["model"]
        status = 200
        body = {"model": model, "message": {"content": result},
                "prompt_eval_count": 2, "eval_count": 3}
        if model == "bad-schema":
            body["message"] = {"content": 123}
        elif model == "bad-metadata":
            body["eval_count"] = "wrong type"
        elif model == "http-error":
            status, body = 500, {"error": "mock server error"}
        elif model == "not-found":
            status, body = 404, {"error": "model not found"}
        elif model == "negative-tokens":
            body["eval_count"] = -1
        elif model == "large-tokens":
            body["eval_count"] = 2**64 - 1
        elif model == "float-tokens":
            body["eval_count"] = 1.5
        elif model == "overflow-tokens":
            body["eval_count"] = 2**31 - 1
        elif model == "bad-duration":
            body["total_duration"] = -1
        elif model == "array-json":
            body = []
        elif model == "null-json":
            body = None
        elif model == "disconnect":
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
            return
        elif model == "missing-field":
            del body["message"]
        elif model == "slow":
            time.sleep(0.2)
        raw = b"not JSON" if model == "bad-json" else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        try:
            self.wfile.write(raw)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

app, smoke = map(str, map(Path, sys.argv[1:3]))
server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
url = f"http://127.0.0.1:{server.server_port}"
try:
    # Lấy một cổng trống rồi đóng socket để thử kết nối bị từ chối.
    with socket.socket() as closed_port:
        closed_port.bind(("127.0.0.1", 0))
        refused_url = f"http://127.0.0.1:{closed_port.getsockname()[1]}"
    subprocess.run([smoke, url, refused_url], check=True, timeout=20)
    path, payload = records[-1]
    assert path == "/api/chat", "Endpoint was duplicated"
    assert payload["model"] == "reconfigured"
    assert payload["options"] == {"temperature": 0.5, "num_predict": 64}
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        text = root / "input.txt"
        text.write_text("Study OOP", encoding="utf-8")
        image = root / "image.png"
        image.write_bytes(b"image bytes for transport check")
        output = root / "result.json"

        def run(model, mode, source, success=True, extra=()):
            command = [app, "--url", url, "--model", model, mode, str(source),
                       "--output", str(output), *extra]
            completed = subprocess.run(command, capture_output=True, timeout=10)
            assert (completed.returncode == 0) == success, completed.stderr
            return completed

        run("valid", "--text", text, extra=("--temperature", "0.25", "--max-tokens", "128"))
        saved = json.loads(output.read_text())
        assert saved["content"] == result and saved["model_used"] == "valid"
        assert saved["input_type"] == "text" and saved["success"]
        path, payload = records[-1]
        assert path == "/api/chat" and payload["stream"] is False
        assert "Study OOP" in payload["messages"][0]["content"]
        assert "images" not in payload["messages"][0]
        assert payload["options"] == {"temperature": 0.25, "num_predict": 128}
        run("valid", "--image", image)
        assert base64.b64decode(records[-1][1]["messages"][0]["images"][0]) == image.read_bytes()
        original = output.read_bytes()
        for model in ["bad-json", "bad-schema", "http-error", "missing-field"]:
            run(model, "--text", text, success=False)
            assert output.read_bytes() == original, "Invalid response overwrote output"
        run("valid", "--text", root / "missing.txt", success=False)
        # Hai chế độ đầu vào cùng lúc phải bị từ chối.
        bad_args = subprocess.run([app, "--model", "valid", "--text", str(text),
                                   "--image", str(image)], capture_output=True, timeout=10)
        assert bad_args.returncode != 0
        for option, value in [("--temperature", "nan"), ("--temperature", "-1"),
                              ("--max-tokens", "1.5"), ("--timeout-ms", "100abc"),
                              ("--connect-timeout-ms", "0")]:
            run("valid", "--text", text, success=False, extra=(option, value))
        # Duong chay exe phai tra dung ba nhom loi cua TASK-09.
        for model, error, extra in [
            ("slow", "Timeout", ("--timeout-ms", "30")),
            ("disconnect", "NetworkError", ()),
            ("bad-json", "MalformedJson", ())
        ]:
            completed = run(model, "--text", text, success=False, extra=extra)
            assert error.encode() in completed.stderr, completed.stderr

        # Mo exe khong truyen tham so: nhap URL, model, temp, token va cau hoi.
        for model, success in [("valid", True), ("bad-json", False)]:
            lines = [url, model, "0.25", "128", "300000", "Study OOP", str(output), ""]
            completed = subprocess.run([app], input=("\n".join(lines) + "\n").encode(),
                                       cwd=root, capture_output=True, timeout=10)
            assert (completed.returncode == 0) == success, completed.stderr
            if success:
                saved = json.loads(output.read_text())
                assert saved["input_type"] == "text" and saved["content"] == result
                assert records[-1][1]["options"] == {"temperature": 0.25, "num_predict": 128}
            else:
                assert b"MalformedJson" in completed.stderr
    print("HTTP payload, errors, timeout, text/image CLI and JSON output checks passed")
finally:
    server.shutdown()
    server.server_close()
    thread.join()
