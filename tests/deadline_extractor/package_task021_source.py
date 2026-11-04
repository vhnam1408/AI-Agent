"""Đóng gói source hiện tại để upload Kaggle; không build, test hoặc thay đổi Git."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import uuid
from zipfile import ZipFile, ZIP_DEFLATED


def git(root, *arguments):
    """Chỉ đọc danh sách file và provenance Git, không stage/commit/push."""
    return subprocess.check_output(['git', '-C', str(root), *arguments])


def main():
    """Chọn file tracked cùng source TASK-021 chưa commit; xuất ZIP và manifest SHA-256."""
    root = Path(__file__).resolve().parents[2]
    additions = {
        'include/agent/deadline_confirmation.h',
        'src/agent/deadline_confirmation.cpp',
        'tests/deadline_extractor/deadline_confirmation_test.cpp',
        'tests/deadline_extractor/AI_Agent_TASK021_Kaggle.ipynb',
        'tests/deadline_extractor/package_task021_source.py',
        'tests/deadline_extractor/make_task021_standalone.py',
        'docs/TASK-021.md',
    }
    tracked = set(git(root, 'ls-files', '-z').decode('utf-8').rstrip('\0').split('\0'))
    payload = {}
    for relative in sorted(tracked | additions):
        path = root / relative
        if not path.resolve().is_relative_to(root) or path.is_symlink() or not path.is_file():
            raise ValueError(f'Source thiếu hoặc nằm ngoài repository: {relative}')
        payload[relative] = path.read_bytes()
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in payload.items()}
    source_id = hashlib.sha256(json.dumps(hashes, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    manifest = {
        'schema_version': 1, 'kind': 'task021-working-tree', 'source_id': source_id,
        'created_at_utc': datetime.now(timezone.utc).isoformat(),
        'base_commit': git(root, 'rev-parse', 'HEAD').decode().strip(),
        'branch': git(root, 'branch', '--show-current').decode().strip(),
        'git_status': git(root, 'status', '--porcelain=v1').decode('utf-8'),
        'note': 'Working tree snapshot; base_commit is not the tested source identity.',
        'files': hashes,
    }
    destination = root / 'output'
    destination.mkdir(exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    bundle = destination / f'task021-source-{timestamp}-{uuid.uuid4().hex[:8]}.zip'
    with ZipFile(bundle, 'w', ZIP_DEFLATED) as archive:
        archive.writestr('task021-source-manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2))
        for relative, data in payload.items():
            archive.writestr('AI-Agent/' + relative, data)
    digest = hashlib.sha256(bundle.read_bytes()).hexdigest()
    bundle.with_suffix('.sha256.txt').write_text(digest + '  ' + bundle.name + '\n', encoding='utf-8')
    print(json.dumps({'bundle': str(bundle), 'sha256': digest,
                      'source_id': source_id, 'files': len(payload)}, indent=2))


if __name__ == '__main__':
    main()
