"""Track new local prompts and evidence; never infer task success."""
import math
import time
import unicodedata

MAPPING = {'UserPromptSubmit': 'WORKING', 'PreToolUse': 'WORKING',
           'PermissionRequest': 'WAITING', 'PostToolUse': 'WORKING',
           'Stop': 'DONE', 'Interrupt': 'IDLE', 'SessionEnd': 'IDLE'}

def screen_project(name):
    text = unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode()
    text = ''.join(c for c in text if 32 <= ord(c) <= 126).strip()
    return (text or 'local-project')[:16]

class State:
    def __init__(self, session=None, stale_seconds=600, clock=time.monotonic):
        self.fixed_session = session
        self.session = session
        self.sessions = {}
        self.stale_seconds = stale_seconds
        self.clock = clock
        self.attention_seq = 0
        self.acknowledged = 0

    def apply(self, entry):
        event, session = entry.get('event'), entry.get('session_id')
        turn = entry.get('turn_id') or ''
        timestamp = entry.get('ts')
        if (event not in MAPPING or not isinstance(session, str) or not session or
                not isinstance(turn, str) or not isinstance(timestamp, (int, float)) or
                not math.isfinite(timestamp)):
            return False
        now = self.clock()
        record = self.sessions.get(session)
        if event == 'UserPromptSubmit':
            record = {'turn': turn, 'status': 'WORKING', 'started': now,
                      'finished': None, 'project': '', 'ended': False, 'alert': 0}
            self.sessions[session] = record
            if not self.fixed_session or session == self.fixed_session:
                self.session = session
        elif record is None or (event != 'SessionEnd' and turn and
                                 record['turn'] and turn != record['turn']):
            return False
        elif record['ended']:
            return False
        project = entry.get('project')
        if isinstance(project, str) and project:
            record['project'] = project[:160]
        if event == 'PermissionRequest' and record['status'] != 'WAITING':
            self.attention_seq += 1
            record['alert'] = self.attention_seq
            record['waiting_since'] = now
        record.update(status=MAPPING[event], updated=now, event=event)
        if event in ('Stop', 'Interrupt', 'SessionEnd'):
            record['finished'] = now
        else:
            record['finished'] = None
        if event == 'SessionEnd':
            record['ended'] = True
        return True

    def status_of(self, record, now):
        if record['status'] in ('WORKING', 'WAITING') and (
                now - record['updated'] > self.stale_seconds):
            return 'UNKNOWN'
        return record['status']

    def wire_status(self):
        record = self.sessions.get(self.session)
        return self.status_of(record, self.clock()) if record else 'UNKNOWN'

    def snapshot(self):
        now = self.clock()
        record = self.sessions.get(self.session)
        statuses = [self.status_of(item, now) for item in self.sessions.values()
                    if not item['ended']]
        elapsed = 0
        if record:
            end = record['finished']
            if end is None:
                end = min(now, record['updated'] + self.stale_seconds)
            elapsed = max(0, int(end - record['started']))
        project = record['project'] if record else ''
        return {'status': self.status_of(record, now) if record else 'UNKNOWN',
                'project': project, 'elapsed': min(elapsed, 99999999),
                'running': min(statuses.count('WORKING'), 999),
                'waiting': min(statuses.count('WAITING'), 999),
                'seen': min(len(self.sessions), 999)}

    def packets(self):
        data = self.snapshot()
        return [('APP:' + data['status']).encode('ascii'),
                ('M:P:' + (screen_project(data['project']) if data['project'] else '-')).encode('ascii'),
                ('M:C:{running},{waiting},{seen}'.format(**data)).encode('ascii'),
                ('M:T:' + str(data['elapsed'])).encode('ascii')]

    def attention_packets(self):
        now = self.clock()
        waiting = [item for item in self.sessions.values() if not item['ended'] and
                   self.status_of(item, now) == 'WAITING']
        waiting.sort(key=lambda item: (item['alert'] <= self.acknowledged,
                                       item.get('waiting_since', now)))
        item = waiting[0] if waiting else None
        unseen = [item['alert'] for item in waiting if item['alert'] > self.acknowledged]
        project = screen_project(item['project']) if item and item['project'] else '-'
        return [('M:A:' + project).encode('ascii'),
                ('M:N:' + str(max(unseen, default=0) % 100000000)).encode('ascii')]

    def acknowledge(self):
        self.acknowledged = self.attention_seq
