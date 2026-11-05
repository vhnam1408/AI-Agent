"""Kiểm notebook và source nhúng; không tải dependency, build C++ hoặc gọi model."""
import ast
import base64
import hashlib
import io
import json
from pathlib import Path
import runpy
import sys
import uuid
import zipfile


def main():
    repo = Path(__file__).resolve().parents[2]
    notebook = json.loads((repo / 'notebooks/TASK025_AgentLoop_Kaggle.ipynb').read_text(encoding='utf-8'))
    assert notebook['nbformat'] == 4 and notebook['nbformat_minor'] == 5
    constants = {}
    count = 0
    for cell in notebook['cells']:
        source = ''.join(cell['source'])
        if cell['cell_type'] == 'code':
            assert cell['execution_count'] is None and cell['outputs'] == []
            count += 1
            for node in ast.parse(source).body:
                if isinstance(node, ast.Assign):
                    for target in node.targets:
                        if isinstance(target, ast.Name) and target.id in ('SOURCE_BASE64', 'SOURCE_SHA256'):
                            constants[target.id] = ast.literal_eval(node.value)
    raw = base64.b64decode(constants['SOURCE_BASE64'])
    assert hashlib.sha256(raw).hexdigest() == constants['SOURCE_SHA256']
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        manifest = json.loads(archive.read('TASK025_MANIFEST.json'))
        assert len(archive.namelist()) == len(manifest['files']) + 1
        for name, expected in manifest['files'].items():
            path = Path(name)
            assert not path.is_absolute() and '..' not in path.parts
            assert path.parts[0] not in ('.git', '.aws', '.codex', '.agents')
            data = archive.read(name)
            assert hashlib.sha256(data).hexdigest() == expected, name
            if name != 'TASK025_WORKTREE.patch':
                assert data == (repo / name).read_bytes(), 'Stale source: ' + name
        for name in ('src/agent/agent_cli.cpp', 'tests/task025/agent_loop_test.cpp',
                     'tests/task025/kaggle_runner.py', 'CMakeLists.txt'):
            assert name in manifest['files'], name
    for path in (repo / 'tests/task025').glob('*.py'):
        ast.parse(path.read_text(encoding='utf-8'))

    # Kiểm nhánh NOT_RUN và evidence khi một stage lỗi; không chạy Kaggle setup.
    work = repo / 'build' / ('task025-notebook-check-' + uuid.uuid4().hex[:8])
    work.mkdir(parents=True)
    session_class = runpy.run_path(str(repo / 'tests/task025/kaggle_runner.py'))['Session']
    session = session_class(work, repo, 'mock-not-loaded', 'http://127.0.0.1:1', False, False)
    session.setup_ollama()
    session.live()
    assert session.report['statuses']['live_agent'] == 'NOT_RUN'
    assert session.report['acceptance'] == 'NOT_READY'
    with session.stage('intentional_failure'):
        session.command('failure_log', [sys.executable, '-c', 'print("expected failure"); raise SystemExit(3)'])
    assert session.report['statuses']['intentional_failure'] == 'FAIL'
    assert (session.evidence / 'failure_log.stdout.txt').read_text().strip() == 'expected failure'
    archive_path = Path(session.archive())
    with zipfile.ZipFile(archive_path) as archive:
        report = json.loads(archive.read('report.json'))
        assert report['statuses']['intentional_failure'] == 'FAIL'
    print(json.dumps({'code_cells': count, 'snapshot_files': len(manifest['files']),
                      'snapshot_hash': constants['SOURCE_SHA256'], 'validation': 'PASS'}, indent=2))


if __name__ == '__main__':
    main()
