"""Opt-in Ollama integration. Native raw JSON MUST pass through the C++ parser.

Python is only the native-tools request driver until the team approves a shared
LLMResponse contract; this does not demonstrate native transport via OllamaClient.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import unicodedata
import urllib.request

probe, repo, output = map(Path, sys.argv[1:4])
if os.environ.get("TASK13_LIVE") != "1":
    print("SKIP: set TASK13_LIVE=1 and TASK13_MODEL after starting Ollama")
    sys.exit(77)
model = os.environ.get("TASK13_MODEL")
if not model:
    print("FAIL: TASK13_MODEL is required for an explicitly enabled live test")
    sys.exit(1)
url = os.environ.get("TASK13_URL", "http://127.0.0.1:11434").rstrip("/")
output.mkdir(parents=True, exist_ok=True)
report = {"model_requested": model, "url": url, "tests": {},
          "native_client_integration": "PENDING_SHARED_CONTRACT"}


def request(name, messages, tools=None):
    payload = {"model": model, "stream": False, "messages": messages,
               "options": {"temperature": 0, "num_predict": 2048}}
    if tools is not None:
        payload["tools"] = tools
    req = urllib.request.Request(url + "/api/chat", json.dumps(payload).encode(),
                                 {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=300) as response:
        raw = response.read()
    path = output / (name + "_raw.json")
    path.write_bytes(raw)
    parsed = subprocess.check_output([str(probe), "parse", str(path)], timeout=30)
    (output / (name + "_cpp.json")).write_bytes(parsed)
    return json.loads(raw), json.loads(parsed)


def normalize(value):
    return " ".join(unicodedata.normalize("NFC", value).split())


def rows(data):
    if not isinstance(data, list):
        raise AssertionError("Timetable must be a list")
    result = []
    for row in data:
        assert set(row) == {"day", "kind", "subject", "room", "start", "end"}, row
        assert type(row["day"]) is int and 2 <= row["day"] <= 8, row
        assert row["kind"] in ("LT", "TH"), row
        assert row["start"] is None and row["end"] is None, "Image has no hour labels"
        result.append((row["day"], row["kind"], normalize(row["subject"]), normalize(row["room"])))
    return result


def test_text():
    _, parsed = request("text", [{"role": "user", "content": "Reply with only the number: 15 + 17"}])
    assert parsed["content"].strip() == "32" and not parsed["calls"], parsed


def test_image():
    result = subprocess.run([str(probe), "image", str(repo / "assets/timetable.png"), url, model],
                             capture_output=True, timeout=360)
    (output / "image_cpp.json").write_bytes(result.stdout)
    (output / "image_stderr.txt").write_bytes(result.stderr)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    content = json.loads(result.stdout)["content"].strip()
    if content.startswith("```json\n") and content.endswith("```"):
        content = content[8:-3].strip()
    predicted = rows(json.loads(content))
    truth = rows(json.loads((repo / "assets/timetable_expected.json").read_text(encoding="utf-8")))
    # Multiset comparison catches omissions, hallucinations AND duplicated classes.
    from collections import Counter
    hits = sum((Counter(predicted) & Counter(truth)).values())
    report["vision_metrics"] = {"matched": hits, "expected": len(truth), "predicted": len(predicted),
        "precision": hits / len(predicted) if predicted else 0, "recall": hits / len(truth)}
    assert Counter(predicted) == Counter(truth), "Timetable differs; inspect image_cpp.json"


def test_tools():
    tools = [{"type": "function", "function": {"name": "task_lookup",
        "description": "Look up a task by its ID", "parameters": {"type": "object",
        "properties": {"task_id": {"type": "string"}}, "required": ["task_id"],
        "additionalProperties": False}}}]
    raw, parsed = request("native_tools", [{"role": "user", "content":
        "Call task_lookup with task_id T1. Do not answer from memory."}], tools)
    assert raw["message"].get("tool_calls"), "No native tool_calls returned"
    assert parsed["calls"] == [{"tool_name": "task_lookup", "arguments": {"task_id": "T1"}}], parsed


for name, test in [("text", test_text), ("image", test_image), ("native_tools_cpp_parser", test_tools)]:
    try:
        test()
        report["tests"][name] = {"status": "PASS"}
    except Exception as exc:
        report["tests"][name] = {"status": "FAIL", "reason": str(exc)}
    print(name, report["tests"][name])
(output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
sys.exit(0 if all(t["status"] == "PASS" for t in report["tests"].values()) else 1)
