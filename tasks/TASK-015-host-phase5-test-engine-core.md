# TASK-015 Host Phase 5 TestEngine 执行核心

## 目标

基于 TASK-014 的规范化用例模型，实现无 QWidget、可由 Fake 确定验证的 TestEngine、基础测试处理器和不可变结果管理，支持单条/批量执行、跳过、中止、超时、显式重试和自动 PASS/FAIL 判定。

## 背景

Phase 4 已完成 `AppStateController`、Monitor owner、Testing owner 预留、通信诊断和会话日志。进入测试模式时必须先停止监控、等待或取消在途请求、取得 Testing owner，测试结束后释放通道。当前 AppStateController 已有 `startTesting()`/`stopTesting()`，但没有用例调度、处理器、断言、重试、结果或中止实现。

本任务先用 `FakeModbusClient` 固化执行语义和所有权清理，真实 RS485 与自动化测试页面留给 TASK-016。

## 范围

- 实现前创建并评审 `specs/host_phase5_test_engine.md`。
- 实现 `TestEngine`，消费 TASK-014 的不可变 `TestSuite`/`TestCase`。
- 实现 `TestResultManager`，保存不可变套件、用例和每次 attempt 结果。
- 实现处理器注册边界，使基础类型通过独立处理器扩展，不在中心循环堆积类型分支。
- 实现规范化后的 `read_registers`、`write_register`、`write_and_verify` 和 `expect_exception` 四类基础处理器；`read_register` 由 Loader 校验并规范化为 count=1 的读取语义。
- 所有通信通过 `IModbusClient`、Testing owner 和现有串行队列执行。
- 支持执行单条、执行全部启用用例、跳过禁用/用户选择用例和保持 JSON 顺序。
- 支持单请求 timeout、复合用例总预算和显式 retry；每次重试使用新 RequestId 并保留独立证据。
- 支持中止：停止接收新用例，取消当前请求，按 Spec 完成当前/剩余状态，并清理 Testing owner。
- 区分 PASS、FAIL、SKIPPED 和 ERROR；NOT_RUN/RUNNING 只用于执行过程。
- 保存期望、实际、断言差异、失败原因、开始/结束时间、耗时、所有 attempt 和完整 TX/RX 证据。
- 把 TEST 级执行事件写入现有 SessionLogService，不重新序列化通信缓冲区。
- 使用 Fake 和手动调度器验证成功、失败、异常、超时、重试、中止和所有权交接。

## 非范围

- 不实现 QWidget 自动化测试页和真实 RS485 执行。
- 不实现完整 20 条 Phase 6 套件、稳定性长循环或半自动人工步骤。
- 不实现公共 Raw Frame、错误 CRC 注入、0x04 或 0x10。
- 不实现 HTML/PDF 报告；结果模型只为后续报告提供不可变输入。
- 不修改 Monitor、ConfigurationService、Firmware 或通信线程模型。

## 依赖

- TASK-014 测试模型、Loader、Schema 和断言核心已完成。
- TASK-011 `AppStateController` 和 Testing owner 状态边界已完成。
- TASK-007/008 `IModbusClient`、Fake、取消、超时和事务证据已完成。
- TASK-013 SessionLogService 与 TEST 日志级别已完成。

## 实现门禁

1. Spec 必须定义 engine/suite/case/attempt 状态机和每个状态的唯一终态。
2. Spec 必须定义监控停止、Testing owner 获取、执行、owner 释放和可选恢复监控的责任边界。
3. 中止、timeout、retry、通信 ERROR 和断言 FAIL 的优先级必须固定。
4. `write_and_verify` 必须定义写前读取、写、回读、恢复原值和清理失败的结果合并规则。
5. 每个已接受 RequestId 必须恰好对应一个 attempt 证据；重试不得覆盖先前失败。
6. 报告模型虽不在本任务实现，但结果必须不可变且足以支持后续汇总和明细。

## 实现要求

1. Engine 公共命令非阻塞，完成、进度和当前用例通知在应用线程发出。
2. 只有 AppState 为 `TESTING` 且 client owner 为 Testing 时才能开始执行。
3. 批量执行严格按规范化用例顺序；同一时刻最多一个用例和一个 Modbus 请求在途。
4. 默认不重试；显式重试每次产生新 RequestId、独立时间和证据，并记录触发原因。
5. 断言不满足为 FAIL；配置/处理器/通信/内部错误按 Spec 分类为 ERROR。
6. `expect_exception` 只有收到匹配异常码才 PASS；正常响应、错误异常或本地通信错误均不能误判成功。
7. 中止后不启动新用例，当前请求受控取消；剩余用例按 Spec 标记并保留中止原因。
8. `write_and_verify` 真正执行独立写和读，不以 0x06 回显替代回读；启用恢复时无论主断言成败都执行受控清理。
9. 日志引用 Result/RequestId，不重新解释 TX/RX；日志失败不改变测试断言结果，但进入结构化附加错误。
10. Engine 不持有 QWidget，不直接访问 QSerialPort、Worker 或 Firmware。

## 验收标准

1. Technical Spec 已评审通过，状态、retry、abort、cleanup 和结果模型无未决项。
2. Host 全新配置、构建和全部 CTest 通过，无监控、配置和日志回归。
3. 单条和批量执行保持 JSON 顺序，禁用/用户跳过用例得到稳定 SKIPPED 结果。
4. 四种基础处理器均由 Fake 覆盖 PASS、FAIL 和 ERROR 路径。
5. timeout 和允许重试场景产生独立 attempt/RequestId；最终结果保留全部证据。
6. 中止 queued/in-flight/复合用例时不再启动新请求，所有用例进入允许的终态。
7. `write_and_verify` 覆盖成功、写失败、回读不一致、恢复成功和恢复失败。
8. Testing owner 在正常、失败、中止和日志错误后均可受控释放，应用返回 `CONNECTED_IDLE`。
9. 结果保存期望、实际、差异、失败原因、时间、耗时、attempt 和完整 TX/RX。
10. 同一套 Fake 脚本可重复执行并得到相同顺序与结果，不依赖真实 `sleep()`。
11. TEST 日志与结果中的用例 ID、RequestId 和状态一致。
12. 最终 Review 无必须修复项，允许进入 TASK-016。

## 测试要求

- Engine 状态：单条、批量、空套件、重复开始、正常完成、中止和销毁。
- 处理器：四种基础类型的成功、断言失败、通信错误和边界。
- 重试/超时：零重试、多 attempt、新 RequestId、总预算耗尽和最终分类。
- 清理：写回读恢复、Testing owner 释放、SessionLog 错误和迟到回调。
- 结果：状态、顺序、时间、实际值、差异、证据和不可变性。
- 回归：全部 Host CTest 和应用 smoke test。

## 当前状态

待实施。任务文档已于 2026-09-04 创建，尚未派发；必须等待 TASK-014 完成。
