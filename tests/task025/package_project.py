"""Đóng gói source/test hiện tại để upload làm Kaggle Input, không nhúng vào notebook."""
from pathlib import Path
import hashlib
import zipfile

REPO = Path(__file__).resolve().parents[2]
DIRECTORIES = ('src', 'include', 'tests', 'assets', 'config', 'skill', 'skills')
SKIP_PARTS = {'.git', 'build', 'output', '__pycache__', '.venv', 'node_modules'}


def main():
    files = [REPO / 'CMakeLists.txt']
    for name in DIRECTORIES:
        for path in (REPO / name).rglob('*'):
            relative = path.relative_to(REPO)
            if (path.is_file() and not path.is_symlink()
                    and not SKIP_PARTS.intersection(relative.parts)
                    and path.suffix not in ('.ipynb', '.pyc')
                    and path.name != '.env' and not path.name.startswith('.env.')):
                files.append(path)
    output = REPO / 'output/TASK025_Project.zip'
    output.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(files):
            archive.write(path, path.relative_to(REPO).as_posix())
    print('ZIP:', output)
    print('Files:', len(files), 'Bytes:', output.stat().st_size)
    print('SHA256:', hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
