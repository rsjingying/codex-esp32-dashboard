"""Record lifecycle metadata only. Never return an approval or block decision."""
import fcntl
import json
from pathlib import Path
import sys
import time

EVENTS = {'UserPromptSubmit', 'PermissionRequest', 'PreToolUse', 'PostToolUse',
          'Stop', 'Interrupt', 'SessionEnd'}

def record(payload):
    event = payload.get('hook_event_name')
    session = payload.get('session_id')
    if event not in EVENTS or not isinstance(session, str) or not session:
        return
    entry = {'event': event, 'session_id': session[:160],
             'turn_id': str(payload.get('turn_id') or '')[:160], 'ts': time.time()}
    cwd = payload.get('cwd')
    if isinstance(cwd, str) and cwd:
        entry['project'] = Path(cwd).name[:160]
    path = Path(__file__).resolve().parent / 'events.jsonl'
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a', encoding='utf-8') as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        stream.write(json.dumps(entry, separators=(',', ':')) + '\n')
        stream.flush()
        fcntl.flock(stream, fcntl.LOCK_UN)

if __name__ == '__main__':
    try:
        payload = json.load(sys.stdin)
        if isinstance(payload, dict):
            record(payload)
    except Exception:
        pass
    print('{}')
