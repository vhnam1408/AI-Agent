"""Tạo notebook Kaggle tự chứa source; chỉ đóng gói/parse, không chạy test local."""
import ast
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import textwrap
from zipfile import ZipFile


OLLAMA_SETUP = r'''
if not RUN_LIVE:
    print("Bỏ qua cài Ollama/model vì RUN_LIVE=False; live giữ NOT RUN.")
else:
    try:
        gpu = run_logged("gpu_inventory", ["nvidia-smi", "--query-gpu=index,name,memory.total,memory.free",
                                          "--format=csv,noheader,nounits"], check=False)
        if gpu.returncode or not gpu.stdout.strip():
            raise RuntimeError("Chưa có GPU NVIDIA: chọn GPU trong Kaggle Settings rồi chạy lại.")
        report["gpu_inventory"] = gpu.stdout.strip().splitlines()
        report["statuses"]["gpu"] = "PASS"
        print(gpu.stdout)
        if not shutil.which("ollama"):
            run_logged("apt_update", ["apt-get", "update"], timeout=600)
            run_logged("ollama_dependencies", ["apt-get", "install", "-y", "curl", "ca-certificates", "zstd", "pciutils"], timeout=600)
            installer = WORK / "install_ollama.sh"
            urllib.request.urlretrieve("https://ollama.com/install.sh", installer)
            report["ollama_installer_sha256"] = hashlib.sha256(installer.read_bytes()).hexdigest()
            run_logged("ollama_install", ["sh", installer], timeout=1200)
        ollama_env = os.environ.copy()
        ollama_env.update({"OLLAMA_HOST": "127.0.0.1:11434",
                           "OLLAMA_MODELS": "/kaggle/working/task021-ollama-models",
                           "OLLAMA_CONTEXT_LENGTH": str(CONTEXT_LENGTH),
                           "OLLAMA_NUM_PARALLEL": "1", "OLLAMA_FLASH_ATTENTION": "1",
                           "OLLAMA_KV_CACHE_TYPE": "q8_0", "OLLAMA_NO_CLOUD": "1"})
        try:
            version = api_json("/api/version", timeout=5)
            report["ollama_server"] = "reused existing server; its original environment is retained"
        except Exception:
            server_log = open(EVIDENCE / "ollama_server.log", "a", encoding="utf-8")
            ollama_process = subprocess.Popen(["ollama", "serve"], env=ollama_env,
                                              stdout=server_log, stderr=subprocess.STDOUT)
            report["ollama_server"] = "started by this notebook"
            for attempt in range(120):
                if ollama_process.poll() is not None:
                    raise RuntimeError("Ollama dừng khi khởi động; xem ollama_server.log.")
                try:
                    version = api_json("/api/version", timeout=2)
                    break
                except Exception:
                    time.sleep(1)
            else:
                raise RuntimeError("Ollama chưa sẵn sàng sau 120 giây.")
        report["ollama_version"] = version
        run_logged("ollama_cli_version", ["ollama", "--version"], env=ollama_env)
        tags = api_json("/api/tags", timeout=30)
        selected = next((item for item in tags.get("models", [])
                         if item.get("name") == LIVE_MODEL or item.get("model") == LIVE_MODEL), None)
        if selected is None:
            run_logged("ollama_pull", ["ollama", "pull", LIVE_MODEL], env=ollama_env, timeout=3600)
        info = api_json("/api/show", {"model": LIVE_MODEL}, timeout=30)
        (EVIDENCE / "model_info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding="utf-8")
        if not {"completion", "vision", "tools"}.issubset(set(info.get("capabilities", []))):
            raise RuntimeError("Model chưa báo đủ completion/vision/tools; không tự đổi model.")
        tags = api_json("/api/tags", timeout=30)
        selected = next((item for item in tags.get("models", [])
                         if item.get("name") == LIVE_MODEL or item.get("model") == LIVE_MODEL), None)
        if not selected or not selected.get("digest"):
            raise RuntimeError("Không xác định được digest của đúng model yêu cầu.")
        report["model"] = LIVE_MODEL
        report["model_digest"] = selected["digest"]
        report["model_size_bytes"] = selected.get("size")
        report["model_capabilities"] = info.get("capabilities", [])
        print("Warm-up model:", LIVE_MODEL, flush=True)
        warmup = api_json("/api/chat", {"model": LIVE_MODEL, "stream": False, "think": False,
                            "messages": [{"role": "user", "content": "Chỉ trả lời OK."}],
                            "options": {"num_ctx": CONTEXT_LENGTH, "num_predict": 16},
                            "keep_alive": "30m"}, timeout=900)
        (EVIDENCE / "warmup.json").write_text(json.dumps(warmup, ensure_ascii=False, indent=2), encoding="utf-8")
        if not warmup.get("message", {}).get("content", "").strip():
            raise RuntimeError("Warm-up chưa trả nội dung; xem warmup.json.")
        process_info = api_json("/api/ps", timeout=30)
        (EVIDENCE / "ollama_ps.json").write_text(json.dumps(process_info, indent=2), encoding="utf-8")
        loaded = next((item for item in process_info.get("models", [])
                       if item.get("name") == LIVE_MODEL or item.get("model") == LIVE_MODEL), None)
        if not loaded or loaded.get("size_vram", 0) <= 0:
            raise RuntimeError("Chưa xác minh model dùng GPU qua /api/ps; xem ollama_ps.json.")
        report["model_vram_bytes"] = loaded.get("size_vram")
        report["statuses"]["ollama"] = "PASS"
        print("Ollama ready:", LIVE_MODEL, "VRAM bytes:", loaded.get("size_vram"))
    except Exception as error:
        if report["statuses"]["gpu"] != "PASS":
            report["statuses"]["gpu"] = "BLOCKED"
        report["statuses"]["ollama"] = "BLOCKED"
        report["ollama_error"] = str(error)
        print("Ollama BLOCKED:", error)
    finally:
        archive()
'''

