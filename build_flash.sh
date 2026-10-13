#!/bin/bash
# Run on the Mac that owns the already tested board project.
set -e
bundle_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="${DASHBOARD_PROJECT:-$HOME/Documents/Codex-Dashboard/waveshare-board/examples/esp-idf/02_lvgl_demo_v9}"
port="${DASHBOARD_PORT:-/dev/cu.usbmodem21101}"
idf_dir="${DASHBOARD_IDF_PATH:-$HOME/esp/esp-idf-v5.5.5}"
if [ ! -f "$idf_dir/export.sh" ]; then
    echo "ESP-IDF export.sh was not found at the previously installed path."
    exit 1
fi
if [ ! -e "$port" ]; then
    echo "Board port is missing: $port. Reconnect USB or set DASHBOARD_PORT."
    exit 1
fi
source "$idf_dir/export.sh"
cd "$bundle_dir"
python install_app.py --firmware --project "$project_dir"
cd "$project_dir"
idf.py fullclean
idf.py build
python - <<'PY'
from pathlib import Path
import re
bsp_source = Path('managed_components/waveshare__esp32_s3_touch_amoled_2_16/esp32_s3_touch_amoled_2_16.c')
if not bsp_source.is_file():
    raise SystemExit('BSP source not found; rotation baseline could not be verified. Flash stopped.')
text = bsp_source.read_text()
init = re.search(r'lcd_init_cmds\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
if not init or not re.search(r'\{0x36,\s*\(uint8_t\[\]\)\{0xA0\}', init.group(1)):
    raise SystemExit('BSP boot orientation differs from verified A0 baseline. Flash stopped.')
print('BSP boot MADCTL A0 baseline verified.')
data = Path('build/lvgl_demo_v9.bin').read_bytes()
for marker in (b'CODEX V6.1', b'Pose shortcuts'):
    if marker not in data:
        raise SystemExit('V6.1 marker missing; flashing has been stopped: ' + repr(marker))
print('V6.1 binary markers verified.')
PY
idf.py -p "$port" flash
echo "V6.1 flashed. Start app_bridge.py from the bundle directory."
