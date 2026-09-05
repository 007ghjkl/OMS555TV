# Host Phase 5 自动化测试 UI 与真实 RS485 验收技术规范

> 状态：已评审并实现
>
> 日期：2026-09-05
>
> 适用任务：TASK-016

## 1. 目的与边界

本规范定义自动化测试页面、测试工作流编排和 Phase 5 最小真实 RS485 套件。既有 `TestCaseLoader`、`TestEngine`、`TestResultManager`、`AppStateController` 和 `IModbusClient` 的职责不变；UI 不解析协议、不直接操作串口、不生成执行结论。

本任务不扩展 v1 Schema，不实现 Raw Frame、人工步骤或正式 HTML/PDF 报告。

## 2. 组件职责

- `TestAutomationController`：异步读取并加载 JSON；编排监控停止、Testing owner 获取、引擎启动和可选监控恢复；保存加载状态、结构化配置错误、当前步骤和工作流错误。
- `TestEngine`：增加只读的步骤开始通知，通知包含 case ID、步骤用途、attempt 序号和 RequestId；不改变执行或结果语义。
- `TestResultManager`：仍是运行状态、统计、用例结果和 attempt 证据的唯一来源。
- `MainWindow`：只发送加载、执行、跳过、中止命令，并把上述模型渲染为页面。

## 3. 加载状态与错误

状态为 `IDLE/LOADING/STOPPING_MONITORING/ACQUIRING_TESTING/RUNNING/RESUMING_MONITORING`。

- UI 文件加载在线程池执行；主线程只接收完成结果。
- 单个 JSON 文件最大 4 MiB；不存在、无法读取或超限使用工作流错误显示。
- Loader 配置错误逐项显示稳定错误码、JSON Pointer 路径、suite/case ID 和中文诊断。
- 工作流活动时拒绝换套件和重复开始；加载新套件成功后保留 Loader 给出的 JSON 顺序。

## 4. 执行与模式交接

1. `CONNECTED_IDLE` 可直接请求 `startTesting()`。
2. `MONITORING` 开始测试时先请求 `stopMonitoring()`，只在命令成功且到达 `CONNECTED_IDLE` 后请求 `startTesting()`。
3. 只有 `startTesting()` 成功、应用为 `TESTING` 且 owner 为 `Testing` 后才调用引擎。
4. 引擎负责终态时调用 `stopTesting()`；控制器收到 `runCompleted` 时，应用必须已回到 `CONNECTED_IDLE`。
5. 只有用户勾选“完成后恢复监控”且运行前确实处于 `MONITORING` 时，才使用开始运行时保存的周期和 timeout 显式恢复监控。
6. 交接或恢复失败使用工作流错误展示；不得伪造 TestEngine 用例结果。引擎启动被拒且 Testing owner 已取得时，必须请求释放 owner。

## 5. UI 门禁与展示

- 页面提供文件路径、浏览、加载、执行选中、执行全部、跳过、中止和恢复监控选择。
- 表格保持套件顺序，显示 ID、名称、类别、类型、状态、请求、预期、实际和耗时。
- 详情显示失败/清理原因及所有 attempt 的步骤、序号、RequestId、TX、RX、RTT、状态和结构化错误。
- 当前步骤显示 case ID、步骤、attempt 和 RequestId；统计只由当前 `TestResultManager` 快照计算。
- 运行或交接中禁用连接、断开、监控、配置、加载和重复开始；中止只调用 `TestEngine::abort()`。
- `FAIL`、`ERROR`、`SKIPPED` 使用不同文本，不合并为“失败”。

## 6. Phase 5 最小真实套件

`testcases/functional/phase5-smoke.json` 使用 Slave ID 1、0-based PDU 地址，包含 8 条安全用例：A/B/C/环境温度各自范围读取、光敏毫伏范围读取、Firmware 0.2 序列读取、非法地址期望 0x02、A 相阈值写入 600/独立回读/恢复原值。

v1 Schema 对 `count>1` 只允许精确 `register_sequence`，无法为温度和光敏的五元素动态块表达逐项范围。为避免把随环境变化的数据固化为不可靠期望，本套件以五条连续执行的单寄存器范围断言覆盖同一测量块；不修改 TASK-014 已确认的输入契约。Firmware 使用稳定的双寄存器序列断言。

阈值用例必须 `read_before_write=true`、`restore_original=true`。真机验收在运行前后另行读取四路阈值，证明最终值与基线一致。

## 7. 验证

- offscreen UI：有效/无效加载、状态与详情、全部/选中、跳过、中止、监控交接、可选恢复和操作门禁。
- 全量 CTest：确保 Phase 4 和 TASK-014/015 无回归。
- 真机：COM 端口必须显式传入；60 秒 watchdog；保存会话日志并核对结果、诊断和 TEST 日志中的 RequestId；前后阈值必须一致。
- 本任务只宣称 Phase 5 基础套件完成，不外推 Phase 6/7/8。

任何预计超过 5 分钟的验证步骤均不自动执行，转由用户手动操作。

## 8. 评审结论

- 编排层复用唯一应用状态机和通信 owner，没有第二套串口或协议路径。
- 异步加载、事件驱动执行和中止不会在 UI 主线程等待通信。
- 动态测量值采用独立范围断言，避免不稳定的精确序列期望；Schema 与公开协议不变。
- 写用例具备引擎内恢复和验收工具外部最终回读两层证据。
- 可以进入实现。
