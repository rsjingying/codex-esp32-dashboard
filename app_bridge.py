"""Tail local Codex lifecycle metadata and maintain a BLE connection."""
import argparse
import asyncio
import fcntl
import json
import os
from pathlib import Path
from bleak import BleakClient, BleakScanner
from state import State
from activity import Activity

SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
WRITE = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
from local_clock import clock_packet

CONTROL = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'

async def events_loop(path, state):
    offset = path.stat().st_size if path.exists() else 0
    pending = b''
    while True:
        if path.exists():
            if path.stat().st_size < offset:
                offset, pending = 0, b''
            with path.open('rb') as stream:
                stream.seek(offset)
                chunk = stream.read(65536)
                offset = stream.tell()
            pending += chunk
            while b'\n' in pending:
                line, pending = pending.split(b'\n', 1)
                try:
                    entry = json.loads(line)
                    if state.apply(entry):
                        print('HOOK:', entry['event'], 'session:', entry['session_id'],
                              'project:', entry.get('project', '-'), flush=True)
                except (ValueError, TypeError, AttributeError):
                    continue
        await asyncio.sleep(0.2)

async def ble_loop(state, activity):
    while True:
        try:
            print('Scanning for Codex-Dashboard...', flush=True)
            results = await BleakScanner.discover(timeout=10, return_adv=True,
                                                 service_uuids=[SERVICE])
            matches = [device for device, adv in results.values()
                       if adv.local_name == 'Codex-Dashboard' or device.name == 'Codex-Dashboard']
            if len(matches) != 1:
                print('Expected one dashboard; found', len(matches), flush=True)
                await asyncio.sleep(3)
                continue
            async with BleakClient(matches[0], timeout=20) as client:
                print('BLE connected. Monitoring new LOCAL Codex hook events.', flush=True)
                if client.services.get_characteristic(CONTROL) is None:
                    raise RuntimeError('Dashboard control characteristic missing. Flash V5 firmware first.')
                last = [None] * 13
                last_command = None
                try:
                    while client.is_connected:
                        raw = bytes(await client.read_gatt_char(CONTROL)).decode('ascii')
                        if raw:
                            parts = raw.split(':')
                            if len(parts) == 3 and parts[0] == 'C' and parts[1].isdigit():
                                key, command = parts[1], parts[2]
                                if key != last_command:
                                    accepted = command in ('SEEN', 'FINISH') or activity.command(command)
                                    if command == 'SEEN':
                                        state.acknowledge()
                                    if command == 'FINISH':
                                        print('Daily note:', activity.finish(state), flush=True)
                                    if accepted:
                                        print('ACTIVITY:', command, activity.snapshot(), flush=True)
                                        last_command = key
                                    else:
                                        print('Unknown device command:', command, flush=True)
                                await client.write_gatt_char(WRITE, ('ACK:' + key).encode(), response=True)
                        activity.checkpoint()
                        packets = state.packets() + state.attention_packets() + activity.packets() + [clock_packet()]
                        for index, packet in enumerate(packets):
                            if packet != last[index]:
                                await client.write_gatt_char(WRITE, packet, response=True)
                                last[index] = packet
                                if index == 0:
                                    print('SCREEN:', state.snapshot(), flush=True)
                        await asyncio.sleep(0.5)
                finally:
                    activity.stop()
            print('BLE disconnected; reconnecting.', flush=True)
        except Exception as error:
            print('BLE:', type(error).__name__, str(error), flush=True)
            await asyncio.sleep(3)

async def main(args):
    home = Path(os.environ.get('CODEX_HOME', str(Path.home()/'.codex'))).expanduser()
    path = home/'dashboard/events.jsonl'
    path.parent.mkdir(parents=True, exist_ok=True)
    with (path.parent/'bridge.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('Another dashboard bridge is already running.')
        state = State(args.session, args.stale_seconds)
        activity = Activity(path.parent/'activity.sqlite3')
        print('Event file:', path, flush=True)
        print('UNKNOWN until a new local prompt is observed. Ctrl+C to stop.', flush=True)
        print('Activity history:', path.parent/'activity.sqlite3', flush=True)
        print('Use the TIMER page on the board; startup and reconnect do not auto-start.', flush=True)
        try:
            await asyncio.gather(events_loop(path, state), ble_loop(state, activity))
        finally:
            activity.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--session', help='Monitor one exact session id')
    parser.add_argument('--stale-seconds', type=int, default=600)
    args = parser.parse_args()
    if args.stale_seconds < 10:
        parser.error('--stale-seconds must be at least 10')
    try:
        asyncio.run(main(args))
    except KeyboardInterrupt:
        print('Bridge stopped; BLE connection released.')
