# V6 三款屏保

以用户确认稳定的 V5.6 姿态、旋转与计时版本为基础，新增以下屏保。

| 款式 | 内容 |
| --- | --- |
| 极简大时钟（默认） | 黑底大数字。活动运行时显示本段计时；暂停时显示 Mac 本地时间，另显示日期 |
| 呼吸光环 | 柔和呼吸光环和时间；Work 蓝、Study 绿、AI Dev 紫、Rest 暖橙 |
| 今日积累 | 四类活动今日分钟条、深度专注分钟、当前类别和时间 |

60 秒无触摸自动进入，运行计时不受影响。屏保期间亮度降到 20%（若原亮度更低则保持），退出恢复此前亮度。内容每分钟以小幅位移调整固定显示位置。

- 上滑下一款、下滑上一款，三款循环；在本次开机内记住最后选用的款式。
- 单击只唤醒，不触发下面的按钮；横滑不会切换底下的功能页。
- 稳定姿态被识别后退出屏保，按既有规则执行 Normal 暂停或四种活动的 3 秒倒计时。
- 新的介入提醒退出屏保并恢复亮度；深度专注时保留当前页并显示角标，其余情况切 AI 页。
- POSE 教学页、活动倒计时、指令待处理和未确认介入提醒期间不自动进入屏保。
- 实时时间采用 Mac 本地日期和时区；断线后时间为推算值并标注 OFFLINE，活动计时按此前逻辑暂停。

V6 新增时钟与专注累计元数据，因此固件和桥接需一起升级，不能继续运行 V5.6 的旧桥接。升级后仍需要重新教学板子内存中的姿态。

验收屏保：启用姿态后保持设备不动、60 秒不触摸，查看自动进入；上下滑动三款；单击恢复原页面，确认没有误点按钮；活动仍持续计时；转动设备确认屏保退出，文字方向与 V5.6 一致；验证新介入提醒唤醒。

12 项 Python 离线测试与 C 日期、20 字节元数据、屏保等待阈值、上下滑判定、款式循环、原姿态和计时逻辑检查通过。完整 ESP-IDF 编译、屏保视觉效果、触摸唤醒和亮度变化仍需 Mac 与板子验证。

# Codex Dashboard V6

这版按明确的物理摆放固定五个姿态的行为，取消动作按钮的循环修改，避免误点后映射变化。基准：屏幕向前、三个实体按钮向上。

| 姿态 | 活动 | 画面补偿 | 倒计时 |
| --- | --- | --- | --- |
| Normal：屏幕向前、按钮向上 | 暂停，不启动计时；保留累计 | 0° | 无 |
| Face up：屏幕向上 | Work | 0°，不保留此前侧转角度 | 3 → 2 → 1 |
| Face down：屏幕向下 | Rest | 0°，不保留此前侧转角度 | 3 → 2 → 1 |
| Left 90：屏幕向前，向左侧转 90° | Study | 画面向右补偿 90° | 3 → 2 → 1 |
| Right 90：屏幕向前，向右侧转 90° | AI Dev | 画面向左补偿 90° | 3 → 2 → 1 |

“0°”指板子启动时的正向画面，不是直接发送 BSP ROTATE_0。官方初始化命令为 MADCTL 0xA0（BSP 270）。以这个基准合成相对旋转后：Normal / Work / Rest 使用 0xA0，Study 的画面右转 90°使用 0x00，AI Dev 的画面左转 90°使用 0xC0。触摸坐标使用相对于启动画面的逆变换，不直接套绝对 BSP 枚举。

这是根据官方 BSP 源码推导的修补，尚待板子验证。build_flash.sh 在实际 BSP 源码中检查初始化 MADCTL 0xA0，若不匹配会停止烧录，避免继续沿用错误基准。

## 安装

已验证环境：Waveshare ESP32-S3-Touch-AMOLED-2.16、Mac、ESP-IDF 5.5.5、Python 3.9、bleak 0.22.3。先 Ctrl+C 停止旧桥接。

```sh
unzip -o ~/Downloads/Codex-App-Dashboard-v6.zip -d ~/Documents/Codex-Dashboard
cd ~/Documents/Codex-Dashboard/Codex-App-Dashboard
/bin/bash build_flash.sh
~/codex-ble-tools/bin/python app_bridge.py
```

脚本先备份现有 main 源码、main/CMakeLists.txt、main/idf_component.yml、sdkconfig，然后完整清理编译、检查 CODEX V6 和 Pose shortcuts 标记再烧录；失败停止。工厂备份不变。默认端口 /dev/cu.usbmodem21101，变化时设置 DASHBOARD_PORT。

