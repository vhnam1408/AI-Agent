"""Kiểm loader folder/ZIP của notebook gọn; không cài dependency hoặc gọi model."""
import ast
import hashlib
import json
from pathlib import Path
import shutil
import uuid
import zipfile


def main():
    repo = Path(__file__).resolve().parents[2]
    notebook_path = repo / 'notebooks/TASK025_Project_Kaggle.ipynb'
    notebook = json.loads(notebook_path.read_text(encoding='utf-8'))
    code = [''.join(cell['source']) for cell in notebook['cells'] if cell['cell_type'] == 'code']
    for cell in notebook['cells']:
        if cell['cell_type'] == 'code':
            assert cell['outputs'] == [] and cell['execution_count'] is None
            ast.parse(''.join(cell['source']))
    assert 'SOURCE_BASE64' not in notebook_path.read_text(encoding='utf-8')
    assert notebook_path.stat().st_size < 20000
    folder = repo / 'build' / ('task025-project-check-' + uuid.uuid4().hex[:8])
    working = folder / 'working'
    inputs = folder / 'input'
    working.mkdir(parents=True)
    inputs.mkdir()
    archive = repo / 'output/TASK025_Project.zip'
    shutil.copy2(archive, inputs / archive.name)
    loader = code[1].replace('/kaggle/working', working.as_posix()).replace('/kaggle/input', inputs.as_posix())
    results = []
    for label, origin in [('project_folder', str(repo)), ('project_zip', str(archive)), ('auto_detect_zip', '')]:
        context = {}
        exec(code[0], context)
        context.update(PROJECT_PATH=origin, RUN_LIVE=False, RUN_DEADLINE_REGRESSION=False)
        exec(loader, context)
        manifest = json.loads((context['SOURCE'] / 'TASK025_MANIFEST.json').read_text())
        assert manifest['snapshot_kind'] == 'uploaded_project'
        for path, digest in manifest['files'].items():
            assert hashlib.sha256((context['SOURCE'] / path).read_bytes()).hexdigest() == digest
        assert manifest['source_tree_sha256'] == context['SOURCE_TREE_SHA256']
        assert not (context['SOURCE'] / '.git').exists()
        exec(code[2], context)
        session = context['session']
        session.setup_ollama()
        session.live()
        assert session.report['statuses']['live_agent'] == 'NOT_RUN'
        results.append({'case': label, 'status': 'PASS'})
    bad = inputs / 'bad.zip'
    with zipfile.ZipFile(bad, 'w') as target:
        target.writestr('../escape.txt', 'must not be written')
    try:
        exec(loader, {'PROJECT_PATH': str(bad)})
    except RuntimeError as error:
        assert 'ZIP' in str(error)
    else:
        raise AssertionError('Unsafe ZIP entry accepted')
    assert not (working / 'escape.txt').exists()
    results.append({'case': 'reject_zip_traversal', 'status': 'PASS'})
    print(json.dumps({'validation': 'PASS', 'code_cells': len(code),
                      'notebook_bytes': notebook_path.stat().st_size, 'cases': results}, indent=2))


if __name__ == '__main__':
    main()
