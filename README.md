# Codex ESP32 Dashboard

将 Waveshare ESP32-S3-Touch-AMOLED-2.16 打造成 Mac 上的 Codex 状态仪表盘与个人活动计时器。

A community-built BLE dashboard for local Codex events, pose-driven activity tracking and three AMOLED screensavers. Not an official OpenAI or Waveshare product.

**版本：V6.1。** 原型已在作者的 Mac + 开发板上使用；支持范围以这套硬件和环境为准。CI 检查离线逻辑，不能代替 ESP-IDF 编译和实机测试。

## 功能

- 本地 Codex 事件状态、项目别名、任务耗时、运行与等待数。
- 优先展示需要介入的批准请求；Seen 仅确认看到，不批准请求。
- Work / Study / AI Dev / Rest 活动计时、深度专注、暂停/恢复、长按停止。
- 五个教学姿态、NVS 持久校准与开关、活动启动前 3 秒倒计时、反向补偿转屏与触摸坐标转换。
- 左右滑动切换 AI / TIMER / POSE；点计时页显示按钮，6 秒后隐藏。
- 三款屏保：极简大时钟、呼吸光环、今日积累；上下滑换款，单击唤醒。
- SQLite 本地活动记录；长按 Finish 生成每日 Markdown 记录。

## 支持环境

| 项目 | 已使用环境 |
| --- | --- |
| 开发板 | Waveshare ESP32-S3-Touch-AMOLED-2.16，16MB Flash / 8MB PSRAM |
| 主机 | Apple Silicon Mac，BLE |
| 固件 | ESP-IDF 5.5.5、LVGL 9、Waveshare BSP 与 QMI8658 2.0.1 |
| 桥接 | Python 3.9，bleak 0.22.3 |
| Codex | 能执行本地 hooks 的桌面版本；普通云端会话不在监控范围内 |

从零安装见 [安装指南](docs/INSTALL.zh-CN.md)；设备交互与验证见 [使用指南](docs/DEVICE_GUIDE.zh-CN.md)。

## 姿态规则

面对屏幕，三个实体按钮在上方是 Normal。左 / 右转指屏幕仍朝向观察者，在屏幕平面内逆 / 顺时针侧转。

| 姿态 | 活动 | 显示补偿 |
| --- | --- | --- |
| Normal | 暂停，保留累计 | 原始方向 |
| 左转 90° | Study | 相对原始画面右转 90° |
| 右转 90° | AI Dev | 相对原始画面左转 90° |
| 屏幕朝上平放 | Work | 原始方向 |
| 屏幕朝下平放 | Rest | 原始方向 |

姿态校准与 Pose 开关写入板子 NVS，重启后自动恢复。V6 升级到 V6.1 后需最后教学一次；普通烧录保留 NVS，整片擦除或改变分区可能清除校准。屏保款式仍仅保存在内存。活动记录保存在 Mac。

重启后从未识别姿态的暂停状态开始，等待新的稳定采样、BLE 桥接就绪和 3 秒倒计时（Normal 只暂停）。查看 POSE 页临时暂停姿态动作，离开后重新识别；重新教学会保存 Pose OFF，完成后手动开启。长按 Hold Clear 只清除本项目的姿态校准与开关。存储失败会提示且保留原有设置。

## 数据真实性与边界

当前时间来自 Mac；活动时长来自实际启动的计时与本地数据库，不填充演示数据。它记录你选择的类别，不能推断你是否实际工作或专注。呼吸光环只是动画。今日分钟按整数显示。

初始状态为 UNKNOWN，需要桥接启动后的新本地任务提供事件。STOPPED 不证明任务成功；WAITING 主要覆盖批准请求。断线时暂停活动，不补算离线时间。Finish 只生成统计记录，不移动或分析项目文档。

详见 [数据与协议说明](docs/PRIVACY_AND_PROTOCOL.md)。本仓库不包含用户事件、活动库、日报、凭据或工厂固件备份。

## 检查

```sh
python -m unittest discover -p 'verify*.py' -v
cc -std=c11 -Wall -Wextra -Werror verify_firmware.c -lm -o /tmp/dashboard-check
/tmp/dashboard-check
bash -n build_flash.sh
```

13 项 Python 测试覆盖状态隔离、旧 turn、hooks 合并、安装备份、活动跨午夜、暂停/停止、BLE ACK、断线、日报、时钟和专注累计。新增的持久存储检查编译实际 NVS 适配代码，使用故障注入后端验证重启恢复、损坏记录、写入失败与清除。C 检查解析上限、日期、姿态、转屏基准、滑动与屏保逻辑。

## 开源与依赖

项目自有代码使用 [MIT License](LICENSE)。外部组件通过 ESP-IDF Component Manager 获取，遵循各自许可证；见 [第三方说明](THIRD_PARTY_NOTICES.md)。源码不内置第三方组件。

欢迎提交问题与改进建议。报告时附版本、板型和经脱敏的错误信息，不上传提示词、活动数据库或整个个人 `.codex` 目录。
