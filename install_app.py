"""Merge monitoring hooks; optionally install the APP-aware firmware source."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import shlex
import shutil
import tempfile
import re

EVENTS = ('UserPromptSubmit', 'PreToolUse', 'PermissionRequest', 'PostToolUse',
          'Stop', 'Interrupt', 'SessionEnd')
MARKER = 'Codex dashboard metadata'

def write_json(path, value):
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False,
                                     encoding='utf-8') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        temporary = Path(stream.name)
    temporary.replace(path)

def install_hooks(home, bundle):
    home.mkdir(parents=True, exist_ok=True)
    path = home/'hooks.json'
    data = json.loads(path.read_text()) if path.exists() else {'hooks': {}}
    if not isinstance(data, dict) or not isinstance(data.get('hooks'), dict):
        raise SystemExit('Existing hooks.json has an unsupported shape; no changes made.')
    for event in EVENTS:
        if event in data['hooks'] and not isinstance(data['hooks'][event], list):
            raise SystemExit('Existing hook event must be an array: ' + event)
    if path.exists():
        backup = home/('hooks.json.backup-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
        shutil.copy2(path, backup)
        print('Hooks backup:', backup)
    directory = home/'dashboard'
    directory.mkdir(exist_ok=True)
    shutil.copy2(bundle/'hook_event.py', directory/'hook_event.py')
    command = '/usr/bin/python3 ' + shlex.quote(str(directory/'hook_event.py'))
    for event in EVENTS:
        groups = data['hooks'].setdefault(event, [])
        retained = []
        for group in groups:
            if not isinstance(group, dict) or not isinstance(group.get('hooks'), list):
                retained.append(group)
                continue
            handlers = [handler for handler in group['hooks']
                        if not (isinstance(handler, dict) and
                                handler.get('statusMessage') == MARKER)]
            if handlers:
                retained.append(dict(group, hooks=handlers))
        retained.append({'hooks': [{'type': 'command', 'command': command,
                                    'timeout': 3, 'statusMessage': MARKER}]})
        data['hooks'][event] = retained
    write_json(path, data)
    print('Installed:', path)
    print('Review and trust the new hooks in Codex, then start a new LOCAL task.')

def install_firmware(project, bundle):
    project = project.expanduser().resolve()
    if not (project/'main/main.c').is_file() or not (project/'sdkconfig').is_file():
        raise SystemExit('Expected an ESP-IDF example with main/main.c and sdkconfig. Build the official LVGL demo first.')
    manifest = project/'main/idf_component.yml'
    if not manifest.is_file() or not re.search(r'^dependencies:\s*$', manifest.read_text(), re.M):
        raise SystemExit('Expected the existing main/idf_component.yml dependency manifest.')
    manifest_text = manifest.read_text()
    if not re.search(r'^  waveshare/qmi8658:', manifest_text, re.M):
        manifest_text = re.sub(r'^dependencies:\s*$',
                               'dependencies:\n  waveshare/qmi8658:\n    version: "2.0.1"',
                               manifest_text, count=1, flags=re.M)
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup = project/'dashboard-backups'/('app-' + stamp)
    backup.mkdir(parents=True)
    for name in ('main.c', 'ble_status.c', 'ble_status.h', 'protocol.h',
                 'posture.c', 'posture.h', 'posture_logic.h', 'interaction.h', 'pose_countdown.h', 'pose_rules.h', 'screensaver_logic.h'):
        if (project/'main'/name).exists():
            shutil.copy2(project/'main'/name, backup/name)
        shutil.copyfile(bundle/'firmware'/name, project/'main'/name)
        (project/'main'/name).touch()
    for name in ('CMakeLists.txt', 'idf_component.yml'):
        if (project/'main'/name).exists():
            shutil.copy2(project/'main'/name, backup/name)
    (project/'main/idf_component.yml').write_text(manifest_text)
    (project/'main/CMakeLists.txt').write_text(
        'idf_component_register(SRCS "main.c" "ble_status.c" "posture.c" INCLUDE_DIRS ".")\n')
    config = project/'sdkconfig'
    shutil.copy2(config, backup/'sdkconfig')
    values = {'CONFIG_COMPILER_OPTIMIZATION_DEBUG':'n',
              'CONFIG_COMPILER_OPTIMIZATION_SIZE':'y',
              'CONFIG_COMPILER_OPTIMIZATION_PERF':'n',
              'CONFIG_COMPILER_OPTIMIZATION_NONE':'n',
              'CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY':'y'}
    values['CONFIG_LV_FONT_MONTSERRAT_48'] = 'y'
    lines = [line for line in config.read_text().splitlines() if not any(
        line.startswith(key+'=') or line == '# '+key+' is not set' for key in values)]
    lines += [key+'=y' if value == 'y' else '# '+key+' is not set'
              for key, value in values.items()]
    config.write_text('\n'.join(lines)+'\n')
    print('Firmware backup:', backup)
    print('V6 sources installed; retained pose/rotation and added three screensavers.')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--firmware', action='store_true')
    parser.add_argument('--project', type=Path, default=Path(
        '~/Documents/Codex-Dashboard/waveshare-board/examples/esp-idf/02_lvgl_demo_v9'))
    args = parser.parse_args()
    bundle = Path(__file__).resolve().parent
    home = Path(os.environ.get('CODEX_HOME', str(Path.home()/'.codex'))).expanduser()
    if args.firmware:
        install_firmware(args.project, bundle)
    install_hooks(home, bundle)
