# 从零安装（Mac）

这份指南面向已具备基础开发环境的使用者。请先确认板型完全一致，并备份原固件。仓库不提供工厂镜像和预编译固件。

## 1. 安装 ESP-IDF 和官方示例

安装 Git、CMake、Ninja、Python。按 Espressif 官方文档安装 ESP-IDF 5.5.5，目标 esp32s3：

```sh
mkdir -p ~/esp ~/Documents/Codex-Dashboard
git clone --branch v5.5.5 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.5
cd ~/esp/esp-idf-v5.5.5
./install.sh esp32s3
source ./export.sh
git clone https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16.git ~/Documents/Codex-Dashboard/waveshare-board
cd ~/Documents/Codex-Dashboard/waveshare-board/examples/esp-idf/02_lvgl_demo_v9
idf.py set-target esp32s3
idf.py build
```

首次构建解析依赖并生成 sdkconfig。建议先烧录官方 LVGL 示例确认屏幕和触摸正常。USB 端口通过 `ls /dev/cu.*` 查找。原始 Flash 备份请使用本机安装的 esptool，对照其版本命令与实际 Flash 大小执行，不要直接擦除。

## 2. 获取仪表盘与安装桥接环境

```sh
cd ~/Documents/Codex-Dashboard
git clone https://github.com/rsjingying/codex-esp32-dashboard.git
cd codex-esp32-dashboard
python3 -m venv ~/codex-ble-tools
~/codex-ble-tools/bin/python -m pip install -r requirements.txt
```

在 macOS 提示时允许运行桥接的终端使用蓝牙。以 Python 3.9 + bleak 0.22.3 为已验证组合；其他组合需要自行验证。

## 3. 安装源码、构建与烧录

```sh
cd ~/Documents/Codex-Dashboard/codex-esp32-dashboard
DASHBOARD_PORT=/dev/cu.usbmodemYOURPORT /bin/bash build_flash.sh
~/codex-ble-tools/bin/python app_bridge.py
```

将 YOURPORT 替换为实际端口。脚本默认路径：

- ESP-IDF：~/esp/esp-idf-v5.5.5；可用 DASHBOARD_IDF_PATH 覆盖。
- 官方项目：~/Documents/Codex-Dashboard/waveshare-board/examples/esp-idf/02_lvgl_demo_v9；可用 DASHBOARD_PROJECT 覆盖。

脚本备份已有源码和配置，安装固件与本地 hooks，fullclean 后构建，校验二进制版本与 BSP 初始化方向，再烧录。若校验发现驱动基准变化，会停止烧录。

hooks.json 保留其他已有 handlers；新增事件只记录事件名、会话/轮次 ID、时间戳和项目目录别名。请在你的 Codex 版本支持的设置流程中检查并允许新增 hooks。没有本地事件支持时，个人活动功能可用，Codex 状态保持 UNKNOWN。若只更新 hooks，可单独运行 `python install_app.py`。

## 4. 初次设置与使用

教学 Normal / Left / Right / Face up / Face down 五个姿态，每次先点名称，再在 5 秒内摆放并保持稳定。全部 saved 后启用开关。屏幕应按 README 的基准补偿，并能正确点选按钮。

60 秒无触摸且不在设置/倒计时/未确认提醒状态时进入屏保。上下滑切换三款，单击只唤醒。运行时显示活动计时，暂停时显示 Mac 时间。时长来自手动或姿态选定类别，不是全天行为自动识别。

## 升级与回退

先停止桥接，更新仓库，再运行构建脚本和新桥接。升级不要并行启动两个桥接进程。

旧源码与配置保存在官方项目 dashboard-backups/app-时间戳。恢复 main 文件与清单、项目根 sdkconfig，然后 fullclean、构建、烧录。数据库无需删除。

SQLite / events / daily 默认在 ~/.codex/dashboard；设置 CODEX_HOME 可更改位置。别把这个目录提交到 Git。

## V6 → V6.1 升级

停止当前桥接，在新版仓库目录运行 `bash build_flash.sh`，完成后重新运行 `~/codex-ble-tools/bin/python app_bridge.py`。升级脚本备份原主程序并编译烧录，不执行 erase-flash。

首次升级后，在 POSE 页重新教学五个姿态并开启 Pose ON；后续每次保存与开关切换都会提交到 NVS。请验证：断电重启，五个姿态仍显示 saved，Pose 开关保留；连接桥接后新的稳定姿态经过 3 秒倒计时进入对应活动，Normal 仅暂停。也可测试关闭 Pose 后重启仍为 OFF，以及长按 Hold Clear 后重启不会恢复旧校准。

当前仓库自动检查覆盖持久数据和 NVS 适配层的离线测试。ESP-IDF 完整编译、NVS 实际断电恢复及屏幕触摸需要在连接开发板的 Mac 上验证。
