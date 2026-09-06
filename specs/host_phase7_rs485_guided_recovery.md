# Host Phase 7 RS485 断线恢复半自动验收 Technical Spec

## 1. 文档状态

- 版本：1.0
- 日期：2026-09-06
- 对应任务：TASK-023
- 评审结论：通过，无阻塞未决项

## 2. 目标与边界

本规范定义当前安全低压、约 20 cm 点对点 RS485 台架上的正式断线—重连验收。实现复用生产 `MainWindow`、`TestAutomationController`、`GuidedTestCoordinator`、`TestEngine`、唯一 `QSerialPortModbusClient`、应用状态机、诊断和会话日志，不创建第二客户端，不关闭或重开串口模拟恢复。

人工动作只允许断开、恢复 TTL-RS485 模块与 USB-RS485 转换器之间的 A/B 信号线。USB 转换器、开发板供电和公共地必须保持连接；不得交换 A/B 极性或改变终端、偏置和线长。

## 3. 采用方案

### 3.1 正式套件

正式文件为 `testcases/phase7/phase7-rs485-disconnect-recovery.json`，Schema v3，且只包含 `TC-R001`。v3 已由 TASK-021 固定为四个数据驱动步骤，预检不伪装成人工步骤：

1. `disconnect`：展示断开 A/B 的操作与安全提示；
2. `observe-outage`：读取 Firmware minor 寄存器，连续 3 次 `ResponseTimeout` 才确认中断；
3. `reconnect`：展示按原极性恢复 A/B 的操作与安全提示；
4. `observe-recovery`：读取 Firmware minor 寄存器，连续 3 次合法响应且值等于 2 才确认恢复。

探测地址使用 PDU 地址 40（文档寄存器 40041），只读、不修改 DUT。请求超时 500 ms，探测间隔 250 ms；中断 deadline 为 5000 ms，恢复 deadline 为 4900 ms。恢复观察由协调器在重连确认后立即开始，因此只有稳定恢复耗时严格小于 5 秒才能进入 PASS。

### 3.2 预检与正式会话

提供可见 GUI 工具 `task023_rs485_validation`：

- `preflight`：运行时枚举并核对端口身份，连接后启动一次生产监控批次，验证 Firmware 0.2、五个读块和链路在线，然后停止监控并断开；不显示断线提示。
- `full`：建立独立会话，重复相同在线门禁后执行正式 v3 套件；终态后再次完成一个生产监控批次，确认链路在线、Firmware 仍为 0.2，再断开。

两种模式使用不同进程和会话日志。正式结论只引用最终完整 `full` 会话；首次开发尝试、预检和失败会话不删除，也不并入最终 PASS 统计。

## 4. 命令行与运行门禁

工具参数：

```text
task023_rs485_validation --mode <preflight|full> --port <port>
  --suite <path> --source-revision <revision> [--timeout-ms 500]
```

门禁如下：

- `--port` 必须来自运行时枚举，不写入正式 JSON；
- 当前台架要求 VID `345F`、PID `3020`、序列号 `A02001JS`，身份不符立即失败；
- 串口固定为 115200、8N1、无流控、Slave ID 1；
- 正式套件必须是 Schema v3、suite id `phase7-rs485-disconnect-recovery`、唯一启用的 `TC-R001`、真实 RS485 环境、四步固定顺序、只读地址 40、连续数 3、恢复 deadline 4900 ms 和 Firmware minor 断言 2；
- 套件与验收程序 SHA-256、源码修订、硬件、端口和通信参数必须写入会话元数据。

## 5. 状态与人工操作

`full` 状态顺序为：

```text
连接 -> 预检监控 -> 停止监控 -> 引导式 Testing
     -> 等待断开确认 -> 中断观察 -> 等待重连确认 -> 恢复观察
     -> 释放 Testing owner -> 最终监控读取 -> 停止监控 -> 断开
```

