# TASK-020 Phase 6 真实 RS485 完整套件验证记录

> 日期：2026-09-05
>
> 结论：通过；TASK-020 完成，Phase 6 关闭。

## 1. 验证对象与环境

- 源码基线：`0cf06318abf05c42f0017c436add5ea843803d2f` 加 TASK-020 工作区修改；当前验收程序 SHA-256 为 `FA255FC77182D2BE0DF70C519A067AA2A5D0C9037FFBB5A4E958AFA38797DDD1`。
- 正式套件：`testcases/phase6/phase6-rs485-full.json`，SHA-256 为 `6AE02899B8FA4A0EA542AC99B8ADE9D7DD946ADDD05C3682C7AE8AC283D5CCD3`。
- Host：Windows、MSVC 19.51.36256.0、Qt 6.8.3、CMake 4.4.3、Ninja 1.13.2，Debug 构建。
- DUT：NUCLEO-F411RE，Firmware 0.2；USART1、MAX13487EESA 自动换向模块、约 20 cm 线缆、公共地，沿用 TASK-010 安全低压台架。
- 运行时端口枚举：COM6，MacroSilicon USB Serial Ports，VID `345F`、PID `3020`、序列号 `A02001JS`；另枚举到 COM3 ST-LINK VCP，但未选择。
- 串口参数：115200、8N1、无流控、Slave ID 1、请求超时 500 ms。

## 2. 实现内容

新增 `task020_rs485_validation` 可见 GUI 验收工具，复用生产 `MainWindow`、`QSerialPortModbusClient`、应用状态机、配置服务、诊断模型、会话日志、自动化控制器与 TestEngine。工具提供互相隔离的 `preflight`、`abort`、`full` 三种模式，并实施以下门禁：

- 运行时 USB 串口身份和正式 20 条套件目录严格核对；
- 套件/程序哈希、源码标识、硬件与串口参数写入会话；
- 任何测试前读取四路阈值，结束后独立读取并逐项比对；
- 诊断记录与 TEST `request_attempt` 的全量 RequestId 集合、数量和唯一性核对；内存有界保留的 attempt 只要求为全集子集；
- 正式模式检查 20/20 PASS、15 步严格串行、异常后合法请求恢复、四个写用例末次恢复、至少 600000 ms 稳定性统计及 UI 心跳；
- 独立中止模式验证在途请求取消、事件循环响应、owner 释放、最终断开和日志正常关闭。
- 真实等待统一使用 `Qt::PreciseTimer`，避免默认粗粒度定时器使每周期迟到在 10 分钟窗口内累计；正式 1000 ms 间隔、600000 ms 时长和 594 最低成功数保持不变。
- 最终 `validation_complete` 日志持久化套件/稳定性持续时间与计数、RTT 汇总、UI tick/最大迟到和前后阈值，不依赖易失的终端输出完成审计。

## 3. 全新构建与自动测试

使用全新目录 `build-host-task020`：

```powershell
$vsShell = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1'
& $vsShell -Arch amd64 -SkipAutomaticLocation
cmake -S host/qt -B build-host-task020 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH='D:\Dev\Qt\6.8.3\msvc2022_64'
cmake --build build-host-task020 --parallel
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir build-host-task020 --output-on-failure
```

结果：配置成功，134 步全新构建成功；精确定时与审计元数据修复后重新构建，最终复跑 20/20 CTest 通过，总测试时间 9.90 秒。测试包含通信后端、配置、诊断、监控、Schema/断言、TestEngine、正式套件、自动化 UI 和应用 smoke 回归。

## 4. 实机短时验证

### 4.1 预检模式

执行 TC-F001、TC-F005、TC-P004、TC-R-AUTO-001，其余 16 条明确为 `SKIPPED/not_selected`，不计作正式套件结果。

- 4/4 所选用例 PASS；Firmware 实际版本为 `[0, 2]`。
- TC-P004 收到 Modbus 异常 `0x02`；TC-R-AUTO-001 随后的合法请求使用不同 RequestId 并成功，无隐藏串口重开。
- 共 5 次请求；保留 attempt、诊断、通信日志和 TEST 日志数量均为 5，RequestId 集合一致，并逐条比对状态、TX/RX/RTT。
- UI 心跳 5 tick，最大迟到 64 ms。
- 测试前后阈值均为 A/B/C `60.0 ℃`、环境 `40.0 ℃`；最终断开且 owner 释放。
- 会话日志：`output/logs/2026-09-05/session-20260905-103314-152-e5a02406-74c0-4ec1-9db0-e6be1a49eea4.jsonl`，10844 字节、25 行，SHA-256 `6835A23927AF33B7CC6116355D2F7FBE91853FC5141689BB20F4EF5D6EADB944`。

