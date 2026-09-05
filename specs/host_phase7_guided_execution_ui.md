# Host Phase 7 引导式执行协调与 UI Technical Spec

## 1. 状态

- 版本：1.0
- 日期：2026-09-05
- 对应任务：TASK-022
- 评审结论：通过，无阻塞未决项

## 2. 目标与边界

本 Spec 在 TASK-021 的 Schema v3 与纯模型之上增加可执行协调层和 QWidget 页面交互。实现必须复用现有 `AppStateController`、`TestEngine`、唯一 `IModbusClient`、`TestResultManager` 与 `SessionLogService`，不得创建第二个通信客户端或绕过 Testing owner。

v1/v2 仍由 `TestAutomationController -> TestEngine::runSuite()` 执行，行为不变。v3 只允许 `guided_recovery`，由 `TestAutomationController -> GuidedTestCoordinator` 执行。两条路径在同一控制器入口互斥。

## 3. 模块职责与所有权

### 3.1 TestAutomationController

- 负责异步加载套件、停止此前监控、取得 Testing owner、选择 v1/v2 或 v3 执行路径，以及可选恢复此前监控。
- v3 开始后把套件和选择集交给协调器，不解释人工步骤，也不发 Modbus 请求。
- v3 协调器完成且已经释放 Testing owner 后，才进入恢复监控或工作流结束。
- `skipCase` 仅用于 v1/v2 尚未开始用例；v3 运行中禁用。

### 3.2 GuidedTestCoordinator

- 是不依赖 Qt Widgets 的 `QObject`，只在应用线程接收动作和处理状态迁移。
- 管理 v3 套件、当前用例、四个固定步骤、一次性 token、人工 deadline、观察 deadline、探测间隔、连续计数、恢复耗时、不可变快照、TEST 日志和 Testing owner 释放。
- 不直接持有或调用 `IModbusClient`。
- 通过注入的 `IMonitorScheduler` 获取 UTC、单调时间和安排回调；生产使用 `QtMonitorScheduler`，测试使用 `ManualMonitorScheduler`。

### 3.3 TestEngine

- 保留既有自动套件执行器职责。
- 新增互斥的引导探测入口：仅在 Engine 空闲、应用为 TESTING、Testing owner 已取得且没有其他引导探测时接受一个 0x03 读请求。
- 探测仍由 Engine 构造 `RequestOptions` 并提交给其唯一 `IModbusClient`；完成后以携带 run/case/step/RequestId 的信号返回协调器。
- 普通套件运行与引导探测互斥；引导探测不取得或释放 owner，不发布套件结果。
- 取消只针对当前匹配 RequestId；迟到或不匹配完成事件不会推进协调器。

### 3.4 UI

- UI 只读取控制器暴露的只读引导视图并提交 `confirmGuidedAction(token)`、`cancelGuidedAction(token)` 或整体 `abort()`。
- UI 不直接访问 TestEngine、调度器或通信客户端，不根据点击结果判定用例状态。

## 4. 信号方向

1. UI -> TestAutomationController：加载、执行、确认、取消、中止。
2. TestAutomationController -> AppStateController：停止监控、取得 Testing owner；v1/v2 后续清理由 Engine 完成。
3. TestAutomationController -> GuidedTestCoordinator：开始 v3、提交动作、中止。
4. GuidedTestCoordinator -> TestEngine：提交或取消单个引导读探测。
5. TestEngine -> GuidedTestCoordinator：探测接受结果和异步完成结果。
6. GuidedTestCoordinator -> TestResultManager/SessionLogService：发布快照与追加 TEST 事件。
7. GuidedTestCoordinator -> AppStateController：终态后释放 Testing owner。
8. GuidedTestCoordinator -> TestAutomationController -> UI：状态、视图和完成通知。

所有对象位于应用线程。通信实现可在内部线程工作，但跨线程完成通过 Qt queued signal 返回；动作校验和状态迁移只在协调器线程执行。

## 5. 执行状态机

协调器对单个用例使用 TASK-021 `GuidedRunState`：

`Idle -> WaitingForDisconnectConfirmation -> ObservingOutage -> WaitingForReconnectConfirmation -> ObservingRecovery -> Finished`

