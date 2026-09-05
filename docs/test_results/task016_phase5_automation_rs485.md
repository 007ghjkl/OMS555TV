# TASK-016 自动化测试 UI 与真实 RS485 验收记录

> 日期：2026-09-05
>
> 结论：通过
>
> 对应规范：`specs/host_phase5_automation_ui.md`

## 1. 验证范围

本次验证覆盖异步 JSON 套件加载、结构化配置错误、测试工作流编排、自动化测试页面、选中/全部执行、跳过、中止、可选恢复监控、模式门禁、结果与 attempt 详情，以及 Phase 5 基础真实 RS485 套件。

不包含 Phase 6 的完整 20 条分类套件、Phase 7 人工步骤、Phase 8 HTML/PDF 报告、Raw Frame、CRC 错误注入或长时稳定性。

## 2. 环境

- Host：Windows，Qt 6.8.3 `msvc2022_64`，MSVC 19.51，CMake + Ninja，Debug。
- DUT：NUCLEO-F411RE / STM32F411RET6，Firmware 0.2，USART1，115200 8N1，Slave ID 1。
- 物理链路：COM6 MacroSilicon USB Serial Ports（VID 345F/PID 3020）、自动换向 TTL-RS485 模块、约 20 cm 安全低压点对点台架。
- 全新构建目录：`build-host-task016`。
- 套件：`testcases/functional/phase5-smoke.json`，8 条用例。

## 3. 自动化构建与测试

全新配置、完整构建成功，Host CTest **19/19 通过**，最终一次全量回归总耗时 8.11 秒。新增 `host.testing.automation_ui` offscreen 测试耗时 1.61 秒，覆盖：

- 有效真实套件与无效 JSON/JSON Pointer 错误展示；
- PASS、FAIL、ERROR、SKIPPED 分离显示；
- 执行全部、执行选中、用户跳过、在途请求中止；
- 当前 case/步骤/attempt/RequestId 和完整 TX/RX/RTT 详情；
- `MONITORING -> STOPPING -> CONNECTED_IDLE -> TESTING -> CONNECTED_IDLE -> MONITORING` 的显式恢复路径；
- 运行期间连接、监控、配置和诊断清空门禁；
- TestResultManager、诊断与 TEST 日志的 RequestId 一致性。

测试使用 Fake Modbus Client、手动调度器和 offscreen Qt，不调用 `sleep()`。

## 4. 真实 RS485 结果

执行命令：

```powershell
$env:Path = "D:\Dev\Qt\6.8.3\msvc2022_64\bin;$env:Path"
.\build-host-task016\task016_rs485_validation.exe --port COM6 --suite testcases\functional\phase5-smoke.json --timeout-ms 500
```

工具包含 60 秒 watchdog，实际总耗时约 1 秒。结果如下：

| 用例 | 实际值 | 状态 |
|---|---:|---|
| P5-TEMP-A-RANGE | 260（26.0 ℃） | PASS |
| P5-TEMP-B-RANGE | 261（26.1 ℃） | PASS |
| P5-TEMP-C-RANGE | 262（26.2 ℃） | PASS |
| P5-TEMP-AMBIENT-RANGE | 250（25.0 ℃，模拟源） | PASS |
| P5-LIGHT-RANGE | 2766 mV | PASS |
| P5-FIRMWARE-VERSION | `[0, 2]` | PASS |
| P5-ILLEGAL-ADDRESS | Modbus exception 0x02 | PASS |
| P5-THRESHOLD-RESTORE | 回读 600，随后恢复原值 | PASS |

套件结果为 **8/8 PASS**。共有 11 个 Testing owner 请求：7 个读取/异常请求，以及阈值用例的写前读、写、独立回读和恢复写。每个请求均保存非空 TX/RX；结果模型、通信诊断和 TEST `request_attempt` 日志的 11 个 RequestId 集合完全一致。

## 5. 阈值恢复与日志证据

- 测试前四路阈值：60.0/60.0/60.0/40.0 ℃。
- A 相测试写入值：60.0 ℃；完成独立回读后仍执行恢复写。
- 测试后独立读取：60.0/60.0/60.0/40.0 ℃，与基线一致。
- 会话日志：`output/logs/2026-09-05/session-20260905-000303-293-75356de8-74e5-4d5b-a45f-12eac46e7873.jsonl`。
- 日志 45 行、15,554 字节；`request_completed` 13 条（测试前后阈值读取各 1 条、Testing owner 11 条），TEST 级别事件 30 条，其中 `request_attempt` 11 条。
- SHA-256：`13319FC7C7125F23AD6C0FE078CBF9E14BC9E305EFCCF8209D4839E73ED79B2F`。

日志目录由 `.gitignore` 排除，不提交运行产物；本记录保存可复核摘要与哈希。

## 6. Schema 约束说明

v1 Schema 对 `count>1` 只允许精确 `register_sequence`。实时温度与光敏随环境变化，不能安全固化为五元素精确序列，因此 Phase 5 套件用五条连续的单寄存器范围断言覆盖地址 0～4。Firmware 地址 39～40 为稳定序列断言。此选择不修改 TASK-014 输入契约，也不把五条单读冒充为 `TC-F002` 多寄存器动态范围用例。

## 7. 最终 Review

### 必须修复

无。

### 已在 Review 中修复

- 修复连接完成后自动化页面门禁未随 ViewModel 刷新，导致执行按钮仍禁用的问题。
- 保留多行选择，避免运行快照刷新后“执行选中”的选择集合丢失。
- 配置操作繁忙时禁止开始测试；测试运行期间禁止连接、监控、配置和诊断清空操作。
- 新套件加载后隐藏旧运行快照，确保初始状态显示为 NOT_RUN。

### 可选后续优化

- Phase 6 如需一次读取多个动态测量量并分别做范围断言，应通过新 Schema 版本评审引入逐元素断言，不能静默改变 v1。

## 8. 结论

TASK-016 的 Technical Spec、实现、offscreen 自动测试、全新 Host 构建、真实 RS485 8 条基础套件、阈值最终恢复、证据一致性和最终 Review 均已完成。Phase 5“至少可以执行 5 个自动化测试”的验收通过；Phase 6/7/8 仍未实施。
