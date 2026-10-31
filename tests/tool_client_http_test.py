"""Kaggle-only test: real C++ tools request -> mock Ollama -> C++ parser.

No ToolRegistry/AgentLoop code is linked, modified or executed.
"""
import json
from pathlib import Path
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

probe, fixtures, output = map(Path, sys.argv[1:4])
raw_fixture = (fixtures / 'native_raw.json').read_bytes()
records = []
results = []


def require(condition, message):
    # Ném AssertionError với message khi điều kiện kiểm tra không đạt.
    if not condition:
        raise AssertionError(message)


class Handler(BaseHTTPRequestHandler):
    # Mô phỏng endpoint Ollama để kiểm tra request/response thật của Client C++.
    def log_message(self, *_):
        # Tắt access log mặc định để stdout chỉ chứa kết quả kiểm thử.
        pass

    def do_POST(self):
        # Lưu payload và tạo response theo model giả dùng làm tên kịch bản.
        # Trả calls hợp lệ, JSON lỗi hoặc HTTP 503 để thử các nhánh xử lý của Client.
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        records.append((self.path, payload))
        mode = payload['model']
        data = json.loads(raw_fixture)
        status = 200
        if mode == 'no-content':
            del data['message']['content']
        elif mode == 'multi':
            second = json.loads(json.dumps(data['message']['tool_calls'][0]))
            second['id'] = 'call_second'
            second['function']['index'] = 1
            second['function']['arguments'] = {'task_id': 'T2'}
            data['message']['tool_calls'].append(second)
        elif mode == 'content-call':
            data['message'] = {'content': json.dumps({'tool_name': 'task_lookup', 'arguments': {'task_id': 'T1'}})}
        elif mode == 'unknown':
            data['message']['tool_calls'][0]['function']['name'] = 'unknown'
        elif mode == 'bad-arguments':
            data['message']['tool_calls'][0]['function']['arguments'] = []
        elif mode == 'bad-metadata':
            data['eval_count'] = 'not a number'
        elif mode == 'incomplete':
            data['done'] = False
        elif mode == 'truncated':
            data['done_reason'] = 'length'
        elif mode == 'missing-message':
            del data['message']
        elif mode == 'http-error':
            status, data = 503, {'error': 'mock unavailable'}
        raw = b'{bad' if mode == 'malformed' else json.dumps(data).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def check_case(name, body):
    # Chạy một ca, lưu PASS/FAIL cùng lý do và tiếp tục các ca còn lại.
    try:
        body()
        results.append({'name': name, 'status': 'PASS'})
    except Exception as error:
        results.append({'name': name, 'status': 'FAIL', 'reason': str(error)})


with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f'http://127.0.0.1:{server.server_port}/api/chat/'
    try:
        def run(mode, scenario='valid'):
            # Gọi executable C++ tới mock server; thu output và giới hạn 30 giây.
            return subprocess.run([str(probe), 'tools', url, mode, scenario], capture_output=True, timeout=30)

        for mode in ('valid', 'no-content', 'multi', 'content-call'):
            def valid_case(mode=mode):
                # Kiểm tra schema request, metadata, arguments và thứ tự/id của calls.
                records.clear()
                completed = run(mode)
                require(completed.returncode == 0, completed.stderr.decode(errors='replace') + completed.stdout.decode(errors='replace'))
                data = json.loads(completed.stdout)
                require(len(records) == 1 and records[0][0] == '/api/chat', 'Endpoint duplicated or extra inference')
                payload = records[0][1]
                require(payload['stream'] is False and payload['think'] is False, 'Request mode not preserved')
                require(payload['tools'][0]['function']['parameters']['required'] == ['task_id'], 'Schema missing from C++ request')
                require(data['http_status'] == 200 and data['calls'][0]['arguments'] == {'task_id': 'T1'}, 'Call data/metadata incorrect')
                require(data['raw_response']['message'] == json.loads(raw_fixture)['message'] if mode == 'valid' else data['ok'], 'Raw evidence changed')
                if mode == 'multi':
                    require(len(data['calls']) == 2 and data['calls'][1]['arguments'] == {'task_id': 'T2'}, 'Batch lost/reordered')
                elif mode != 'content-call':
                    require(data['calls'][0]['id'] == 'call_d38iu8jd' and data['calls'][0]['index'] == 0, 'id/index lost')
            check_case(mode, valid_case)

        for mode in ('unknown', 'bad-arguments', 'bad-metadata', 'incomplete', 'truncated', 'missing-message', 'malformed', 'http-error'):
            def invalid_case(mode=mode):
                # Kiểm tra loại lỗi và HTTP status khi response không hợp lệ.
                completed = run(mode)
                data = json.loads(completed.stdout)
                expected = 'HttpError' if mode == 'http-error' else 'MalformedJson'
                require(completed.returncode != 0 and data['error_type'] == expected, f'Wrong error for {mode}: {data}')
                require(data['http_status'] == (503 if mode == 'http-error' else 200), 'HTTP status lost')
            check_case(mode, invalid_case)

        for scenario in ('no-tools', 'bad-tool-name', 'duplicate-tools', 'bad-schema', 'bad-url', 'bad-temperature'):
            def configuration_case(scenario=scenario):
                # Xác nhận input sai bị từ chối trước khi đến mock server.
                records.clear()
                completed = run('valid', scenario)
                data = json.loads(completed.stdout)
                require(completed.returncode != 0 and data['error_type'] == 'InvalidRequest' and not records, 'Invalid configuration reached server')
            check_case(scenario, configuration_case)
    finally:
        server.shutdown()
        thread.join()

summary = {'passed': sum(r['status'] == 'PASS' for r in results),
           'failed': sum(r['status'] == 'FAIL' for r in results), 'cases': results}
output.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(summary, ensure_ascii=False, indent=2))
sys.exit(1 if summary['failed'] else 0)