### 4.2 独立中止模式

只选择 TC-S001，在首个稳定性请求进入运行后延迟 2000 ms 调用既有自动化中止入口；其余 19 条明确为 `SKIPPED/not_selected`，该会话不计作正式 PASS。

- 中止前完成 2 次成功请求，第 3 次在途请求进入 `Cancelled`；TC-S001 以 `Aborted/ERROR` 终止，符合专项预期。
- 3 次请求在诊断、通信日志与 TEST 日志中全部存在且 RequestId 唯一；有界结果保留首尾 2 条，并逐条核对状态、TX/RX/RTT；取消请求保留 TX、已接收部分 RX 和 RTT。
- UI 心跳 20 tick，最大迟到 32 ms；owner 释放并最终断开。
- 测试前后阈值均为 A/B/C `60.0 ℃`、环境 `40.0 ℃`。
- 会话日志：`output/logs/2026-09-05/session-20260905-103326-922-5d9c819f-04f9-4bfe-ac84-a44108cc2e69.jsonl`，7020 字节、15 行，SHA-256 `3E5CC11FB865A539B283E7DC0F4E4800F181D4DC5D64FF24FFC4415E8EEB54DE`。

## 5. 正式完整套件验证

最终正式结果只引用最新会话 `6d6a9a7d-9252-4a3e-966f-da0e0d3ea344`；更早开发尝试不参与本节统计，原始日志仍按审计要求保留。

- 正式主套件 20/20 PASS，无 FAIL、ERROR、SKIPPED 或 NOT_RUN；功能/协议/边界/数据一致性/自动恢复/稳定性分别为 8/4/4/2/1/1 条。
- 套件持续 608438 ms；TC-S001 真实持续 600001 ms，共 599 次请求，599 成功、0 失败、0 超时，失败率 0，599 个 RTT 全部有效。
- TC-S001 RTT 最小/平均/最大值为 20/32/68 ms；内存按有界代表策略保留 31 条、丢弃 568 条，完整 599 条 attempt 均存在于 JSONL。
- 正式套件总计 647 个 TEST attempt；诊断模型、通信日志和 TEST 日志均为 647 个 Testing RequestId，集合完全相等且无重复，所有通信记录均有 TX、RX 和 RTT。
- TC-P007 完成 15 个严格串行步骤；TC-R-AUTO-001 的异常请求 RequestId 48 后，合法版本请求使用 RequestId 49 成功，无隐藏重开。
- TC-F004、TC-P002、TC-B003、TC-B004 均完成写前读、写入、独立回读和恢复；TC-B005 由 Firmware 以 0x03 拒绝，不产生成功越界写入。
- Firmware 版本请求 RequestId 9 的响应为 `01 03 04 00 00 00 02 7B F2`，解码为 0.2。
- 测试前后阈值均为 A/B/C `60.0 ℃`、环境 `40.0 ℃`；最终 owner 为 None、应用断开且会话正常结束。
- 可见 UI 共记录 4125 个心跳，最大迟到 484 ms，小于 1000 ms 门槛。
- 会话日志：`output/logs/2026-09-05/session-20260905-103519-983-6d6a9a7d-9252-4a3e-966f-da0e0d3ea344.jsonl`，648218 字节、1341 行，SHA-256 `C5F7229A42C4CCF80D9C32CDD2BC39C439B1A05AA606E50EEC2E0D99BF836337`。

## 6. 最终 Review

- 必须修复：无。
- 建议修复：无。
- 可选优化：后续 Phase 7 可补充人工拔插、设备复位与传感器断开恢复；Phase 8 可生成 HTML/PDF 正式报告，均不属于本任务。

TASK-020 的实现、全新构建、全部 CTest、短时预检、独立中止专项、正式真实 20 条/10 分钟套件、证据审计、阈值恢复与文档同步均已完成。结论只适用于当前约 20 cm 安全低压点对点台架，不外推为 8/24 小时、工业长线、隔离或 EMC 验证。
