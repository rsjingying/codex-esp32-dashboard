"""Personal activity intervals, kept locally in SQLite; no task text is stored."""
from datetime import datetime, timedelta
import sqlite3
import time
from pathlib import Path

MODES = {'WORK': 1, 'STUDY': 2, 'DEV': 3, 'REST': 4}

class Activity:
    def __init__(self, path, clock=time.time):
        self.clock = clock
        self.path = Path(path)
        self.db = sqlite3.connect(str(path))
        self.db.execute('CREATE TABLE IF NOT EXISTS segments '
                        '(id INTEGER PRIMARY KEY, start REAL, end REAL, mode TEXT, deep INTEGER)')
        self.db.commit()
        self.mode, self.deep, self.running = 'WORK', False, False
        self.row, self.started, self.saved = None, 0, 0
        self.elapsed_base = 0
        self.stopped = True

    def stop(self):
        if self.row is not None:
            self.elapsed_base += max(0, self.clock() - self.started)
            self.checkpoint(force=True)
        self.row = None
        self.running = False

    def start(self):
        self.started = self.clock()
        self.saved = self.started
        cursor = self.db.execute('INSERT INTO segments(start,end,mode,deep) VALUES(?,?,?,?)',
                                 (self.started, self.started, self.mode, int(self.deep)))
        self.row = cursor.lastrowid
        self.db.commit()
        self.running = True
        self.stopped = False

    def command(self, command):
        if command == 'FOCUS':
            self.command('DEEP1')
            self.command('RESUME')
        elif command in MODES:
            if self.running and self.mode == command:
                return True
            self.stop()
            if self.mode != command or self.stopped:
                self.elapsed_base = 0
            self.mode = command
            self.start()
        elif command in ('DEEP1', 'DEEP0'):
            target = command == 'DEEP1'
            if target == self.deep:
                return True
            running = self.running
            self.stop()
            self.deep = target
            if running:
                self.start()
        elif command == 'PAUSE':
            self.stop()
            self.stopped = False
        elif command == 'STOP':
            self.stop()
            self.stopped = True
        elif command == 'RESUME':
            if not self.running:
                if self.stopped:
                    self.elapsed_base = 0
                self.start()
        else:
            return False
        return True

    def checkpoint(self, force=False):
        now = self.clock()
        if self.row is not None and (force or now - self.saved >= 15):
            # Backward clock adjustments never create negative intervals.
            self.db.execute('UPDATE segments SET end=? WHERE id=?',
                            (max(self.started, now), self.row))
            self.db.commit()
            self.saved = now

    def snapshot(self):
        now = self.clock()
        today = datetime.fromtimestamp(now).replace(hour=0, minute=0, second=0, microsecond=0)
        lower, upper = today.timestamp(), (today + timedelta(days=1)).timestamp()
        totals = {name: 0.0 for name in MODES}
        deep_seconds = 0.0
        for ident, start, end, mode, deep in self.db.execute(
                'SELECT id,start,end,mode,deep FROM segments WHERE start<? AND (end>? OR id=?)',
                (upper, lower, self.row or -1)):
            if ident == self.row:
                end = max(start, now)
            length = max(0, min(end, upper) - max(start, lower))
            totals[mode] += length
            if deep:
                deep_seconds += length
        return {'mode': self.mode, 'deep': self.deep, 'running': self.running, 'stopped': self.stopped,
                'elapsed': int(self.elapsed_base + (max(0, now - self.started) if self.running else 0)),
                'totals': totals, 'deep_seconds': deep_seconds,
                'date': today.strftime('%Y-%m-%d')}

    def packets(self):
        data = self.snapshot()
        minutes = [min(int(data['totals'][name] / 60), 9999) for name in MODES]
        return [('M:U:%d,%d,%d' % (MODES[self.mode], self.deep, self.running)).encode(),
                ('M:E:%d' % min(data['elapsed'], 99999999)).encode(),
                ('M:D:%d,%d,%d' % tuple(minutes[:3])).encode(),
                ('M:R:%d' % minutes[3]).encode(),
                ('M:S:%d' % self.stopped).encode(),
                ('M:F:%d' % min(int(data['deep_seconds']/60),9999)).encode()]

    def close(self):
        self.stop()
        self.db.close()

    def finish(self, state):
        self.stop()
        self.stopped = True
        data = self.snapshot()
        now = datetime.fromtimestamp(self.clock())
        directory = self.path.parent/'daily'
        directory.mkdir(parents=True, exist_ok=True)
        path = directory/(now.strftime('%Y-%m-%d-%H%M%S-%f')+'.md')
        lines = ['# '+data['date']+' 收工记录', '',
                 '统计范围：本机仪表盘记录的活动；不是全天自动推断。', '',
                 '| 活动 | 今日分钟 |', '| --- | ---: |']
        names = {'WORK': '工作', 'STUDY': '学习', 'DEV': 'AI 开发', 'REST': '休息'}
        lines += ['| %s | %.1f |' % (names[key],data['totals'][key]/60) for key in MODES]
        lines += ['', '其中深度专注：%.1f 分钟。' % (data['deep_seconds']/60),
                  '', '## 待处理批准请求', '']
        waiting = [record for record in state.sessions.values() if not record['ended'] and
                   state.status_of(record,state.clock()) == 'WAITING']
        for record in waiting:
            name = (record['project'] or '未提供项目名').replace('\n',' ').replace('\r',' ').replace('`',"'")
            lines.append('- `'+name+'`：观察到批准请求，尚未观察到继续事件。')
        if not waiting:
            lines.append('当前没有新近证据支持的等待批准请求。')
        lines += ['', '本记录未移动或分析项目文件，也未判断任务成功。', '']
        path.write_text('\n'.join(lines),encoding='utf-8')
        return path
