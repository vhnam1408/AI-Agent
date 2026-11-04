"""Tiêu chí chấm live TASK-017. Chỉ đọc output, không sửa dữ liệu model."""
from collections import Counter
import json
import unicodedata

CRITERIA_VERSION = 'task017-retention-v3-newlines'
MODEL_FIELDS = ('kind', 'course', 'title', 'date_text', 'location', 'evidence')


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def normalized_text(value):
    """Chỉ chuẩn hóa Unicode/khoảng trắng, giữ dấu phòng và chữ/số nguyên vẹn."""
    if isinstance(value, str):
        return ' '.join(unicodedata.normalize('NFC', value).split())
    return value


def normalized_newlines(value):
    """Chỉ coi CRLF và LF tương đương; giữ nguyên chữ, dấu, khoảng trắng và nguồn gốc."""
    return value.replace('\r\n', '\n')


def read_raw_items(content):
    text = content.strip()
    for opening in ('```json\r\n', '```json\n', '```\r\n', '```\n'):
        if text.startswith(opening) and text.endswith('```'):
            text = text[len(opening):-3].strip()
            break
    return json.loads(text)['items']


def check_common_result(data):
    """Bắt hậu xử lý đổi đáp án và mất/bổ sung item so với raw response."""
    require(data.get('ok') is True, 'Extractor did not return a data result')
    raw_items = read_raw_items(data['raw_model_content'])
    require(len(raw_items) == len(data['items']), 'Items differ from raw model output')
    for raw, item in zip(raw_items, data['items']):
        for field in MODEL_FIELDS:
            require(raw[field] == item[field], f'Raw field was changed: {field}')
        require(item['source_id'] == data['source']['id'], 'Source id was lost')
    submissions = [item for item in data['items'] if item['kind'] == 'submission']
    require(data['tasks'] == submissions, 'Tasks must be exactly the submission items')
    expected_valid = all(item['validity'] == 'valid' for item in data['items'])
    require(data['all_items_valid'] == expected_valid, 'Wrong aggregate validity')


def check_text_evidence(data, source_text):
    """Một item không được mượn tên môn từ dòng khác mà thiếu bằng chứng."""
    source_text = normalized_newlines(source_text)
    require(normalized_newlines(data['source']['text']) == source_text,
            'Original source text was changed')
    require(data['source']['image_count'] == 0, 'Unexpected image input')
    for item in data['items']:
        evidence = item['evidence']
        require(evidence and normalized_newlines(evidence) in source_text,
                'Evidence is not an original quotation')
        for field in ('course', 'title', 'date_text', 'location'):
            value = item[field]
            if value is not None:
                require(value in evidence, f'{field} is absent from its own evidence')


def check_complete_text(data, source_text, expected_items):
    """Đúng đủ sự kiện, môn, tên việc, vị trí và thời điểm; không chỉ đếm tasks."""
    check_common_result(data)
    check_text_evidence(data, source_text)
    fields = ('kind', 'course', 'title', 'location', 'event_time_utc', 'deadline_utc')
    predicted = Counter(tuple(normalized_text(item[field]) for field in fields)
                        for item in data['items'])
    expected = Counter(tuple(normalized_text(item[field]) for field in fields)
                       for item in expected_items)
    require(predicted == expected, 'Controlled syllabus differs from predefined ground truth')
    require(data['all_items_valid'] and len(data['tasks']) == 2, 'Expected two valid hand-in tasks')
    require(data['reference_year'] is None, 'Year must come from the full source dates')
    for item in data['items']:
        require(not item['issues'] and not item['missing_fields'], 'Complete source lost fields')
        require(item['normalized']['year_origin'] == 'source', 'Wrong origin of year')
        require(item['normalized']['hour'] is not None, 'Printed source clock was lost')
        require(item['normalized']['minute'] is not None, 'Printed source minute was lost')
        target = next(row for row in expected_items
                      if row['kind'] == item['kind'] and row['course'] == item['course'])
        require(item['normalized']['year'] == 2026, 'Wrong full-source year')
        require(item['normalized']['local_date'] == target['local_date'], 'Wrong full-source date')
        require(item['normalized']['utc_offset_minutes'] == 420, 'Wrong Vietnam UTC offset')
        for field in ('hour', 'minute', 'second'):
            require(type(item['normalized'][field]) is int
                    and item['normalized'][field] == target[field], f'Wrong source {field}')


def index_sample_events(items):
    """Bốn sự kiện của input.txt có khóa kind/course riêng; bắt thiếu/thừa/trùng."""
    indexed = {}
    for item in items:
        key = (item['kind'], normalized_text(item['course']))
        require(key not in indexed, f'Duplicate sample event: {key}')
        indexed[key] = item
    return indexed