套件执行规则：

- disabled 与未选择用例在初始化时标记 SKIPPED。
- 当前用例终态为 PASS 时继续下一条可执行用例。
- 当前用例为 FAIL、ERROR 或操作员取消 SKIPPED 时结束套件，剩余可执行用例标记 SKIPPED；不要求操作员在物理状态不确定时继续。
- 协调器进入逻辑终态后停止定时器、取消在途探测、拒绝后续动作，随后释放 Testing owner；释放完成后发布 terminal 快照并发出完成信号。
- 释放失败记录到 `auxiliaryErrors`，套件状态提升为 ERROR，但不得覆盖用例原始终态和恢复提醒。

## 6. 人工动作与 token

- 每次展示人工提示时生成非空 UUID token，并创建 `GuidedActionContext(runId, caseId, stepId, token, allowedActions)`。
- 命令必须同时匹配 run/case/step/token，且上下文正在等待、token 未消费、动作在允许列表中。
- 第一个合法命令在任何状态迁移前原子地消费 token、取消人工 deadline 并写日志。
- stale、重复、跨运行、错误步骤或错误 token 返回明确拒绝原因，不迁移、不重复写动作日志。
- `confirm` 只开始后续观察；`cancel` 产生 `operator_cancelled`，用例为 SKIPPED，并根据是否已观察到稳定恢复设置恢复提醒。
- 人工 deadline 到期产生 `operator_timeout` 和 ERROR。整体中止产生 `user_aborted` 和 ERROR。

## 7. 调度、deadline 与倒计时

- 所有 duration/deadline 使用单调时钟；审计时间使用 UTC。
- 提示展示时记录人工绝对 deadline；观察开始时记录观察绝对 deadline。
- 倒计时是 `max(0, deadline - monotonicNow)` 的派生只读值；UI 刷新不改变状态。
- 协调器为 deadline 安排一次回调。每次状态切换增加 generation 并取消旧任务，迟到 generation 不生效。
- 观察首个探测在进入观察状态时立即提交。每次完成后，仅在未达到目标且 deadline 未到时，从“本次完成时刻”安排一个 interval 后的下一次探测；禁止 catch-up 和并发探测。
- 若 interval 到点时 deadline 已到，直接结束观察，不再提交请求。
- request timeout 为 `min(case.request_ms, observation deadline 剩余量, case 总预算剩余量)`，且至少 1 ms。

## 8. 观察判定

### 8.1 中断观察

- 只有 `ErrorCode::ResponseTimeout` 计为 Match。
- 合法响应为 NonMatch 并将连续计数清零。
- 调用方取消为 Aborted；CRC、协议、串口、连接、远端异常及其他错误为 FatalCommunicationError。
- 达到连续次数后进入重连提示；deadline 到期而未达到时为 `outage_not_detected`、FAIL。

### 8.2 恢复观察

- 只有合法 0x03 响应，且可选业务断言通过时计为 Match。
- 合法响应但业务断言不通过为 NonMatch，并将连续计数清零。
- ResponseTimeout 为 NonMatch，并将连续计数清零；其他错误同上按致命或取消分类。
- 首个 Match 保存 firstSuccessfulResponse 时刻与 RequestId；连续计数被重置后保留首个成功时刻，用于“首次响应”指标。
- 达到连续次数保存 stableRecovery 时刻与 RequestId，并由单调时钟计算“首次成功耗时”和“稳定恢复耗时”，用例 PASS。
- deadline 到期为 `recovery_timeout`、FAIL。

## 9. 证据与结果

- 每次探测转换为 `TestRequestAttemptResult`，保存 sequence、stepId、RequestId、UTC、duration 和完整 `ModbusRequestResult`；后者包含 TX ADU、RX ADU、CRC 与 RTT 证据。
- 每个观察步骤生成 `GuidedObservationResult`，保存目标、所需/已达连续数、结果、首个/稳定 RequestId、探测列表及错误。
- 人工步骤生成 `GuidedOperatorActionRecord`，保存 prompt 时间、token、动作时间、等待耗时和可选说明。
- 用例 `guidedRecovery` 保存终态原因、观察、恢复耗时、`physicalLinkRestored` 与恢复提醒。
- `physicalLinkRestored` 只在恢复观察达到稳定连续成功后为 true；用户声称已接回不能单独设置为 true。
- 快照由 `TestResultManager` 发布，运行期间为非 terminal；owner 释放结果合并后只发布一次 terminal 历史记录。

