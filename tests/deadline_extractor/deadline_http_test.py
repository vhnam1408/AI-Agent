"""Run only on Kaggle via CTest: extractor -> real Client C++ -> mock HTTP."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import subprocess
import sys
import threading

probe, fixtures, output = map(Path, sys.argv[1:4])
records = []
cases = []
response_item = {
    'kind': 'submission', 'course': 'OOP', 'title': 'Báo cáo',
    'date_text': 'Nộp Báo cáo OOP ngày 24/10/2026 lúc 23:59.',
    'location': None, 'evidence': 'Nộp Báo cáo OOP ngày 24/10/2026 lúc 23:59.'}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        # Giữ stdout sạch để CTest thu JSON report.
        pass

    def do_POST(self):
        # Trả envelope Ollama; schema extraction nằm trong content, không có tool calls.
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        records.append((self.path, payload))
        mode = payload['model']
        content = json.dumps({'items': [response_item]}, ensure_ascii=False)
        if mode == 'bad-json':
            content = '{bad'
        if mode == 'tool-json':
            content = json.dumps({'tool_name': 'task_lookup', 'arguments': {}})
        data = {'model': mode, 'message': {'role': 'assistant', 'content': content},
                'done': True, 'done_reason': 'length' if mode == 'truncated' else 'stop',
                'prompt_eval_count': 20, 'eval_count': 30, 'total_duration': 1000000}
        if mode == 'missing-year':
            item = dict(response_item)
            item['evidence'] = item['date_text'] = 'Nộp Báo cáo OOP ngày 24/10 lúc 23:59.'
            data['message']['content'] = json.dumps({'items': [item]}, ensure_ascii=False)
        if mode == 'omitted-clock':
            item = dict(response_item)
            item['date_text'] = '24/10/2026'
            data['message']['content'] = json.dumps({'items': [item]}, ensure_ascii=False)
        if mode == 'online-location':
            item = dict(response_item)
            item['evidence'] = item['date_text'] = 'Nộp Báo cáo OOP ngày 24/10/2026 lúc 23:59 trên hệ thống LMS.'
            item['location'] = 'hệ thống LMS'
            data['message']['content'] = json.dumps({'items': [item]}, ensure_ascii=False)
        status = 503 if mode == 'http-error' else 200
        if status != 200:
            data = {'error': 'mock unavailable'}
        raw = json.dumps(data, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check(name, body):
    # Thu mọi ca độc lập; không che các lỗi sau ca đầu.
    try:
        body()
        cases.append({'name': name, 'status': 'PASS'})
    except Exception as error:
        cases.append({'name': name, 'status': 'FAIL', 'reason': str(error)})


with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        def run(mode, source='complete.txt'):
            records.clear()
            completed = subprocess.run([str(probe), '--text', str(fixtures / source),
                f'http://127.0.0.1:{server.server_port}', mode], capture_output=True, timeout=40)
            return completed, json.loads(completed.stdout)

        def valid():
            completed, data = run('valid')
            require(completed.returncode == 0 and data['tasks'][0]['deadline_utc'] == '2026-10-24T16:59:00Z', 'Wrong deadline')
            require(len(records) == 1 and records[0][0] == '/api/chat', 'Wrong endpoint/request count')
            payload = records[0][1]
            require(payload['stream'] is False and 'tools' not in payload, 'Extraction treated as tool calling')
            require(payload['messages'][0]['role'] == 'system' and payload['messages'][1]['role'] == 'user', 'Lost request messages')
            require(response_item['evidence'] in payload['messages'][1]['content'], 'Source text lost')
            require(data['response_metadata']['http_status'] == 200, 'Lost metadata')
        check('real_client_extractor_http', valid)

        for mode, expected in [('bad-json', 'InvalidJson'), ('tool-json', 'InvalidSchema'),
                               ('truncated', 'TruncatedResponse'), ('http-error', 'LLMFailure')]:
            def invalid(mode=mode, expected=expected):
                completed, data = run(mode)
                require(completed.returncode != 0 and data['error'] == expected, f'Wrong error: {data}')
            check(mode, invalid)

        def missing_year():
            completed, data = run('missing-year', 'missing_year.txt')
            item = data['tasks'][0]
            require(completed.returncode == 0 and item['deadline_utc'] is None and 'year' in item['missing_fields'], 'Invented year')
        check('missing_year_real_client', missing_year)

        def omitted_clock():
            completed, data = run('omitted-clock')
            item = data['tasks'][0]
            codes = {issue['code'] for issue in item['issues']}
            require(completed.returncode == 0 and item['validity'] == 'invalid'
                    and item['deadline_utc'] is None and 'omitted_source_time' in codes,
                    'Source clock omission was not reported')
        check('printed_clock_omission_real_client', omitted_clock)

        def online_location():
            completed, data = run('online-location', 'online_location.txt')
            item = data['tasks'][0]
            require(completed.returncode == 0 and item['location'] == 'hệ thống LMS',
                    'Online location lost in Client/extractor round trip')
            require(item['deadline_utc'] == '2026-10-24T16:59:00Z', 'Online deadline changed')
        check('online_location_real_client', online_location)
    finally:
        server.shutdown()
        thread.join()

summary = {'passed': sum(c['status'] == 'PASS' for c in cases),
           'failed': sum(c['status'] == 'FAIL' for c in cases), 'cases': cases}
output.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(summary, ensure_ascii=False, indent=2))
sys.exit(1 if summary['failed'] else 0)
