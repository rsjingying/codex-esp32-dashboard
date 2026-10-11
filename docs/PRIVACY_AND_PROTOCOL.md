# 数据与 BLE 协议

## 本地数据

hooks 记录：事件名、session / turn 标识、时间戳、cwd 的目录别名。活动数据库记录：开始/结束时间、WORK / STUDY / DEV / REST 类别和 deep 标志。日报记录今日分钟与已观察到的待处理请求。

不采集提示词、工具命令、文档内容、浏览历史或应用窗口。默认保存在 ~/.codex/dashboard；不要公开该目录。本仓库只包含代码和合成测试数据。

## BLE

设备名 Codex-Dashboard。服务 UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`，状态写入特征 `6e400002-b5a3-f393-e0a9-e50e24dcca9e`，控制读取特征 `6e400003-b5a3-f393-e0a9-e50e24dcca9e`。

每个元数据包最多 20 字节，ASCII：

| 前缀 | 内容 |
| --- | --- |
| APP: | IDLE / WORKING / WAITING / DONE / ERROR / UNKNOWN |
| M:P: | 项目别名，16 字符上限 |
| M:C: | running,waiting,seen |
| M:T: | 当前任务秒数 |
| M:A: / M:N: | 待介入项目 / 提醒标识 |
| M:U: | 活动 mode,deep,running；模式 1 工作 / 2 学习 / 3 开发 / 4 休息 |
| M:E: | 本段活动秒数 |
| M:D: / M:R: | 工作,学习,开发累计分钟 / 休息累计分钟 |
| M:S: / M:F: | stopped / 专注累计分钟 |
| M:K: | YYYYMMDD,本地当天秒数 |

板端读取控制队列返回 `C:<id>:<command>`，桥接执行后写 `ACK:<id>`。指令只操作活动计时、SEEN 和 FINISH，不执行任意命令，不批准或停止 Codex 任务。重复控制 ID 在当前连接内去重；此协议不是跨进程 exactly-once 消息系统。

目前沿用未配对认证的实验 BLE 协议，适用于个人开发场景，不是生产安全产品。附近设备可能连接并伪造仪表盘数据；应用不能据此作权限或安全决策。

## 统计含义

活动类别是使用者选择的标签；时长不是实际行为识别结果。Focus 是手动标志，不是专注程度检测。屏保光环只是动画。

STOPPED 表示观察到 Stop，不证明成功。WAITING 主要覆盖批准请求，不覆盖所有提问和登录。断线和正常退出暂停；异常退出最多丢失最近 15 秒检查点。日期按 Mac 本地时区划分。