def check_repository_text(data, source_text, expected_items, reference_year=None):
    """Chấm input.txt, cả khi thiếu năm và khi context 2026 làm lộ sai thứ."""
    check_common_result(data)
    check_text_evidence(data, source_text)
    require(data['reference_year'] == reference_year, 'Reference year context was changed')
    require(len(data['tasks']) == 2, 'Expected two submission tasks, not exams as tasks')
    predicted = index_sample_events(data['items'])
    expected = index_sample_events(expected_items)
    require(predicted.keys() == expected.keys(), 'Missing, extra or misclassified sample event')

    for key, target in expected.items():
        item = predicted[key]
        date_time = item['normalized']
        require(target['title_fragment'] in item['title'], f'Work name lost: {key}')
        for field in ('month', 'day', 'hour', 'minute', 'second', 'weekday'):
            if target[field] is not None:
                require(type(date_time[field]) is int, f'Source {field} lost or has wrong type: {key}')
            require(date_time[field] == target[field], f'Source {field} lost or changed: {key}')
        require(item['location'] is not None and target['location_identifier'] in item['location'],
                f'Printed location was lost: {key}')
        require(item['deadline_utc'] is None and item['event_time_utc'] is None,
                'Unresolved or contradictory source became an approved timestamp')

        missing_fields = set()
        if target['hour'] is None:
            missing_fields.add('time')
        if reference_year is None:
            missing_fields.add('year')
        require(set(item['missing_fields']) == missing_fields, f'Wrong missing fields: {key}')
        issue_codes = {issue['code'] for issue in item['issues']}
        require('omitted_source_time' not in issue_codes, f'Model dropped a printed clock: {key}')

        if reference_year is None:
            require(date_time['year'] is None and date_time['local_date'] is None,
                    'Year inferred without explicit context')
            require(date_time['year_origin'] == 'unresolved', 'Wrong missing-year origin')
            require(item['validity'] == 'needs_review', 'Missing year must need review')
            require(issue_codes == {'missing_field'}, 'Unexpected source error')
        else:
            require(reference_year == 2026, 'This conflict fixture is defined for year 2026')
            require(date_time['year'] == 2026 and date_time['year_origin'] == 'explicit_context',
                    'Explicit context year lost')
            expected_date = f"2026-{target['month']:02d}-{target['day']:02d}"
            require(date_time['local_date'] == expected_date, 'Source calendar date was changed')
            require(item['validity'] == 'invalid' and 'weekday_mismatch' in issue_codes,
                    'Printed weekday conflict must be reported')
            allowed = {'weekday_mismatch', 'missing_field'}
            require(issue_codes <= allowed, 'Unexpected extraction error besides source conflict')


def vision_metrics(data, expected_items):
    predicted = Counter()
    for item in data['items']:
        key = (item['normalized']['weekday'], normalized_text(item['course']),
               normalized_text(item['location']))
        predicted[key] += 1
    expected = Counter((item['day'] - 1, normalized_text(item['subject']),
                        normalized_text(item['room'])) for item in expected_items)
    return {
        'matched': sum((predicted & expected).values()),
        'expected': sum(expected.values()),
        'predicted': sum(predicted.values()),
        'missing': [list(row) for row in (expected - predicted).elements()],
        'extra': [list(row) for row in (predicted - expected).elements()],
    }


def check_vision_timetable(data, expected_items):
    """Đúng thứ/môn/phòng 9 buổi; không tự sửa Phòng: hay dấu mã phòng để chấm."""
    check_common_result(data)
    require(data['source']['text'] == '' and data['source']['image_count'] == 1,
            'Expected the image-only source')
    require(data['reference_year'] is None, 'Timetable acquired an unrelated year context')
    require(not data['tasks'], 'Class timetable became submission tasks')
    require(len(data['items']) == len(expected_items), 'Expected all separate lessons')

    for item in data['items']:
        require(item['kind'] == 'class', 'A lesson was reclassified')
        require(item['location'] and not item['location'].startswith(('Phòng:', 'Phong:', 'phòng:')),
                'Location must contain the room identifier, without its label')
        require(item['date_text'] is None, 'Weekday-only timetable must not become a calendar date')
        require(item['deadline_utc'] is None and item['event_time_utc'] is None,
                'Timetable invented an absolute timestamp')
        for field in ('year', 'month', 'day', 'hour', 'minute', 'second', 'local_date'):
            require(item['normalized'][field] is None, f'Timetable invented {field}')
        require(item['normalized']['year_origin'] == 'unresolved', 'Timetable invented a year origin')
        require(type(item['normalized']['weekday']) is int
                and item['normalized']['weekday'] in range(1, 8), 'Weekday heading was lost')
        for field in ('course', 'title', 'location'):
            require(item[field] and item[field] in item['evidence'], f'Visual {field} lacks evidence')
        require(item['validity'] == 'needs_review', 'Visual evidence must remain unverified')
        issue_codes = {issue['code'] for issue in item['issues']}
        require(issue_codes == {'unverified_visual_evidence', 'missing_field'},
                'Unexpected invalid or ungrounded visual field')
        require(set(item['missing_fields']) == {'date', 'year', 'time'}, 'Wrong missing image fields')

    metrics = vision_metrics(data, expected_items)
    require(not metrics['missing'] and not metrics['extra'], 'Weekday/course/room transcription differs')
