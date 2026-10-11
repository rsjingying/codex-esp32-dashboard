"""Offline checks. Does not connect to BLE or modify real hook configuration."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from state import State, screen_project
import install_app
import hook_event

class Checks(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.state = State(stale_seconds=10, clock=lambda: self.now)

    def event(self, name, session='a', turn='one', project='project-a'):
        return self.state.apply(dict(event=name, session_id=session, turn_id=turn,
                                     project=project, ts=123.0))

    def test_sessions_and_old_turn(self):
        self.assertEqual(self.state.snapshot()['status'], 'UNKNOWN')
        self.event('PreToolUse')
        self.assertEqual(self.state.snapshot()['seen'], 0)
        self.event('UserPromptSubmit')
        self.now = 2
        self.event('UserPromptSubmit', 'b', project='project-b')
        self.event('PermissionRequest', 'a')
        data = self.state.snapshot()
        self.assertEqual((data['project'], data['running'], data['waiting'], data['seen']),
                         ('project-b', 1, 1, 2))
        self.now = 5
        self.event('Stop', 'b')
        self.now = 7
        self.assertEqual(self.state.snapshot()['elapsed'], 3)
        self.event('UserPromptSubmit', 'b', turn='two', project='project-b')
        self.assertFalse(self.event('Stop', 'b', turn='one'))
        self.assertEqual(self.state.wire_status(), 'WORKING')
        self.event('SessionEnd', 'a', turn='')
        self.assertEqual(self.state.snapshot()['waiting'], 0)
        self.assertEqual(self.state.snapshot()['seen'], 2)

    def test_stale_and_continuation(self):
        self.event('UserPromptSubmit')
        self.now = 11
        self.assertEqual(self.state.wire_status(), 'UNKNOWN')
        self.assertEqual(self.state.snapshot()['running'], 0)
        self.assertEqual(self.state.snapshot()['elapsed'], 10)
        self.event('PreToolUse')
        self.assertEqual(self.state.wire_status(), 'WORKING')
        self.now = 14
        self.event('Stop')
        self.now = 16
        self.assertEqual(self.state.snapshot()['elapsed'], 14)
        self.event('PostToolUse')
        self.assertEqual(self.state.snapshot()['elapsed'], 16)
        self.event('Interrupt')
        self.now = 99
        self.assertEqual(self.state.snapshot()['elapsed'], 16)

    def test_pin_and_wire_limits(self):
        self.state.fixed_session = 'a'
        self.event('UserPromptSubmit')
        self.event('UserPromptSubmit', 'b', project='other')
        self.assertEqual(self.state.session, 'a')
        self.assertEqual(self.state.snapshot()['seen'], 2)
        self.event('UserPromptSubmit', project='中文名称')
        self.assertEqual(self.state.packets()[1], b'M:P:local-project')
        self.assertEqual(screen_project('é-example\n-long-name-123'), 'e-example-long-n')
        for index in range(1001):
            self.event('UserPromptSubmit', str(index), project='x' * 90)
        self.assertEqual(self.state.snapshot()['seen'], 999)
        self.assertTrue(all(len(packet) <= 20 for packet in self.state.packets()))
        self.assertFalse(self.state.apply(dict(event='Stop', session_id='a', ts=float('nan'))))

    def test_privacy_and_install(self):
        bundle = Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            home = root/'home'
            home.mkdir()
            other = {'type': 'command', 'command': 'existing-observer'}
            (home/'hooks.json').write_text(json.dumps({'hooks': {'Stop': [{'hooks': [other]}]}}))
            install_app.install_hooks(home, bundle)
            install_app.install_hooks(home, bundle)
            hooks = json.loads((home/'hooks.json').read_text())['hooks']
            for event in install_app.EVENTS:
                handlers = [h for group in hooks[event] for h in group['hooks']]
                self.assertEqual(sum(h.get('statusMessage') == install_app.MARKER for h in handlers), 1)
            self.assertIn(other, hooks['Stop'][0]['hooks'])
            spec = importlib.util.spec_from_file_location('test_hook', home/'dashboard/hook_event.py')
            hook = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(hook)
            hook.record(dict(hook_event_name='UserPromptSubmit', session_id='a', turn_id='t',
                             cwd='/private/user/projects/project-name', prompt='secret-prompt',
                             tool_input={'command': 'secret-command'}))
            raw = (home/'dashboard/events.jsonl').read_text()
            entry = json.loads(raw)
            self.assertEqual(entry['project'], 'project-name')
            self.assertEqual(set(entry), {'event', 'session_id', 'turn_id', 'ts', 'project'})
            self.assertNotIn('secret', raw)
            self.assertNotIn('/private/', raw)
            project = root/'firmware-project'
            (project/'main').mkdir(parents=True)
            (project/'main/idf_component.yml').write_text('dependencies:\n  lvgl/lvgl:\n    version: "9.*"\n')
            (project/'main/CMakeLists.txt').write_text('old-cmake')
            for name in ('main.c', 'ble_status.c', 'ble_status.h'):
                (project/'main'/name).write_text('original ' + name)
            (project/'sdkconfig').write_text('CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y\nCONFIG_COMPILER_OPTIMIZATION_PERF=y\n')
            install_app.install_firmware(project, bundle)
            self.assertTrue((project/'main/protocol.h').is_file())
            self.assertIn('CODEX V6', (project/'main/main.c').read_text())
            self.assertIn('waveshare/qmi8658', (project/'main/idf_component.yml').read_text())
            config = (project/'sdkconfig').read_text()
            self.assertIn('CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y', config)
            self.assertIn('CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y', config)
            backups = list((project/'dashboard-backups').iterdir())
            self.assertEqual((backups[0]/'main.c').read_text(), 'original main.c')

if __name__ == '__main__':
    unittest.main()
