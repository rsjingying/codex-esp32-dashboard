"""V4 checks with simulated clocks and BLE; no actual device is contacted."""
import asyncio
import contextlib
from datetime import datetime
import importlib.util
import io
import os
from pathlib import Path
import sqlite3
import sys
import tempfile
import time
import types
import unittest
from unittest.mock import patch
from activity import Activity
from state import State

class V4Checks(unittest.TestCase):
    def test_activity_transitions_recovery_and_midnight(self):
        with tempfile.TemporaryDirectory() as root:
            now = [datetime(2026, 10, 10, 23, 59, 30).timestamp()]
            activity = Activity(Path(root)/'history.db', clock=lambda: now[0])
            self.assertFalse(activity.snapshot()['running'])
            activity.command('FOCUS')
            self.assertTrue(activity.snapshot()['running'])
            self.assertTrue(activity.snapshot()['deep'])
            activity.command('PAUSE')
            activity.command('DEEP0')
            activity.command('STUDY')
            now[0] += 20
            activity.command('DEEP1')
            now[0] += 40
            data = activity.snapshot()
            self.assertEqual(data['elapsed'], 60)
            self.assertEqual(data['totals']['STUDY'], 30)
            self.assertEqual(data['deep_seconds'], 30)
            activity.command('PAUSE')
            now[0] += 500
            self.assertEqual(activity.snapshot()['elapsed'], 60)
            activity.command('RESUME')
            now[0] += 60
            activity.command('DEV')
            self.assertEqual(activity.snapshot()['elapsed'], 0)
            now[0] += 90
            data = activity.snapshot()
            self.assertEqual((data['totals']['STUDY'], data['totals']['DEV']), (90, 90))
            self.assertTrue(all(len(p) <= 20 for p in activity.packets()))
            activity.checkpoint(force=True)
            # Simulate abrupt termination: only checkpointed time is recovered.
            activity.db.close()
            now[0] += 300
            recovered = Activity(Path(root)/'history.db', clock=lambda: now[0])
            self.assertFalse(recovered.snapshot()['running'])
            self.assertEqual(recovered.snapshot()['totals']['DEV'], 90)
            recovered.close()

    def test_finish_writes_a_time_note_without_touching_project_files(self):
        with tempfile.TemporaryDirectory() as root:
            now = [100.0]
            path = Path(root)/'activity.sqlite3'
            activity = Activity(path,clock=lambda:now[0])
            activity.command('WORK')
            now[0] += 120
            state = State()
            for event in ('UserPromptSubmit','PermissionRequest'):
                state.apply(dict(event=event,session_id='a',turn_id='t',project='project-a',ts=1))
            note = activity.finish(state)
            self.assertFalse(activity.running)
            self.assertIn('| 工作 | 2.0 |', note.read_text())
            self.assertIn('project-a',note.read_text())
            self.assertEqual(note.parent,Path(root)/'daily')
            activity.close()

    def test_attention_episodes_and_acknowledgement(self):
        now = [1]
        state = State(clock=lambda: now[0], stale_seconds=10)
        def event(name, session='a', project='A'):
            return state.apply(dict(event=name,session_id=session,turn_id='t',ts=1,project=project))
        event('UserPromptSubmit')
        event('PermissionRequest')
        first = state.attention_seq
        event('PermissionRequest')
        self.assertEqual(state.attention_seq, first)
        event('UserPromptSubmit', 'b', 'B')
        self.assertEqual(state.session, 'b')
        self.assertEqual(state.attention_packets()[0], b'M:A:A')
        state.acknowledge()
        self.assertEqual(state.attention_packets()[1], b'M:N:0')
        self.assertEqual(state.snapshot()['waiting'], 1)
        event('PermissionRequest','b','B')
        self.assertEqual(state.attention_packets()[0], b'M:A:B')
        event('PostToolUse','b','B')
        self.assertEqual(state.attention_packets()[1], b'M:N:0')
        event('PermissionRequest','b','B')
        self.assertGreater(state.attention_seq, first)
        now[0] += 11
        self.assertEqual(state.attention_packets(), [b'M:A:-', b'M:N:0'])

    def test_ble_control_retry_ack_and_disconnect_pause(self):
        now = [100.0]
        writes = []
        class Scanner:
            calls = 0
            @classmethod
            async def discover(cls, **kwargs):
                cls.calls += 1
                if cls.calls > 1:
                    raise asyncio.CancelledError()
                return {'dev': (types.SimpleNamespace(name='Codex-Dashboard'),
                                types.SimpleNamespace(local_name='Codex-Dashboard'))}
        class Client:
            def __init__(self,*args,**kwargs):
                self.reads = 0
                self.services = types.SimpleNamespace(get_characteristic=lambda name: True)
            async def __aenter__(self): return self
            async def __aexit__(self,*args): pass
            @property
            def is_connected(self): return self.reads < 4
            async def read_gatt_char(self,*args):
                packets = [b'C:1:STUDY', b'C:1:STUDY', b'C:2:DEEP1', b'C:3:RESUME']
                packet = packets[self.reads]
                self.reads += 1
                now[0] += 20
                return packet
            async def write_gatt_char(self,char,packet,**kwargs): writes.append(bytes(packet))
        async def immediate(seconds): pass
        bleak = types.SimpleNamespace(BleakClient=Client,BleakScanner=Scanner)
        with tempfile.TemporaryDirectory() as root, patch.dict(sys.modules, {'bleak':bleak}):
            activity = Activity(Path(root)/'history.db', clock=lambda:now[0])
            spec = importlib.util.spec_from_file_location('v4_bridge_test',Path(__file__).parent/'app_bridge.py')
            bridge = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(bridge)
            with contextlib.redirect_stdout(io.StringIO()), patch.object(bridge.asyncio,'sleep',immediate):
                with self.assertRaises(asyncio.CancelledError):
                    asyncio.run(bridge.ble_loop(State(),activity))
            self.assertFalse(activity.running)
            self.assertEqual(activity.snapshot()['totals']['STUDY'],60)
            self.assertEqual(activity.db.execute('SELECT COUNT(*) FROM segments').fetchone()[0],2)
            self.assertEqual(writes.count(b'ACK:1'),2)
            self.assertTrue(all(len(packet)<=20 for packet in writes))
            self.assertIn(b'M:U:2,1,1',writes)
            activity.close()

if __name__ == '__main__':
    unittest.main()