NGROK = r'''
if ENABLE_NGROK:
    try:
        if report["statuses"]["ollama"] != "PASS":
            raise RuntimeError("Ollama chưa sẵn sàng để mở tunnel.")
        run_logged("pyngrok_install", [sys.executable, "-m", "pip", "install", "-q", "pyngrok"], timeout=600)
        from kaggle_secrets import UserSecretsClient
        from pyngrok import ngrok
        ngrok.set_auth_token(UserSecretsClient().get_secret("NGROK_AUTHTOKEN"))
        tunnel = ngrok.connect(addr="127.0.0.1:11434", proto="http", bind_tls=True,
                               host_header="localhost:11434")
        print("Endpoint cho máy cá nhân:", tunnel.public_url)
        report["statuses"]["ngrok"] = "PASS"
    except Exception as error:
        report["statuses"]["ngrok"] = "BLOCKED"
        print("Ngrok chưa sẵn sàng; kiểm Kaggle Secret NGROK_AUTHTOKEN và log.")
    finally:
        archive()
else:
    print("Ngrok tắt; probe C++ gọi http://127.0.0.1:11434 ngay trong Kaggle.")
'''

SUMMARY = r'''
archive()
rows = [{"Nhóm": name, "Trạng thái": state} for name, state in report["statuses"].items()]
try:
    import pandas as pd
    display(pd.DataFrame(rows))
except ImportError:
    print(json.dumps(rows, ensure_ascii=False, indent=2))
print("Core TASK-021:", report["core_status"])
print("Overall:", report["overall_status"])
for label in ("error", "root_error", "ollama_error", "live_error"):
    if report.get(label):
        print(label + ":", report[label])
for relative in ("task21_unit_results.json", "unit_results.json", "http_results.json", "criteria_results.json"):
    artifact = BUILD / relative
    if artifact.is_file():
        data = json.loads(artifact.read_text(encoding="utf-8"))
        print(relative, "PASS:", data.get("passed"), "FAIL:", data.get("failed"))
print("Evidence ZIP:", ZIP)
from IPython.display import FileLink, display
display(FileLink(str(ZIP)))
print("Tải ZIP trong /kaggle/working về để review. ZIP không chứa model weights hoặc secret.")
'''


