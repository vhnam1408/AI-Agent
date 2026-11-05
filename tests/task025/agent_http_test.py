"""CLI + OllamaClient + parser + Registry + CalculatorTool; chỉ HTTP/model là giả."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def main():
    app, report_path = sys.argv[1:3]
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            requests.append(data)
            model = data['model']
            observed = data['messages'][-1]['content']
            reply = {'tool_name': 'calculator', 'arguments': {'expression': '15 + 17'}}
            if model == 'recover':
                if 'Loi Tool:' in observed:
                    pass
                elif observed == 'Observation: 32':
                    reply = {'final_answer': '32'}
                else:
                    reply['arguments']['expression'] = '1 / 0'
            elif model == 'complete' and observed == 'Observation: 32':
                reply = {'final_answer': '32'}
            content = '{bad' if model == 'bad-json' else json.dumps(reply)
            raw = json.dumps({'model': model, 'done': True,
                              'message': {'role': 'assistant', 'content': content}}).encode()
            if model == 'llm-error':
                self.send_response(500)
            else:
                self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    outcomes = []
    try:
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'agent.json'

            def run(model, status, steps, max_steps=8):
                requests.clear()
                completed = subprocess.run([
                    app, '--agent', '--model', model, '--url',
                    f'http://127.0.0.1:{server.server_port}', '--goal', 'Calculate 15 + 17 using calculator',
                    '--max-steps', str(max_steps), '--output', str(output)
                ], capture_output=True, timeout=20)
                data = json.loads(output.read_text(encoding='utf-8'))
                assert data['status'] == status, data
                assert data['steps'] == steps and len(data['events']) == steps, data
                assert data['success'] == (status == 'completed'), data
                assert completed.returncode == (0 if data['success'] else 2), completed.stderr
                outcomes.append({'name': model, 'status': 'PASS'})
                return data

            data = run('complete', 'completed', 2)
            assert data['answer'] == '32' and data['tool_calls'] == 1
            assert data['events'][0]['tool_result'] == {'success': True, 'message': '32'}
            assert requests[1]['messages'][-2]['role'] == 'assistant'
            assert requests[1]['messages'][-1]['content'] == 'Observation: 32'
            run('budget', 'max_steps_reached', 1, max_steps=1)
            run('repeat', 'loop_detected', 4)
            run('bad-json', 'invalid_decision', 1)
            run('llm-error', 'llm_error', 1)
            data = run('recover', 'completed', 3)
            assert data['events'][0]['tool_result']['success'] is False
            assert data['events'][1]['tool_result']['message'] == '32'
            for value in ['0', '-1', '1.5', '100abc']:
                bad = subprocess.run([app, '--agent', '--model', 'complete', '--goal', 'test',
                                      '--max-steps', value], capture_output=True, timeout=5)
                assert bad.returncode == 1
            outcomes.append({'name': 'invalid_cli_budget', 'status': 'PASS'})
        report = {'task': 'TASK-025', 'mode': 'http_mock', 'passed': len(outcomes),
                  'failed': 0, 'tests': outcomes}
        Path(report_path).write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps(report, indent=2))
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    main()
