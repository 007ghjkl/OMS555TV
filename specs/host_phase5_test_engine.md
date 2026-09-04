# Host Phase 5 TestEngine 执行核心技术规范

> 状态：已评审并实现
>
> 日期：2026-09-05
>
> 适用任务：TASK-015

## 1. 目的与边界

本文定义规范化 `TestSuite` 的执行语义、处理器边界、状态机、超时/重试、中止、结果证据和 Testing owner 清理。引擎不读取 JSON、不访问 QWidget、QSerialPort、Worker 或 Firmware；所有通信只经过 `IModbusClient`。

TASK-016/UI 负责在需要时先停止监控，并通过 `AppStateController::startTesting()` 进入 `TESTING`。`TestEngine` 只接受已经处于 `TESTING` 且 owner 为 `Testing` 的运行；引擎结束后调用 `stopTesting()`，等待 owner 释放和应用回到 `CONNECTED_IDLE` 后才发布最终完成事件。是否恢复监控由 UI 的明确选择决定，不属于本任务。

## 2. 状态机

### 2.1 Engine

`IDLE -> RUNNING -> ABORTING -> RELEASING -> IDLE`，正常运行可由 `RUNNING` 直接进入 `RELEASING`。

- `IDLE`：可接受一次运行；重复中止返回拒绝。
- `RUNNING`：最多一个用例、一个 Modbus 请求在途。
- `ABORTING`：不再开始新用例或普通请求；取消当前普通请求。若复合写用例已保存原值且写请求曾被接受，仍允许执行恢复步骤。
- `RELEASING`：不接受新请求，调用并等待 `AppStateController::stopTesting()`。
- 销毁时若有在途请求，先请求取消；QObject 连接自动断开。销毁不伪造成功结果。

### 2.2 Suite/Case/Attempt

- Suite：`NOT_RUN -> RUNNING -> PASS|FAIL|ERROR`。空套件为 `PASS`。任一 `ERROR` 使套件为 `ERROR`，否则任一 `FAIL` 使套件为 `FAIL`，否则为 `PASS`；`SKIPPED` 不降低套件状态。
- Case：`NOT_RUN -> RUNNING -> PASS|FAIL|ERROR`；未启用、未选择、用户跳过或中止后的未开始用例为 `SKIPPED`。所有已纳入结果的用例最终只能是 `PASS/FAIL/SKIPPED/ERROR`。
- Attempt：只表示一次被通信层接受并分配 RequestId 的物理请求，终态与完整 `ModbusRequestResult` 一一对应。提交即拒绝不创建 attempt，而写入用例错误。每次 retry 创建新 attempt 和新 RequestId，不覆盖历史。

结果管理器通过复制替换发布只读快照；外部不能修改套件、用例、attempt 或证据。JSON 顺序始终保持。

## 3. 公共命令与选择语义

- `runSuite(suite, selectedCaseIds)` 非阻塞。选择集合为空表示执行所有启用用例；非空时只执行集合内且启用的用例。
- `runCase(suite, caseId)` 等价于单元素选择集合。
- 禁用用例标记 `SKIPPED/Disabled`；未选择用例标记 `SKIPPED/NotSelected`；显式跳过标记 `SKIPPED/UserSelected`。
- 重复开始、未知 case ID、非 `TESTING`、非 Testing owner、未连接均同步拒绝，不改变已有结果。
- 进度、快照和完成信号均由引擎所在应用线程发出。

## 4. 处理器边界

注册表按规范化 `TestCaseType` 创建一次用例级处理器会话。会话只负责：

1. 产生下一条逻辑请求（读/写及步骤用途）；
2. 消费成功响应或远端 Modbus 异常；
3. 调用 TASK-014 断言并产生主结论；
4. 管理 `write_and_verify` 的原值、写入、独立回读和恢复顺序。

引擎统一负责提交、单请求 timeout、总预算、通信错误分类、retry、RequestId/证据、日志、中止和 owner 释放。中心循环不按用例类型分支。

基础处理器：

- `read_registers`（同时执行已规范化的 `read_register`）：一次 0x03，正常响应交给寄存器断言。
- `write_register`：一次 0x06，成功回显的原始值作为单寄存器实际值交给断言。
- `expect_exception`：按配置发 0x03 或 0x06；仅匹配的远端异常码 PASS。正常响应或错误异常码为 FAIL，本地通信错误为 ERROR。
- `write_and_verify`：可选写前读（恢复启用时必有）、0x06 写、独立 0x03 回读、主断言、可选 0x06 恢复。

