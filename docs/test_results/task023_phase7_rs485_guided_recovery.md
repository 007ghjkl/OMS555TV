# TASK-023 Phase 7 RS485 断线恢复半自动验证记录

> 日期：2026-09-06
>
> 结论：通过；TASK-023 完成，Phase 7 关闭。

## 1. 验证对象与环境

- 源码基线：`20533a11b24e50c153a4aba77c11992b67fc5556` 加 TASK-023 工作区修改。
- 正式套件：`testcases/phase7/phase7-rs485-disconnect-recovery.json`，SHA-256 `1ACB3CEF6D5F21124F682D19A703EBEB0F63AE2DABBC752F39888164ACA39AEE`。
- 验收程序：`task023_rs485_validation.exe`，正式运行 SHA-256 `DEE9B19057A0351B58103303B1313820A0C166B095796D230629C7A15147744D`。
- Host：Windows、MSVC 19.51.36256.0、Qt 6.8.3、CMake 4.4.3、Ninja 1.13.2，Debug 构建。
- DUT：NUCLEO-F411RE，Firmware 0.2；USART1、MAX13487EESA 自动换向 TTL-RS485 模块、约 20 cm A/B、公共地，安全低压台架。
- 运行时端口：COM6，MacroSilicon USB Serial Ports，VID `345F`、PID `3020`、序列号 `A02001JS`；另枚举 COM3 ST-LINK VCP，但未选择。
- 串口：115200、8N1、无流控、Slave ID 1、请求超时 500 ms。
- 人工范围：只断开和恢复两个 RS485 模块之间的 A/B；USB、开发板供电和公共地全程保持连接，未改变极性、终端、偏置或线长。

## 2. 实现与安全门禁

- 新增 `specs/host_phase7_rs485_guided_recovery.md`，明确四步 v3 契约、预检边界、恢复时间口径、清理与证据要求。
- 新增唯一正式用例 `TC-R001`：中断要求连续 3 次结构化 `ResponseTimeout`；恢复要求连续 3 次合法读取 PDU 40，且 Firmware minor 等于 2。
- 恢复 deadline 为 4900 ms，稳定恢复耗时必须严格小于 5 秒才能由协调器判定 PASS。
- 新增可见 GUI 工具 `task023_rs485_validation`，复用生产 MainWindow、监控、状态机、唯一 QSerialPort 客户端、自动化控制器、TestEngine、诊断和 SessionLog；没有隐藏 close/open、自动重连或第二客户端。
- `preflight` 和 `full` 使用独立进程与会话；两者均在运行时严格核对端口身份、套件目录、Firmware 0.2 和完整五读块快照。

## 3. 全新构建与自动测试

