"""Tạo notebook nhỏ, đọc thư mục/ZIP dự án trên Kaggle thay cho source Base64."""
import ast
import json
from pathlib import Path
import textwrap

REPO = Path(__file__).resolve().parents[2]


def main():
    cells = []

    def add(kind, source):
        source = textwrap.dedent(source).strip() + '\n'
        if kind == 'code':
            ast.parse(source)
        cell = {'cell_type': kind, 'id': f'task025-project-{len(cells):02d}',
                'metadata': {}, 'source': source.splitlines(keepends=True)}
        if kind == 'code':
            cell.update(execution_count=None, outputs=[])
        cells.append(cell)

    add('markdown', '''
        # TASK-025 — Kiểm thử thư mục dự án trên Kaggle

        Notebook này không nhúng source. Thêm thư mục dự án hoặc ZIP bằng Kaggle Input,
        điền đường dẫn ở cell cấu hình, rồi chạy các cell theo thứ tự.

        Từ máy của bạn, chạy `python tests/task025/package_project.py` để tạo
        `output/TASK025_Project.zip` gồm source hiện tại và các test, bỏ build và notebook lớn.
        Upload ZIP đó làm Input. Có thể dùng thư mục source trực tiếp nếu đã có trên Kaggle.

        Notebook chép source sang thư mục chạy mới trong `/kaggle/working`, ghi hash,
        rồi dùng `tests/task025/kaggle_runner.py` của chính dự án để build/test/chạy Agent.
        Bật Internet; chọn GPU khi chạy model lớn. Source phải có implementation TASK-025.
    ''')
    add('code', '''
        # Ví dụ: /kaggle/input/datasets/USER/DATASET/TASK025_Project.zip
        # Hoặc: /kaggle/input/datasets/USER/DATASET/AI-Agent
        # Có thể dùng thư mục source bạn đã chuẩn bị trong /kaggle/working.
        PROJECT_PATH = ""
        RUN_LIVE = True
        RUN_DEADLINE_REGRESSION = True
        LIVE_MODEL = "qwen3.8:27b"
        OLLAMA_URL = "http://127.0.0.1:11434"
    ''')
    add('markdown', '''
        ## 1. Đọc dự án từ thư mục hoặc ZIP
        Điền `PROJECT_PATH` tới thư mục chứa CMakeLists.txt, hoặc tới ZIP dự án.
        Nếu để trống, notebook thử tìm đúng một thư mục TASK-025 trong Kaggle Input,
        rồi mới thử tìm đúng một file TASK025_Project.zip. Khi có nhiều lựa chọn,
        notebook yêu cầu chỉ rõ đường dẫn để tránh kiểm nhầm phiên bản.
    ''')
    add('code', '''
        from datetime import datetime, timezone
        import hashlib, json, shutil, uuid, zipfile
        from pathlib import Path

        REQUIRED = ("CMakeLists.txt", "src/agent/agent_loop.cpp",
                    "src/agent/agent_cli.cpp", "include/agent/agent_loop.h",
                    "tests/task025/kaggle_runner.py", "tests/task025/agent_loop_test.cpp")

        def is_project(path):
            return all((path / name).is_file() for name in REQUIRED)

        working = Path('/kaggle/working')
        if not working.is_dir():
            raise RuntimeError('Chạy notebook trong Kaggle.')
        if PROJECT_PATH.strip():
            origin = Path(PROJECT_PATH).expanduser().resolve()
        else:
            inputs = Path('/kaggle/input')
            projects = sorted({p.parent.resolve() for p in inputs.rglob('CMakeLists.txt')
                               if is_project(p.parent)})
            if len(projects) == 1:
                origin = projects[0]
            elif len(projects) > 1:
                raise RuntimeError('Có nhiều dự án; điền PROJECT_PATH: ' + str(projects))
            else:
                archives = sorted(inputs.rglob('TASK025_Project.zip'))
                if len(archives) != 1:
                    raise RuntimeError('Thêm dự án vào Input rồi điền PROJECT_PATH.')
                origin = archives[0].resolve()
        if not origin.exists():
            raise FileNotFoundError(str(origin))

        RUN = working / ('task025-project-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S')
                         + '-' + uuid.uuid4().hex[:6])
        RUN.mkdir()
        INPUT_ARCHIVE_SHA256 = None
        if origin.is_file():
            INPUT_ARCHIVE_SHA256 = hashlib.sha256(origin.read_bytes()).hexdigest()
            unpacked = RUN / 'uploaded'
            unpacked.mkdir()
            with zipfile.ZipFile(origin) as archive:
                for item in archive.infolist():
                    relative = Path(item.filename.replace('\\\\', '/'))
                    if relative.is_absolute() or '..' in relative.parts or ':' in item.filename:
                        raise RuntimeError('Đường dẫn ZIP không hợp lệ: ' + item.filename)
                    target = unpacked / relative
                    if item.is_dir():
                        target.mkdir(parents=True, exist_ok=True)
                    else:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(archive.read(item))
            candidates = [p.parent for p in unpacked.rglob('CMakeLists.txt') if is_project(p.parent)]
            if len(candidates) != 1:
                raise RuntimeError('ZIP phải có đúng một dự án chứa các file TASK-025.')
            project = candidates[0]
        else:
            project = origin
        if not is_project(project):
            raise RuntimeError('Thiếu file dự án: ' + str([name for name in REQUIRED
                                                          if not (project / name).is_file()]))

        SOURCE = RUN / 'source'
        SOURCE.mkdir()
        shutil.copy2(project / 'CMakeLists.txt', SOURCE / 'CMakeLists.txt')
        for name in ('src', 'include', 'tests', 'assets', 'config', 'skill', 'skills'):
            if (project / name).is_dir():
                shutil.copytree(project / name, SOURCE / name,
                    ignore=shutil.ignore_patterns('.git', 'build', 'output', '__pycache__',
                                                 '*.pyc', '*.ipynb', '.env', '.env.*'))
        files = {str(p.relative_to(SOURCE)).replace('\\\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sorted(SOURCE.rglob('*')) if p.is_file()}
        SOURCE_TREE_SHA256 = hashlib.sha256(json.dumps(files, sort_keys=True,
                                                       separators=(',', ':')).encode()).hexdigest()
        manifest = {'task': 'TASK-025', 'snapshot_kind': 'uploaded_project',
                    'source_origin': str(origin), 'source_tree_sha256': SOURCE_TREE_SHA256,
                    'input_archive_sha256': INPUT_ARCHIVE_SHA256, 'files': files}
        (SOURCE / 'TASK025_MANIFEST.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        print('Project:', origin)
        print('Copied files:', len(files))
        print('Source hash:', SOURCE_TREE_SHA256)
        print('Run folder:', RUN)
    ''')
    add('markdown', '## 2. Khởi tạo báo cáo từ source vừa chọn')
    add('code', '''
        import runpy
        runner = runpy.run_path(str(SOURCE / 'tests/task025/kaggle_runner.py'))
        session = runner['Session'](RUN, SOURCE, LIVE_MODEL, OLLAMA_URL,
                                    RUN_LIVE, RUN_DEADLINE_REGRESSION)
        session.report.update(source_origin=str(origin), source_tree_sha256=SOURCE_TREE_SHA256,
                              input_archive_sha256=INPUT_ARCHIVE_SHA256)
        session.report['statuses']['source'] = 'PASS'
        session.archive()
    ''')
    add('markdown', '## 3. Chuẩn bị toolchain C++23\nTái sử dụng môi trường compiler nếu đã cài từ phiên TASK-025 trước.')
    add('code', 'session.toolchain()')
    add('markdown', '## 4. Build và test thư mục dự án\nChạy root CTest và tùy chọn regression TASK-017/021.')
    add('code', 'session.build_and_test()')
    add('markdown', '## 5. Chuẩn bị Ollama\nBỏ qua khi RUN_LIVE=False. Giữ đúng tag model đã chọn.')
    add('code', 'session.setup_ollama()')
    add('markdown', '## 6. Chạy Agent thật\nKiểm Calculator, history và đáp án 4014 trong budget 6 bước.')
    add('code', 'session.live()')
    add('markdown', '''
        ## 7. Tải evidence
        `core=PASS`: build và root tests đạt. `live_agent=PASS`: model thật đã chạy đúng demo.
        `RUN_LIVE=False` sẽ ghi live là NOT_RUN. Manifest ghi hash của chính source vừa upload;
        kết quả này là một lần chạy mới, không thay thế ZIP evidence của notebook snapshot cũ.
    ''')
    add('code', '''
        from IPython.display import FileLink, display
        archive_path = session.archive()
        print(json.dumps(session.report, ensure_ascii=False, indent=2))
        display(FileLink(archive_path))
    ''')
    notebook = {'nbformat': 4, 'nbformat_minor': 5,
                'metadata': {'kernelspec': {'display_name': 'Python 3', 'language': 'python', 'name': 'python3'},
                             'language_info': {'name': 'python'},
                             'kaggle': {'isInternetEnabled': True, 'isGpuEnabled': True}},
                'cells': cells}
    destination = REPO / 'notebooks/TASK025_Project_Kaggle.ipynb'
    destination.write_text(json.dumps(notebook, ensure_ascii=False, indent=1), encoding='utf-8')
    print('Created:', destination, 'bytes:', destination.stat().st_size)


if __name__ == '__main__':
    main()