## 5. Timeout 与 Retry

- 每次提交的响应 timeout 为 `min(request_ms, case 剩余预算)`，不得小于 1 ms。
- 用例预算从进入 `RUNNING` 计至主流程和必要清理结束，使用注入时钟判定；测试使用手动虚拟时钟，不调用 `sleep()`。
- 开始任一步骤或 retry 前若预算耗尽，不再提交，当前用例为 `ERROR/CaseTimeout`。恢复步骤是唯一例外：为降低设备残留风险，可再使用一次 `request_ms` 的独立清理预算，并记录预算越界附加错误。
- 默认零重试。仅当通信错误类别映射到配置的 `timeout/connection/serial/crc/protocol` 时重试当前步骤，最多 `max_retries` 次。
- 断言 FAIL、远端异常、提交拒绝、中止、owner/参数/队列/内部错误不重试。
- retry 只重做失败步骤；`write_and_verify` 已完成的前序步骤不重复。

## 6. 优先级与分类

固定优先级从高到低：

1. 恢复/owner 释放失败和内部不变量错误：`ERROR`；
2. 用户中止：当前用例 `ERROR/Aborted`，未开始用例 `SKIPPED/Aborted`；
3. 用例总预算或请求 timeout、其他本地通信错误：`ERROR`；
4. 已收到可判定的正常响应或远端异常但断言不满足：`FAIL`；
5. 断言满足且无清理错误：`PASS`。

迟到、未知或已完成 RequestId 的回调必须忽略并记录附加错误，不得改变已发布终态。

## 7. write_and_verify 合并规则

1. `read_before_write=true` 时先读取并保存原值；失败则不写、不恢复，主结果为 ERROR。
2. 写请求成功后必须独立回读；0x06 回显不能代替回读。
3. 回读断言决定主结果 PASS/FAIL；写或回读通信失败决定主结果 ERROR。
4. `restore_original=true` 且原值已保存时，只要写请求曾被通信层接受，无论其后主断言 PASS、FAIL、ERROR 或用户中止，均尝试一次恢复写；恢复不参与 retry，防止重复不确定写。
5. 恢复成功保留原主结果；恢复失败把最终结果提升为 `ERROR/CleanupFailed`，并同时保存主结果、清理错误和恢复 attempt 证据。
6. 中止发生在恢复步骤时不取消恢复；完成恢复后结束。

## 8. 结果模型

Suite 结果至少保存：运行 ID、不可变套件副本、状态、开始/结束 UTC、耗时、是否中止、用例顺序和附加错误。

Case 结果至少保存：ID、声明/规范化类型、状态、跳过原因、期望断言、最终实际、断言差异、主错误、清理错误、开始/结束 UTC、耗时和全部 attempt。

Attempt 至少保存：全局序号、步骤用途、步骤内 attempt 序号、是否 retry、retry 触发原因、RequestId、开始/结束 UTC、耗时和完整不可变 `ModbusRequestResult`（含 TX/RX、RTT、CRC 和结构化错误）。

## 9. TEST 日志

引擎向现有 `SessionLogService` 追加 `suite_started/case_started/request_attempt/case_finished/suite_finished` TEST 事件。事件引用 suite/case/run/RequestId/status；TX/RX 仍来自通信诊断自动记录，引擎不重新序列化缓冲区。

日志写入失败不改变测试断言和主状态，只追加结构化 `LoggingFailed` 错误。日志服务未激活时内存 TEST 事件仍可用，不视为失败。

## 10. 评审结论

- 状态均有唯一稳定终态，FAIL/ERROR/SKIPPED 不混用。
- AppStateController 是唯一 owner/mode 交接入口；引擎没有第二套所有权状态机。
- timeout、retry、abort、主断言和 cleanup 的优先级无未决项。
- `write_and_verify` 对不确定写、中止和恢复失败采取保守清理规则。
- 每个已接受 RequestId 恰好对应一个 attempt，结果足够支持 TASK-016 UI 和后续报告。
- 不改变现有通信协议、线程模型和公开 Modbus 能力，可进入实现。