沿用 BSP、LVGL 9、waveshare/qmi8658 2.0.1、共享 I²C、PSRAM 与已验证的 SIZE / BT_CTRL_RUN_IN_FLASH_ONLY 设置，启用 Montserrat 48 字体。

若组件下载超时，可在确认代理运行后用：

```sh
HTTP_PROXY=http://127.0.0.1:29758 HTTPS_PROXY=http://127.0.0.1:29758 /bin/bash build_flash.sh
```

## 姿态教学

每次板子重新上电或烧录后都需要教学；校准不写 Flash，Mac 今日记录保留。

1. 左右滑到 POSE，自动动作暂时锁定。
2. 点 Normal，5 秒内将设备摆为屏幕向前、三个实体按钮向上，并稳定约一秒，等待 saved。
3. 依次点 Left 90 / Right 90 / Face up / Face down，然后在 5 秒内摆好对应姿态。屏幕朝下也采用先点再摆放。
4. 五行均 saved 后点开关启用，立即进入 TIMER。
5. 姿态稳定约 3 秒后触发；四个活动姿态再显示 3 秒倒计时，结束后才发送活动指令。Normal 稳定后直接暂停，不倒计时、不开始活动。

倒计时中改变姿态或滑动离开 TIMER 会取消本次触发；蓝牙不就绪时保留识别到的动作，连接后重新倒计时。保持同一姿态不会重复触发，手动暂停后不会因保持姿态而立刻恢复。进入 POSE 设置页会关闭自动模式，教学结束需重新启用。

屏幕显示 Pose: Left 90 等识别反馈；IMU invalid 说明传感器采样无效，hold still / no match 表示还未稳定匹配。传感器驱动失败不影响手动触摸。

## 其他交互

左右滑动循环切换 AI / TIMER / POSE；滑动不同时点击按钮。

TIMER 以大数字时间为主，点击显示按钮，6 秒后隐藏。手动 Work / Study / AI Dev 直接开始活动；重复点当前运行活动不清零。短按 Pause 暂停，Resume 继续；长按 Stop 后下次 Start 从零开始，今日累计保留。Focus 独立于活动类别。倒计时期间隐藏操作按钮。

AI 页保留本地 Codex 状态、项目名、任务耗时与需要介入的批准请求。新提醒在非专注时切到 AI；专注时只显示 NEEDS YOU 角标，POSE 页不被打断。Seen 只确认看到了提醒，不批准请求、不清除等待数。

桥接刚启动为 UNKNOWN，需要新本地任务提供事件。STOPPED 只表示收到 Stop，不表示任务成功；WAITING 主要覆盖批准请求。个人活动计时独立于 Codex 任务。

## 记录

SQLite：~/.codex/dashboard/activity.sqlite3。按 Mac 本地日期统计，跨午夜拆分。每 15 秒保存检查点；断线或退出暂停，不补算离线时间。重新启动进程时当前段从零开始，今日累计保留。

长按 Finish 停止活动并生成 ~/.codex/dashboard/daily/日期-时间.md，含工作、学习、开发、休息、专注时长和已观察到的待处理批准请求。当前不会移动或分析项目文档。

hooks 只记录事件名、会话/轮次 ID、时间戳、目录名；不采集提示词、工具命令、窗口或文档内容。BLE 控制只操作计时、提醒确认和本地日报，不批准或停止 Codex 任务。

## 检查与验收

离线检查：12 项 Python 测试与 C 解析、五姿态识别、固定映射、倒计时边界、滑动与触摸坐标检查通过。未在当前环境完整编译 ESP-IDF 或实测本版；Mac 脚本会执行编译。

```sh
/usr/bin/python3 -m unittest discover -p 'verify*.py' -v
cc -std=c11 -Wall -Wextra -Werror verify_firmware.c -lm -o /tmp/dashboard-v54-check
/tmp/dashboard-v54-check
```

板子验收：先启动 Work，再回 Normal，确认时间停止且画面恢复正向；分别测试左转 Study 时文字向右补偿并正向、右转 AI Dev 时文字向左补偿并正向，点击按钮的位置正确；从侧转直接平放，确认 Work / Rest 且画面恢复原始方向；验证 3 秒倒计时、滑动、日报。

回退：停止桥接，从项目 dashboard-backups/app-时间戳 恢复 main 源码、main 的 CMakeLists.txt / idf_component.yml 和项目根 sdkconfig，fullclean 后编译烧录，恢复对应旧桥接包。活动数据库无需删除。