人工等待阶段不得发送 Testing 请求。确认按钮只提交一次性 token；用户确认不能替代自动观察。操作员取消、人工超时、恢复超时、中止或致命通信错误均不得 PASS，并必须显示恢复 A/B 接线提醒。

正式运行不得执行隐藏的串口 close/open、自动重连或 owner 切换。中断与恢复探测期间只允许 Testing owner，且严格单请求串行。

## 6. 恢复时间口径

起点为重连提示的合法 `confirm` 单调时刻，保存：

- 到首个满足合法 0x03 响应且 Firmware minor 等于 2 的耗时；
- 到第 3 个连续满足响应的稳定恢复耗时。

最终验收采用稳定恢复耗时，要求 `toStableRecovery < 5000 ms`。正式套件用 4900 ms deadline 将此门槛纳入协调器的自动判定；验收工具仍须对不可变结果中的耗时再次审计。

## 7. 证据审计

正式 `full` 必须核对：

- 两条人工记录均为 confirm，包含提示、动作 UTC、一次性 token 和等待耗时；
- 中断、恢复观察均为 Matched，所需/达到连续数均至少为 3；
- 中断每次匹配均为结构化 `ResponseTimeout`，有 TX、无合法 RX，并有 RTT；
- 恢复每次匹配均为成功的 0x03 响应、Firmware minor 为 2，且 TX/RX、CRC、RTT 完整；
- 所有探测 RequestId 唯一，并在不可变结果、诊断、communication `request_completed` 和 TEST `request_attempt` 中集合一致；
- TEST 日志包含 prompt、confirm、observation start/finish 和 case finish 事件；
- 最终用例与套件均 PASS、`physicalLinkRestored=true`、无恢复提醒、无 auxiliary error；
- Testing owner 已释放，应用返回 `CONNECTED_IDLE`，随后最终监控读成功。

日志结束后输出会话 ID、路径、大小、行数和 SHA-256。原始 JSONL 保持在忽略目录 `output/logs`，测试记录只保存审计摘要与哈希。

## 8. 错误与清理

- 任何门禁或审计失败都记录首个错误并进入清理；
- 引导运行中先调用现有 `abort()`，不得直接关闭串口；
- 监控中先停止监控；仅在 `CONNECTED_IDLE` 且 owner 为 None 时断开；
- 若物理链路未被软件确认恢复，工具和 UI 必须提示“按原极性恢复 A/B 并确认通信”，不得声称安全恢复；
- 进程 watchdog 只防止无限等待，不代替各阶段 deadline。

## 9. 自动测试

- 新增正式套件目录测试，验证唯一性、固定步骤、只读探测、连续数、严格恢复阈值和业务断言；
- 继续运行全部 v1/v2/v3 Loader、guided 模型/协调器、TestEngine、offscreen UI 与应用 smoke；
- 使用全新构建目录完成配置、构建和全部 CTest。

## 10. 实机执行顺序

1. 复核安全低压接线、A/B 标识、公共地、开发板和 USB-RS485 均已连接；
2. 执行 `preflight`，不得断线，确认端口身份、Firmware 0.2 和一次完整监控批次；
3. 独立执行 `full`；看到断开提示后只断开 A/B，再点击确认；
4. 软件达到连续 3 次超时并显示重连提示后，按原极性接回 A/B，再点击确认；
5. 等待软件完成连续 3 次合法响应、审计、最终在线读取和退出；
6. 保存最终 stdout、会话日志路径与 SHA-256，更新 TASK 和测试记录。

## 11. 评审记录

### 必须修复

无。预检不能加入 Schema v3 四步，否则会破坏 TASK-021 已评审契约；本规范将其作为 Testing owner 取得前的生产监控门禁，并在正式流程结束后重复验证在线状态。

### 建议修复

无。

### 可选优化

STM32 Reset、传感器断开和人工输入变化可在后续独立任务扩展，不属于 TASK-023。