def cell(kind, source, index):
    """Tạo cell nbformat 4 sạch output; chỉ parse cú pháp code, không thực thi."""
    source = textwrap.dedent(source).strip() + '\n'
    if kind == 'code':
        ast.parse(source, filename=f'standalone:cell{index}')
    value = {'cell_type': kind, 'metadata': {}, 'id': f'task021-standalone-{index:02d}', 'source': source}
    if kind == 'code':
        value.update(execution_count=None, outputs=[])
    return value


def main():
    """Nhúng snapshot được hash vào notebook mới; không cần upload ZIP hoặc GitHub khi chạy."""
    root = Path(__file__).resolve().parents[2]
    folder = root / 'tests/deadline_extractor'
    base = json.loads((folder / 'AI_Agent_TASK021_Kaggle.ipynb').read_text(encoding='utf-8'))
    result = subprocess.check_output([sys.executable, str(folder / 'package_task021_source.py')], text=True)
    bundle = Path(json.loads(result)['bundle'])
    raw = bundle.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    with ZipFile(bundle) as archive:
        snapshot = json.loads(archive.read('task021-source-manifest.json'))
    encoded = base64.b64encode(raw).decode('ascii')
    literals = '\n'.join('    ' + repr(encoded[index:index + 100]) for index in range(0, len(encoded), 100))

    setup = base['cells'][2]['source']
    start, end = setup.index('if SOURCE_MODE not in'), setup.index('WORK = Path')
    setup = setup[:start] + setup[end:]
    setup = 'import base64\n' + setup
    start, end = setup.index('report = '), setup.index('          "statuses":')
    setup = setup[:start] + 'report = {"source_mode": "embedded", "requested_model": LIVE_MODEL, "commands": [],\n' + setup[end:]
    setup = setup.replace('"root_regression", "live")', '"root_regression", "gpu", "ollama", "live", "ngrok")')
    setup = setup.replace('    if RUN_LIVE:\n        required += ("live",)',
        '    core_states = [report["statuses"][name] for name in required]\n'
        '    report["core_status"] = ("FAIL" if "FAIL" in core_states else "BLOCKED" if "BLOCKED" in core_states\n'
        '                             else "PASS" if all(state == "PASS" for state in core_states) else "NOT RUN")\n'
        '    if RUN_LIVE:\n        required += ("gpu", "ollama", "live")\n'
        '    if ENABLE_NGROK:\n        required += ("ngrok",)')
    setup = setup.replace('def run_logged(name, command, cwd=None, check=True, timeout=1200):',
                          'def run_logged(name, command, cwd=None, check=True, timeout=1200, env=None):')
    setup = setup.replace('    try:\n        result = subprocess.run',
                          '    print("[RUN]", name, flush=True)\n    try:\n        result = subprocess.run')
    setup = setup.replace('capture_output=True, timeout=timeout)', 'capture_output=True, timeout=timeout, env=env)')
    setup = setup.replace('    if check and result.returncode:',
        '    print("[END]", name, "exit=", result.returncode, flush=True)\n'
        '    if result.returncode:\n        print(result.stderr[-2000:] or result.stdout[-2000:])\n'
        '    if check and result.returncode:')
    setup += '''

def api_json(endpoint, payload=None, timeout=30):
    # Chỉ dùng cho setup/metadata; extraction thực tế vẫn qua OllamaClient C++.
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(OLLAMA_URL + endpoint, data=body,
                                     headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.load(response)
'''

    loader = 'def copy_uploaded_source' + base['cells'][5]['source'].split('def copy_uploaded_source', 1)[1].split('\n\ntry:', 1)[0]
    embedded = loader + '\n\nEMBEDDED_SHA256 = ' + repr(digest) + '\nSOURCE_BASE64 = (\n' + literals + '\n)\n'
    embedded += '''
try:
    source_bytes = base64.b64decode(SOURCE_BASE64, validate=True)
    if hashlib.sha256(source_bytes).hexdigest() != EMBEDDED_SHA256:
        raise ValueError("Payload source trong notebook bị thay đổi hoặc bị cắt; import lại file gốc.")
    source_zip = WORK / "embedded-source.zip"
    source_zip.write_bytes(source_bytes)
    manifest = copy_uploaded_source(source_zip)
    (EVIDENCE / "source_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    for name in ("src/agent/deadline_confirmation.cpp", "include/agent/deadline_confirmation.h",
                 "tests/deadline_extractor/deadline_confirmation_test.cpp"):
        if name not in manifest:
            raise ValueError("Snapshot thiếu " + name)
    if "add_test(NAME task21_unit " not in (REPO / "tests/deadline_extractor/CMakeLists.txt").read_text(encoding="utf-8"):
        raise ValueError("Snapshot chưa đăng ký task21_unit")
    report["statuses"]["source"] = "PASS"
    print("Source ready:", len(manifest), "files; source_id:", report["source_id"])
except Exception as error:
    report["statuses"]["source"] = "BLOCKED"
    report["error"] = str(error)
    print("Source BLOCKED:", error)
finally:
    archive()
'''
    toolchain = base['cells'][4]['source'].replace('    raise\nfinally:', '    print("Toolchain BLOCKED:", error)\nfinally:')
    build = base['cells'][7]['source'].replace('    raise\nfinally:', '    print("TASK-021 build/test FAIL:", error)\nfinally:')
    build = 'BUILD = REPO / "build/task21"\nif report["statuses"]["source"] != "PASS" or report["statuses"]["toolchain"] != "PASS":\n    report["statuses"]["build"] = "BLOCKED"\n    print("Build BLOCKED: cần source và toolchain PASS.")\n    archive()\nelse:\n' + textwrap.indent(build, '    ')
    regression = base['cells'][8]['source'].replace('if RUN_ROOT_REGRESSION:',
        'if RUN_ROOT_REGRESSION and report["statuses"]["source"] == "PASS" and report["statuses"]["toolchain"] == "PASS":', 1)
    live = base['cells'][10]['source'].split('\narchive()\nprint(json.dumps', 1)[0]
    live = live.replace('    try:\n        if not LIVE_MODEL:',
        '    try:\n        if report["statuses"]["ollama"] != "PASS" or report["statuses"]["build"] != "PASS":\n'
        '            raise RuntimeError("Live cần Ollama và build PASS; xem nhóm bị lỗi ở trên.")\n'
        '        if not LIVE_MODEL:', 1)
    cells = []

    def add(kind, source):
        """Giữ thứ tự cell và định danh ổn định trong một lần đóng gói."""
        cells.append(cell(kind, source, len(cells)))

    add('markdown', '''# TASK-021 — Kaggle / Qwen3.8:27B / source nhúng sẵn

**Import một file này rồi Run All. Không cần GitHub, EXPECTED_COMMIT hoặc ZIP/Dataset source.**

Trước khi chạy: bật Internet và chọn GPU (ưu tiên T4 x2 nếu được cấp).
Mặc định chạy unit + HTTP mock + regression + live text/ảnh với `qwen3.8:27b`.
Nếu muốn test C++ trước, đặt `RUN_LIVE=False`; phần đó không cần GPU hoặc model.
Ngrok là tùy chọn khi cần gọi Ollama từ máy cá nhân, mặc định tắt.

**NOT RUN — pending Kaggle validation.** Không có kết quả thực thi sẵn trong file.
Notebook chứa snapshot source hiện tại; sau khi sửa source cần tạo lại notebook.

Tham khảo yêu cầu của bạn: [tdquang / qwen-3-8-27b-ngrok-ollama](https://www.kaggle.com/code/tdquang/qwen-3-8-27b-ngrok-ollama).
Trang này chưa truy xuất được bằng công cụ lúc soạn, nên đây là notebook mới theo luồng
Ollama/Qwen, không phải bản sao đã kiểm chứng từng cell của notebook tham khảo.
Đối chiếu [Ollama Linux](https://docs.ollama.com/linux), [FAQ/GPU/ngrok](https://docs.ollama.com/faq),
[đúng model qwen3.8:27b](https://ollama.com/library/qwen3.8:27b).
''')
    add('code', '''RUN_LIVE = True
LIVE_MODEL = "qwen3.8:27b"
CONTEXT_LENGTH = 16384
INSTALL_TOOLCHAIN = True
TOOLCHAIN = "/kaggle/working/task021-toolchain"
RUN_ROOT_REGRESSION = True
ENABLE_NGROK = False
OLLAMA_URL = "http://127.0.0.1:11434"
''')
    add('markdown', '## 1. Khởi tạo và lưu evidence\nChỉ thực thi trên Kaggle. Mỗi run dùng thư mục mới, giữ log và trạng thái riêng.')
    add('code', setup)
    add('markdown', '## 2. Khôi phục source nhúng sẵn\nKiểm hash ZIP và từng file trước khi build. Base commit chỉ để tham chiếu vì source chưa commit.')
    add('code', embedded)
    add('markdown', '## 3. Toolchain C++23\nCài compiler GCC >=15, libcurl và SQLite trên Kaggle. Probe cả `std::expected` và `<print>` trước build.')
    add('code', toolchain)
    add('markdown', '## 4. Build và test TASK-017/TASK-021\nGồm `task17_unit`, `task21_unit`, HTTP mock và bộ kiểm tiêu chí bằng fixture. Fixture test không phải live model.')
    add('code', build)
    add('markdown', '## 5. Regression toàn dự án\nDùng CMake gốc; lỗi được báo riêng và không tự sửa source của đồng đội.')
    add('code', regression)
    add('markdown', '''## 6. GPU → Ollama → tải và warm-up model

Model `qwen3.8:27b` tải khoảng 18 GB theo thư viện Ollama; thời gian và khả năng chạy
phụ thuộc tài nguyên thực tế được Kaggle cấp. Notebook ghi VRAM từng GPU, digest,
capabilities và bộ nhớ model dùng; không tự đổi model để vượt lỗi.
Nếu server được tái sử dụng thì cấu hình môi trường cũ của server vẫn giữ nguyên.
Đợi cell tải model hoàn tất; log được giữ trong evidence.
''')
    add('code', OLLAMA_SETUP)
    add('markdown', '## 7. Live text và ảnh qua Client C++\nProbe hiện có gọi OllamaClient; giữ JSON và raw response, không sửa đầu ra model để chấm PASS.\nUnit TASK-021 kiểm luồng xác nhận deterministic; live này kiểm extraction text/ảnh TASK-017.')
    add('code', live)
    add('markdown', '## 8. Ngrok tùy chọn\nChỉ cần khi gọi server từ máy cá nhân. Nếu bật `ENABLE_NGROK=True`, thêm Kaggle Secret tên `NGROK_AUTHTOKEN` và cấp quyền cho notebook. Không dán token vào cell.')
    add('code', NGROK)
    add('markdown', '## 9. Kết quả và tải ZIP\n`Core TASK-021` gồm build/unit/HTTP/fixture/root regression. `Overall` còn yêu cầu live khi RUN_LIVE=True; model chưa chạy không được suy thành PASS.')
    add('code', SUMMARY)
    notebook = {'cells': cells, 'metadata': base['metadata'], 'nbformat': 4, 'nbformat_minor': 5}
    destination = folder / 'AI_Agent_TASK021_Standalone_Kaggle.ipynb'
    destination.write_text(json.dumps(notebook, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    report = {'notebook': str(destination), 'bytes': destination.stat().st_size,
              'source_bundle': str(bundle), 'embedded_zip_sha256': digest,
              'source_id': snapshot['source_id'], 'source_files': len(snapshot['files']),
              'cells': len(cells), 'code_cells_ast_checked': sum(item['cell_type'] == 'code' for item in cells),
              'outputs_empty': True, 'execution_status': 'NOT RUN - pending Kaggle validation'}
    (root / 'output/TASK-021-standalone-static-review.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
