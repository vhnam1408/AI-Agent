"""Các bước Kaggle cho TASK-025; được notebook gọi, không tự chạy khi import."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
from contextlib import contextmanager


class Session:
    def __init__(self, work, source, model, url, run_live, deadline_tests=True):
        self.work, self.source = Path(work), Path(source)
        self.evidence = self.work / 'evidence'
        self.evidence.mkdir(exist_ok=True)
        self.model, self.url = model, url.rstrip('/')
        self.run_live, self.deadline_tests = run_live, deadline_tests
        self.report = {'task': 'TASK-025', 'model_requested': model,
                       'statuses': {}, 'commands': {}, 'acceptance': 'NOT_RUN'}
        self.prefix = []
        self.build = self.work / 'build'
        self.ollama_process = None
        self.save()

    def save(self):
        statuses = self.report['statuses']
        core = ('toolchain', 'root_build', 'root_ctest')
        self.report['core'] = 'PASS' if all(statuses.get(k) == 'PASS' for k in core) else 'NOT_PASS'
        self.report['acceptance'] = (
            'READY_FOR_REVIEW' if self.report['core'] == 'PASS'
            and statuses.get('live_agent') == 'PASS'
            and (not self.deadline_tests or statuses.get('deadline_regression') == 'PASS')
            else 'NOT_READY')
        manifest = self.source / 'TASK025_MANIFEST.json'
        if manifest.exists():
            shutil.copy2(manifest, self.evidence / manifest.name)
        patch = self.source / 'TASK025_WORKTREE.patch'
        if patch.exists():
            shutil.copy2(patch, self.evidence / patch.name)
        (self.evidence / 'report.json').write_text(
            json.dumps(self.report, ensure_ascii=False, indent=2), encoding='utf-8')

    def archive(self):
        self.save()
        archive = shutil.make_archive(str(self.work / 'task025_evidence'), 'zip', self.evidence)
        print('Evidence:', archive)
        return archive

    @contextmanager
    def stage(self, name):
        self.report['statuses'][name] = 'RUNNING'
        self.save()
        try:
            yield
        except Exception as error:
            self.report['statuses'][name] = 'FAIL'
            self.report[name + '_error'] = str(error)
            print(name, 'FAIL:', error)
        else:
            self.report['statuses'][name] = 'PASS'
        finally:
            self.archive()

    def command(self, label, arguments, timeout=1200, check=True, env=None):
        arguments = [str(arg) for arg in arguments]
        print('Running:', label, flush=True)
        start = time.monotonic()
        try:
            result = subprocess.run(arguments, capture_output=True, timeout=timeout,
                                    env=env, text=True, encoding='utf-8', errors='replace')
        except subprocess.TimeoutExpired as error:
            for suffix, content in [('stdout', error.stdout), ('stderr', error.stderr)]:
                if isinstance(content, bytes):
                    content = content.decode('utf-8', errors='replace')
                (self.evidence / f'{label}.{suffix}.txt').write_text(content or '', encoding='utf-8')
            self.report['commands'][label] = {'args': arguments, 'status': 'TIMEOUT'}
            self.save()
            raise
        (self.evidence / f'{label}.stdout.txt').write_text(result.stdout, encoding='utf-8')
        (self.evidence / f'{label}.stderr.txt').write_text(result.stderr, encoding='utf-8')
        self.report['commands'][label] = {'args': arguments, 'returncode': result.returncode,
                                          'seconds': round(time.monotonic() - start, 2)}
        self.save()
        print((result.stdout + result.stderr)[-2000:], flush=True)
        if check and result.returncode:
            raise RuntimeError(f'{label}: exit {result.returncode}; read saved logs')
        return result

    def toolchain(self):
        with self.stage('toolchain'):
            # Dùng thư mục mới riêng để không thay môi trường notebook TASK-013/021.
            tools = Path('/kaggle/working/task025-tools')
            tools.mkdir(exist_ok=True)
            mm = tools / 'micromamba'
            if not mm.exists():
                raw = urllib.request.urlopen(
                    'https://micro.mamba.pm/api/micromamba/linux-64/latest', timeout=120).read()
                self.report['micromamba_archive_sha256'] = hashlib.sha256(raw).hexdigest()
                with tarfile.open(fileobj=io.BytesIO(raw), mode='r:bz2') as archive:
                    stream = archive.extractfile('bin/micromamba')
                    if stream is None:
                        raise RuntimeError('Missing micromamba binary')
                    mm.write_bytes(stream.read())
                mm.chmod(0o755)
            environment = tools / 'compiler'
            compilers = list((environment / 'bin').glob('*conda-linux-gnu-c++'))
            if not compilers:
                self.command('install_toolchain', [mm, 'create', '-y', '-p', environment,
                             '-c', 'conda-forge', 'gxx_linux-64=15', 'cmake', 'ninja',
                             'libcurl', 'sqlite'], timeout=2400)
            compilers = sorted((environment / 'bin').glob('*conda-linux-gnu-c++'))
            if not compilers:
                raise RuntimeError('C++ compiler unavailable')
            self.prefix = [str(mm), 'run', '-p', str(environment)]
            self.flags = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                          '-DCMAKE_CXX_COMPILER=' + str(compilers[0]),
                          '-DCMAKE_PREFIX_PATH=' + str(environment),
                          '-DPython3_EXECUTABLE=' + sys.executable]
            self.command('compiler', self.prefix + [str(compilers[0]), '--version'])
            self.command('packages', [mm, 'list', '-p', environment, '--explicit'])

    def build_and_test(self):
        if self.report['statuses'].get('toolchain') != 'PASS':
            self.report['statuses']['root_build'] = 'BLOCKED'
            self.archive()
            return
        with self.stage('root_build'):
            self.command('root_configure', self.prefix + ['cmake', '-S', self.source,
                         '-B', self.build, '-DBUILD_TESTING=ON'] + self.flags)
            self.command('root_build', self.prefix + ['cmake', '--build', self.build, '--parallel', '2'])
        if self.report['statuses']['root_build'] != 'PASS':
            self.report['statuses']['root_ctest'] = 'BLOCKED'
            self.archive()
            return
        with self.stage('root_ctest'):
            inventory = self.command('test_inventory', self.prefix + [
                'ctest', '--test-dir', self.build, '--show-only=json-v1'])
            names = {test['name'] for test in json.loads(inventory.stdout)['tests']}
            if not {'task025_unit', 'task025_http', 'agent_contract'}.issubset(names):
                raise RuntimeError('Required TASK-025 tests are not registered')
            try:
                self.command('root_ctest', self.prefix + [
                    'ctest', '--test-dir', self.build, '--output-on-failure', '--no-tests=error'])
            finally:
                for name in ('task025_unit_results.json', 'task025_http_results.json',
                             'parser_results.json', 'client_results.json'):
                    result = self.build / name
                    if result.exists():
                        shutil.copy2(result, self.evidence / name)
            for name in ('task025_unit_results.json', 'task025_http_results.json'):
                data = json.loads((self.evidence / name).read_text(encoding='utf-8'))
                if data['failed'] or data['passed'] < 1:
                    raise RuntimeError('Test report has failures: ' + name)
        if self.deadline_tests:
            with self.stage('deadline_regression'):
                build = self.work / 'deadline-build'
                self.command('deadline_configure', self.prefix + ['cmake', '-S',
                             self.source / 'tests/deadline_extractor', '-B', build] + self.flags)
                self.command('deadline_build', self.prefix + ['cmake', '--build', build, '--parallel', '2'])
                self.command('deadline_ctest', self.prefix + [
                    'ctest', '--test-dir', build, '--output-on-failure', '--no-tests=error'])

    def api(self, path, payload=None, timeout=30):
        data = None if payload is None else json.dumps(payload).encode()
        request = urllib.request.Request(self.url + path, data=data,
                                         headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.load(response)

    def setup_ollama(self):
        if not self.run_live:
            self.report['statuses']['ollama'] = 'NOT_RUN'
            self.archive()
            return
        with self.stage('ollama'):
            if shutil.which('nvidia-smi'):
                self.command('gpu', ['nvidia-smi'], check=False, timeout=30)
            else:
                self.report['gpu'] = 'nvidia-smi unavailable; CPU inference may be slow'
            if self.url != 'http://127.0.0.1:11434':
                self.report['server'] = 'external; install/pull skipped'
                self.report['ollama_version'] = self.api('/api/version')
            else:
                if not shutil.which('ollama'):
                    self.command('apt_update', ['apt-get', 'update'], timeout=600)
                    self.command('ollama_dependencies', ['apt-get', 'install', '-y',
                                 'curl', 'ca-certificates', 'zstd', 'pciutils'], timeout=600)
                    installer = self.work / 'install_ollama.sh'
                    urllib.request.urlretrieve('https://ollama.com/install.sh', installer)
                    self.report['ollama_installer_sha256'] = hashlib.sha256(installer.read_bytes()).hexdigest()
                    self.command('ollama_install', ['sh', installer], timeout=1200)
                environment = os.environ.copy()
                environment.update({'OLLAMA_HOST': '127.0.0.1:11434',
                                    'OLLAMA_MODELS': '/kaggle/working/task025-models',
                                    'OLLAMA_NUM_PARALLEL': '1', 'OLLAMA_CONTEXT_LENGTH': '8192',
                                    'OLLAMA_FLASH_ATTENTION': '1', 'OLLAMA_NO_CLOUD': '1'})
                try:
                    self.report['ollama_version'] = self.api('/api/version', timeout=3)
                    self.report['server'] = 'reused existing server; original environment retained'
                except Exception:
                    self.server_log = open(self.evidence / 'ollama_server.log', 'a', encoding='utf-8')
                    self.ollama_process = subprocess.Popen(['ollama', 'serve'], env=environment,
                                                          stdout=self.server_log, stderr=subprocess.STDOUT)
                    for _ in range(120):
                        if self.ollama_process.poll() is not None:
                            raise RuntimeError('Ollama stopped; see ollama_server.log')
                        try:
                            self.report['ollama_version'] = self.api('/api/version', timeout=2)
                            break
                        except Exception:
                            time.sleep(1)
                    else:
                        raise RuntimeError('Ollama server not ready')
                    self.report['server'] = 'started by notebook'
                tags = self.api('/api/tags').get('models', [])
                if not any(item.get('name') == self.model for item in tags):
                    self.command('ollama_pull', ['ollama', 'pull', self.model], env=environment, timeout=3600)
            info = self.api('/api/show', {'model': self.model})
            (self.evidence / 'model_info.json').write_text(json.dumps(info, indent=2), encoding='utf-8')
            tags = self.api('/api/tags').get('models', [])
            selected = next((item for item in tags if item.get('name') == self.model), None)
            if not selected:
                raise RuntimeError('Requested model tag not found; model is never changed automatically')
            self.report['model_digest'] = selected.get('digest')

    def live(self):
        if not self.run_live:
            self.report['statuses']['live_agent'] = 'NOT_RUN'
            self.archive()
            return
        if any(self.report['statuses'].get(key) != 'PASS' for key in ('root_build', 'ollama')):
            self.report['statuses']['live_agent'] = 'BLOCKED'
            self.archive()
            return
        with self.stage('live_agent'):
            output = self.evidence / 'live_agent.json'
            result = self.command('live_agent', self.prefix + [self.build / 'ai_agent', '--agent',
                '--model', self.model, '--url', self.url, '--goal',
                'Use calculator to evaluate 137 * 29 + 41. Read the Observation and report the result.',
                '--max-steps', '6', '--max-tokens', '4096', '--timeout-ms', '600000',
                '--output', output], timeout=3600, check=False)
            if not output.exists():
                raise RuntimeError('CLI did not create live_agent.json')
            data = json.loads(output.read_text(encoding='utf-8'))
            if result.returncode or not data['success'] or data['status'] != 'completed':
                raise RuntimeError('Agent did not complete; raw content is retained in events')
            calls = [event for event in data['events'] if event.get('tool_result')]
            if not any(event['tool_result']['success'] and event['tool_result']['message'] == '4014' for event in calls):
                raise RuntimeError('Missing real calculator result 4014; direct model arithmetic is not a pass')
            if '4014' not in data['answer']:
                raise RuntimeError('Final answer does not contain the expected value')
            if len(data['events']) != data['steps'] or data['steps'] > 6:
                raise RuntimeError('Step budget/event mismatch')
            for event in data['events']:
                if event.get('model_response', {}).get('model_used') != self.model:
                    raise RuntimeError('Model tag differs from the requested tag')
            print('Live Agent PASS:', data['answer'])