## 10. 日志

协调器以 `LogLevel::Test`、module=`testing` 写入：

- `prompt_shown`
- `operator_confirmed`
- `operator_cancelled`
- `observation_started`
- `observation_finished`
- `guided_case_finished`

元数据至少包含 run_id、suite_id、case_id、step_id、token（人工事件）、state/status/reason；观察结束包含 target、连续进度和相关 RequestId。每次探测另写 `request_attempt`，RequestId 与结果证据一致。

日志写入失败不改变原始 FAIL/ERROR 为 PASS；在结果 `auxiliaryErrors` 记录 `LoggingFailed`。清理仍继续。

## 11. UI 契约

自动化页新增引导区域，使用稳定 objectName：

- `guidedPanel`
- `guidedPromptTitleLabel`
- `guidedInstructionLabel`
- `guidedSafetyLabel`
- `guidedCountdownLabel`
- `guidedObservationProgressLabel`
- `guidedRecoveryTimingLabel`
- `guidedConfirmButton`
- `guidedCancelButton`
- `guidedRestorationReminderLabel`

显示规则：

- v1/v2 或未运行 v3 时区域隐藏；v3 等待、观察及 v3 终态可见。
- 人工等待显示标题、说明、安全提示、倒计时和允许动作；只有等待且 token 有效时确认/取消可用。
- 观察显示目标、当前连续数/所需数、剩余时间和当前 RequestId；按钮不可确认，整体中止仍可用。
- 恢复阶段显示首次成功与稳定恢复耗时，未知值显示 `--`。
- 取消、人工超时、中止、恢复超时或致命错误且未观察到稳定恢复时显示醒目的接线恢复提醒，不使用“已安全恢复”措辞。
- 详情页显示人工记录、观察结果、每个 RequestId、TX/RX、RTT 和最终原因。
- 工作流 busy 期间沿用既有连接、监控、配置和调试门禁；页面重绘和详情选择不受阻塞。

## 12. 错误和清理优先级

终态原因沿用 TASK-021：

`internal_error > user_aborted > operator_cancelled > fatal_communication_error > operator_timeout > outage_not_detected > recovery_timeout > pass`

状态映射沿用 TASK-021。多个近同时事件只由第一个成功取得当前 generation 的处理路径迁移；若必须合并原因，使用上述优先级。逻辑终态一旦确定不得被迟到探测改写。

## 13. 生命周期

- `abort()` 在人工等待时直接终止；观察时先使 generation 失效并取消在途 RequestId，再终止。
- 协调器销毁时取消所有 scheduler task 和在途探测；若仍持有 Testing owner，则请求 `stopTesting()`，不启动新请求。
- 窗口关闭只销毁视图，不直接改通信状态；应用对象析构顺序必须让协调器早于 Engine、AppState 和 client 析构。

## 14. 验证计划

- 核心测试：成功、未检测中断、恢复超时、业务断言重置、致命错误、人工取消、人工超时、中止、销毁。
- token 测试：stale、重复、跨运行、错误 step、终态后命令。
- 调度测试：无 catch-up、严格串行、deadline、迟到回调、人工等待零请求。
- 证据/日志测试：动作、观察、RequestId、TX/RX/RTT、恢复耗时、附加日志错误、owner 释放。
- offscreen UI：提示、安全说明、倒计时、进度、恢复耗时、全部终态、按钮和全局门禁。
- 回归：全新配置、完整构建、全部 Host CTest、`oms555tv_host --smoke-test`。

## 15. 评审记录

### 必须修复

无。既有 TestEngine 结束时自释放 owner，因此禁止把引导探测实现为多个普通 suite；本 Spec 通过 Engine 的互斥单探测入口解决该冲突。

### 建议修复

无。

### 可选优化

后续 TASK-023 可在不改变本状态机的前提下增加正式 Phase 7 套件和人工实板验证记录。
