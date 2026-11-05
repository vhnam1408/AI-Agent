"""Đóng gói working tree chọn lọc thành notebook Kaggle tự chứa source."""
import ast
import base64
import hashlib
import io
import json
from pathlib import Path
import subprocess
import textwrap
import zipfile

REPO = Path(__file__).resolve().parents[2]


def main():
    tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=REPO).decode().split('\0')
    paths = {path for path in tracked if path and (
        path.startswith(('src/', 'include/', 'tests/', 'assets/', 'config/'))
        or path in ('CMakeLists.txt', '.gitignore')) and not path.endswith('.ipynb')}
    paths.update(str(path.relative_to(REPO)).replace('\\', '/')
                 for path in (REPO / 'tests/task025').glob('*.py'))
    paths.update({'tests/task025/agent_loop_test.cpp', 'include/agent/agent_cli.h',
                  'src/agent/agent_cli.cpp', 'docs/TASK-025.md'})
    files = {path: (REPO / path).read_bytes() for path in sorted(paths)}
    files['TASK025_WORKTREE.patch'] = subprocess.check_output(
        ['git', 'diff', '--no-ext-diff', '--', 'CMakeLists.txt', 'include', 'src', 'tests'], cwd=REPO)
    manifest = {
        'task': 'TASK-025', 'snapshot_kind': 'selected_working_tree_not_a_commit',
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO).decode().strip(),
        'files': {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}}
    files['TASK025_MANIFEST.json'] = json.dumps(manifest, ensure_ascii=False, indent=2).encode()
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    raw = buffer.getvalue()
    encoded, digest = base64.b64encode(raw).decode(), hashlib.sha256(raw).hexdigest()
    cells = []

    def add(kind, source):
        source = textwrap.dedent(source).strip() + '\n'
        if kind == 'code':
            ast.parse(source)
        cell = {'cell_type': kind, 'id': f'task025-{len(cells):02d}', 'metadata': {},
                'source': source.splitlines(keepends=True)}
        if kind == 'code':
            cell.update(execution_count=None, outputs=[])
        cells.append(cell)

    add('markdown', '''
        # TASK-025 — Chạy ReAct AgentLoop trên Kaggle

        Notebook tự chứa source hiện tại, không cần clone/push GitHub. Bật **Internet**;
        chọn **GPU** nếu chạy model lớn. `RUN_LIVE=False` chỉ build và kiểm thử bằng mock.
        Mặc định dùng tag model trong các notebook dự án trước; bạn có thể sửa `LIVE_MODEL`
        theo tag thực tế. Notebook không tự đổi model khi pull/inference lỗi.

        TASK-025 gồm vòng lặp, history và max_steps. Luồng đề cương → lập lịch MVP thuộc
        TASK-029. Demo ở đây gọi CalculatorTool thật để kiểm chứng vòng Agent.
        Notebook chưa có output chạy sẵn; kết quả Kaggle chỉ xuất hiện sau khi bạn chạy.
    ''')
    add('code', '''
        RUN_LIVE = True
        RUN_DEADLINE_REGRESSION = True
        LIVE_MODEL = "qwen3.8:27b"
        OLLAMA_URL = "http://127.0.0.1:11434"
        # Nếu đã có server ở phiên trước, notebook tái sử dụng và giữ môi trường server đó.
        # Với URL server riêng, notebook bỏ qua cài/pull và kiểm đúng model được chọn.
    ''')
    add('markdown', '## 1. Khôi phục source và kiểm hash\nMỗi lần chạy tạo thư mục mới; không dùng binary cũ.')
    restore = '''
        import base64, hashlib, io, json, uuid, zipfile
        from datetime import datetime, timezone
        from pathlib import Path
        if not Path('/kaggle/working').is_dir():
            raise RuntimeError('Import notebook này vào Kaggle trước khi chạy.')
        RUN = Path('/kaggle/working') / ('task025-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])
        SOURCE = RUN / 'source'
        SOURCE.mkdir(parents=True)
        SOURCE_SHA256 = '__HASH__'
        SOURCE_BASE64 = '__BASE64__'
        raw = base64.b64decode(SOURCE_BASE64)
        if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
            raise RuntimeError('Source ZIP hash mismatch')
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            for info in archive.infolist():
                relative = Path(info.filename)
                if relative.is_absolute() or '..' in relative.parts:
                    raise RuntimeError('Invalid snapshot path')
                target = SOURCE / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(info))
        manifest = json.loads((SOURCE / 'TASK025_MANIFEST.json').read_text(encoding='utf-8'))
        for name, expected in manifest['files'].items():
            if hashlib.sha256((SOURCE / name).read_bytes()).hexdigest() != expected:
                raise RuntimeError('File hash mismatch: ' + name)
        print('Verified', len(manifest['files']), 'files. Base commit:', manifest['base_commit'])
        print('Working tree snapshot SHA256:', SOURCE_SHA256)
        print('Run directory:', RUN)
    '''
    add('code', textwrap.dedent(restore).replace('__HASH__', digest).replace('__BASE64__', encoded))
    add('markdown', '## 2. Khởi tạo báo cáo\nMỗi bước lưu log riêng và cập nhật ZIP evidence, kể cả khi bước đó lỗi.')
    add('code', '''
        import runpy
        runner = runpy.run_path(str(SOURCE / 'tests/task025/kaggle_runner.py'))
        session = runner['Session'](RUN, SOURCE, LIVE_MODEL, OLLAMA_URL, RUN_LIVE, RUN_DEADLINE_REGRESSION)
        session.report['statuses']['source'] = 'PASS'
        session.report['source_zip_sha256'] = SOURCE_SHA256
        session.archive()
    ''')
    add('markdown', '''
        ## 3. Cài toolchain C++23
        Dùng micromamba riêng với GCC 15, CMake/Ninja, libcurl và SQLite.
        Lần đầu cần tải dependency; các phiên sau có thể tái sử dụng môi trường.
    ''')
    add('code', 'session.toolchain()')
    add('markdown', '''
        ## 4. Build dự án và chạy kiểm thử
        Chạy toàn bộ root CTest, gồm test mới TASK-025 và các regression đã đăng ký.
        Tùy chọn chạy thêm bộ TASK-017/021 ở build riêng. Mock HTTP chỉ giả server/model;
        CLI C++, parser, Registry và CalculatorTool vẫn là code thật.
    ''')
    add('code', 'session.build_and_test()')
    add('markdown', '''
        ## 5. Chuẩn bị Ollama
        Bỏ qua nếu `RUN_LIVE=False`. Model lớn cần đủ RAM/VRAM và thời gian tải.
        Chọn model khác bằng cách sửa cấu hình, không đổi tag sau khi chấm kết quả.
        Nếu đã có model trên server hiện tại, bước này không tải lại.
    ''')
    add('code', 'session.setup_ollama()')
    add('markdown', '''
        ## 6. Chạy Agent với model thật
        Yêu cầu tính `137 * 29 + 41` qua tool; đáp án đúng là `4014`.
        Chỉ PASS khi trace có kết quả CalculatorTool thật, Agent hoàn thành trong 6 bước,
        đáp án chứa kết quả đúng và model trả về đúng tag. Không đổi raw output để chấm lại.
    ''')
    add('code', 'session.live()')
    add('markdown', '''
        ## 7. Tải kết quả
        `core=PASS` là build + root tests; `live_agent=PASS` là demo model thật.
        Khi bỏ qua live, trạng thái live là `NOT_RUN`, không được coi là PASS.
        ZIP chứa manifest source, log compiler/build/CTest, báo cáo test và JSON history/events
        của live demo nếu đã chạy. Mở `report.json` để xem lỗi cụ thể.
    ''')
    add('code', '''
        from IPython.display import FileLink, display
        archive_path = session.archive()
        print(json.dumps(session.report, ensure_ascii=False, indent=2))
        display(FileLink(archive_path))
    ''')
    add('markdown', '''
        Tài liệu cài đặt: [micromamba](https://mamba.readthedocs.io/en/stable/installation/micromamba-installation.html),
        [Ollama Linux](https://docs.ollama.com/linux), [Ollama API](https://docs.ollama.com/api/chat).
        CLI cũ của TASK-09 vẫn dùng được: chạy `ai_agent --model TAG --text assets/chat_prompt.txt`.
    ''')
    notebook = {'nbformat': 4, 'nbformat_minor': 5,
                'metadata': {'kernelspec': {'display_name': 'Python 3', 'language': 'python', 'name': 'python3'},
                             'language_info': {'name': 'python', 'version': '3.11'},
                             'kaggle': {'isInternetEnabled': True, 'isGpuEnabled': True}}, 'cells': cells}
    destination = REPO / 'notebooks/TASK025_AgentLoop_Kaggle.ipynb'
    destination.parent.mkdir(exist_ok=True)
    destination.write_text(json.dumps(notebook, ensure_ascii=False, indent=1), encoding='utf-8')
    print(json.dumps({'notebook': str(destination), 'source_files': len(files),
                      'source_zip_bytes': len(raw), 'sha256': digest}, indent=2))


if __name__ == '__main__':
    main()
