"""Chạy trên Kaggle: tiêu chí phải nhận ví dụ đúng và từ chối lỗi đã gặp thật."""
from pathlib import Path
import json
import sys

from live_result_checks import (check_common_result, check_complete_text,
                                check_repository_text, check_vision_timetable)

repo, output = map(Path, sys.argv[1:3])
fixtures = repo / 'tests/fixtures/deadline_extractor/regression'
sample_text = (repo / 'assets/input.txt').read_bytes().decode('utf-8')
complete_text = (repo / 'tests/fixtures/deadline_extractor/live_syllabus.txt').read_bytes().decode('utf-8')
sample_expected = json.loads((repo / 'tests/fixtures/deadline_extractor/input_expected.json').read_text(encoding='utf-8'))['items']
complete_expected = json.loads((repo / 'tests/fixtures/deadline_extractor/live_expected.json').read_text(encoding='utf-8'))['items']
vision_expected = json.loads((repo / 'assets/task013/timetable_expected.json').read_text(encoding='utf-8'))
cases = []


def load(name):
    return json.loads((fixtures / name).read_text(encoding='utf-8'))


def check_case(name, body):
    try:
        body()
        cases.append({'name': name, 'status': 'PASS'})
    except Exception as error:
        cases.append({'name': name, 'status': 'FAIL', 'reason': str(error)})


def must_reject(body, reason):
    try:
        body()
    except AssertionError as error:
        if reason not in str(error):
            raise AssertionError(f'Rejected for unexpected reason: {error}') from error
        return
    raise AssertionError('Validator accepted a known failure')


check_case('accept_synthetic_complete_text', lambda: check_complete_text(
    load('synthetic_complete.json'), complete_text, complete_expected))
check_case('accept_captured_missing_year_text', lambda: check_repository_text(
    load('captured_text_missing_year.json'), sample_text, sample_expected))
check_case('accept_synthetic_conflict_with_clocks_retained', lambda: check_repository_text(
    load('synthetic_weekday_conflict.json'), sample_text, sample_expected, reference_year=2026))
check_case('accept_synthetic_vision_without_deadlines', lambda: check_vision_timetable(
    load('synthetic_vision.json'), vision_expected))


# Hai kiểu xuống dòng phải cho cùng kết quả; không sửa bất kỳ fixture lịch sử nào.
for label, newline in (('lf', '\n'), ('crlf', '\r\n')):
    check_case('accept_source_newlines_' + label, lambda newline=newline: check_repository_text(
        load('captured_text_missing_year.json'),
        sample_text.replace('\r\n', '\n').replace('\n', newline), sample_expected))


def reject_changed_source(suffix):
    """Không nới tiêu chí cho thay chữ hoặc thêm khoảng trắng ngoài CRLF/LF."""
    data = load('captured_text_missing_year.json')
    data['source']['text'] += suffix
    must_reject(lambda: check_repository_text(data, sample_text, sample_expected),
                'Original source text was changed')


check_case('reject_changed_source_content', lambda: reject_changed_source('X'))
check_case('reject_changed_source_whitespace', lambda: reject_changed_source(' '))

check_case('reject_captured_incomplete_evidence', lambda: must_reject(
    lambda: check_complete_text(load('captured_text_complete.json'), complete_text, complete_expected),
    'course is absent'))
check_case('reject_captured_weekday_pass_that_lost_clocks', lambda: must_reject(
    lambda: check_repository_text(load('captured_text_weekday_conflict.json'), sample_text,
                                  sample_expected, reference_year=2026),
    'Source hour lost'))
check_case('reject_captured_room_labels', lambda: must_reject(
    lambda: check_vision_timetable(load('captured_vision_timetable.json'), vision_expected),
    'room identifier'))
check_case('reject_captured_lms_omission_missing_year', lambda: must_reject(
    lambda: check_repository_text(load('captured_lms_missing_year.json'), sample_text,
                                  sample_expected),
    'Printed location was lost'))
check_case('reject_captured_lms_omission_weekday_conflict', lambda: must_reject(
    lambda: check_repository_text(load('captured_lms_weekday_conflict.json'), sample_text,
                                  sample_expected, reference_year=2026),
    'Printed location was lost'))


def reject_changed_room_after_model_response():
    # Cố tình sửa bản sao trong test để chứng minh không thể hậu xử lý ép PASS.
    data = load('captured_vision_timetable.json')
    data['items'][0]['location'] = 'F203 (CS2)'
    must_reject(lambda: check_common_result(data), 'Raw field was changed')


check_case('reject_postprocessed_room', reject_changed_room_after_model_response)
summary = {'passed': sum(case['status'] == 'PASS' for case in cases),
           'failed': sum(case['status'] == 'FAIL' for case in cases), 'cases': cases}
output.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(summary, ensure_ascii=False, indent=2))
sys.exit(1 if summary['failed'] else 0)