使用全新目录 `build-host-task023`：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -SkipAutomaticLocation
cmake -S host/qt -B build-host-task023 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH='D:\Dev\Qt\6.8.3\msvc2022_64'
cmake --build build-host-task023 --parallel
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir build-host-task023 --output-on-failure
```

结果：配置成功，153 步全新构建成功；最终 23/23 CTest 通过，总测试时间 9.10 秒。新增 `host.testing.phase7_suite` 验证唯一正式目录、四步顺序、只读探测、连续数、Firmware 业务断言和 `< 5 秒` 稳定恢复门槛；其余测试覆盖 v1/v2/v3 Loader、guided 模型/协调器、TestEngine、Phase 5/6、offscreen UI 和应用 smoke。

文档同步后的一次完整复跑暴露出既有 `tst_phase6suite` 把 `docs/test_cases.md` 全文所有 `TC-*` 表格都计作 Phase 6，并固定要求 24 行；新增独立 Phase 7 表格后该断言失败。测试已改为只截取 Phase 7 标题之前的 Phase 6 章节，继续严格核对原 24 条目录；增量构建后最终完整复跑全部通过。该失败没有访问硬件，也没有改变正式实机会话结论。

## 4. 开发尝试与独立预检

首次从不含 Qt 运行库 PATH 的独立 shell 启动时，程序在进入验收逻辑前以代码 1 退出，没有应用输出、会话文件或串口访问；该环境启动失败不计为硬件结果。补齐进程 PATH 后执行独立 `preflight`：

- 端口身份、正式套件目录、连接和一次生产监控批次通过；5/5 读块成功，Firmware 为 0.2。
- owner 正常释放，应用最终断开；未要求或执行 A/B 断线。
- 会话 `0a6d09c3-7356-48a4-a6f1-643fcfbaf615`，持续 372 ms。
- 日志：`output/logs/2026-09-06/session-20260906-001335-397-0a6d09c3-7356-48a4-a6f1-643fcfbaf615.jsonl`，3375 字节、8 行，SHA-256 `6074061AEF939D37F5F74FF81F712D63CC404FB8F8CB479B0C971F851029CBAF`。

预检仅证明正式操作前链路在线，不并入下节 `TC-R001` PASS 统计。

## 5. 正式半自动实机结果

最终正式结论只引用 `full` 会话 `ec22d497-b478-463b-96d0-242681f9412b`。会话从 `2026-09-06T00:32:11.219Z` 至 `00:33:38.822Z`，持续 87603 ms。

### 5.1 人工动作

| 步骤 | 提示 UTC | 确认 UTC | 等待 | 一次性 token |
|---|---|---|---:|---|
| 只断开 RS485 A/B | 00:32:11.618 | 00:32:43.113 | 31495 ms | `572af692-8e10-4b93-b78c-b8a2efad10c3` |
| 按原极性恢复 A/B | 00:32:45.208 | 00:33:37.671 | 52463 ms | `67bd2f6e-53bc-4a6f-b76a-e2e63e480a5c` |

两个 token 均只消费一次，动作均为 confirm。人工确认只启动观察，没有直接设置 PASS。

### 5.2 自动观察与恢复时间

- 中断观察 RequestId 6、7、8：3/3 均为结构化 `ResponseTimeout`；TX 均为 `01 03 00 28 00 01 04 02`，RTT 分别约 493、501、491 ms，没有合法响应帧。
- 恢复观察 RequestId 9、10、11：3/3 均为合法 0x03 响应；TX 同上，RX 均为 `01 03 02 00 02 39 85`，Firmware minor 断言等于 2，CRC 与 RTT 完整。
- 恢复 RTT 约为 9.5、10.9、13.2 ms。
- 从重连确认到首个合法业务响应为 51 ms，到连续 3 次合法响应完成为 716 ms；稳定恢复严格小于 5000 ms。
- `TC-R001` 与套件状态均为 PASS，`physicalLinkRestored=true`，无恢复提醒、无 auxiliary error。

### 5.3 跨层证据和清理

- 不可变结果包含 6 个唯一 Testing RequestId；诊断、communication `request_completed` 和 TEST `request_attempt` 各 6 个，集合完全一致。
- TEST 日志包含两组 `prompt_shown`/`operator_confirmed`、两组 `observation_started`/`observation_finished` 和 `guided_case_finished/pass`。
- UI 心跳 868 次，最大迟到 138 ms，小于 1000 ms 门槛。
- 引导终态后 Testing owner 释放，应用返回 `CONNECTED_IDLE`；随后生产监控再次完成 5/5 读块并确认 Firmware 0.2，最终 owner 为 None 且应用断开。
- 正式日志：`output/logs/2026-09-06/session-20260906-003211-219-ec22d497-b478-463b-96d0-242681f9412b.jsonl`，13067 字节、34 行，SHA-256 `0A2B032DF6F7315435240242B6DD157FF96201C81766DE54D156934433CC39E3`。

## 6. 验收追踪

TASK-023 的 Spec/套件/安全说明、全新构建、全部 CTest、在线预检、两次人工确认、连续中断与恢复证据、稳定恢复阈值、跨层 RequestId、最终在线读取、owner 清理和日志哈希均已满足。取消、人工超时、恢复超时和致命错误继续由 TASK-022 的 Fake/offscreen 自动测试覆盖，未在实机重复制造失败。

## 7. 最终 Review

- 必须修复：无。
- 建议修复：无。
- 可选优化：后续可增加 STM32 Reset、传感器断开或人工输入变化；Phase 8 可消费现有不可变结果生成 HTML/PDF 报告。

结论只适用于当前安全低压、约 20 cm 点对点台架。测试没有拔插 USB-RS485 转换器，也没有验证 COM 端口自动重连、工业长线、隔离、EMC、8/24 小时或带电高压环境。
