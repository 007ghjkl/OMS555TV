# TASK-016 Host Phase 5 自动化测试 UI 与真实 RS485 验收

## 目标

将 TASK-014/015 的测试套件、Loader 和 TestEngine 接入 Qt 自动化测试页面，通过真实 RS485 加载并执行至少 5 条安全、可恢复的 JSON 用例，完成 Phase 5 的可演示闭环。

## 背景

PRD 建议自动化测试页包含套件/用例树、详情、当前步骤、预期、实际、执行日志、原始报文和统计。Phase 5 的最低验收是至少执行 5 个自动化测试，而完整 20 条分类套件属于 Phase 6。

本任务只验证基础引擎、模式交接和 UI，不提前实现半自动人工步骤或正式 HTML 报告。

## 范围

- 在 MainWindow 中增加自动化测试页面，不破坏监控、配置和通信调试页面。
- 支持从 `testcases/` 选择/加载 JSON 套件，并显示 Schema/配置错误。
- 显示套件和用例列表、状态、类别、类型、请求、预期、实际、失败原因和耗时。
- 显示当前用例/步骤、执行进度、attempt、RequestId、TX/RX、RTT 和结构化错误。
- 提供执行选中、执行全部、跳过和中止操作。
- 通过 `AppStateController` 受控停止监控、进入 TESTING、执行并释放 Testing owner；用户可选择完成后恢复监控。
- 测试运行时锁定连接与配置操作，禁止其他页面绕过状态机访问串口。
- 创建 Phase 5 最小真实套件，例如 `testcases/functional/phase5-smoke.json`，至少包含：
  1. A 相温度范围读取；
  2. 四路温度/光敏连续块读取或序列断言；
  3. Firmware 版本读取；
  4. 非法地址返回 0x02；
  5. 阈值写入、独立回读并恢复原值。
- 使用真实 RS485 执行最小套件，保存每条结果、原始 TX/RX、耗时和最终阈值恢复证据。
- 添加 offscreen UI 测试，覆盖加载、执行、进度、PASS/FAIL/ERROR、跳过、中止和模式门禁。
- 创建 `docs/test_results/task016_phase5_automation_rs485.md`。

## 非范围

- 不实现 Phase 6 的完整 20 条功能/协议/边界/一致性/稳定性套件。
- 不实现 Phase 7 半自动人工提示、RS485 拔插引导或传感器操作。
- 不实现 Phase 8 HTML/PDF 报告和一键导出。
- 不实现 Raw Frame、错误 CRC 注入、0x04、0x10 或自动重连。
- 不修改 Firmware、寄存器表、通信线程模型或 Phase 4 监控规则。

## 依赖

- TASK-014 TestCase Loader、Schema 和断言已完成。
- TASK-015 TestEngine、处理器和结果管理已完成。
- TASK-011/012 应用状态、监控和 UI 已完成。
- TASK-013 诊断与会话日志已完成。
- TASK-010 当前真实 RS485 台架已通过。

## 实现门禁

1. TASK-015 未通过时不得在 QWidget 槽函数中临时编写测试执行循环。
2. 真机套件必须在执行前评审写操作和清理；任何阈值修改都必须保存并恢复原值。
3. UI 必须区分 FAIL、ERROR 和 SKIPPED，不得用颜色或单一“失败”文本合并。
4. 测试模式交接必须通过 AppStateController；监控停止和 Testing owner 未完成前不得发测试请求。
5. 真机测试前记录 Firmware/Host 版本、RS485 硬件、端口和阈值基线。
6. Phase 5 的 5 条用例不得被表述为 Phase 6 的 20 条完整套件已经完成。

## 实现要求

1. JSON 加载和执行不得阻塞 UI；长操作期间窗口仍响应重绘和中止。
2. 用例状态和统计只来自 TestResultManager，不由表格单元格反向驱动业务。
3. 监控活动时开始测试先受控停止监控；完成后是否恢复由明确用户选择，不能隐式猜测。
4. 执行中连接、监控、配置和重复开始按钮按状态机禁用。
5. 中止只通过 TestEngine，UI 不直接取消 QSerialPort 或 Worker。
6. 每条结果可定位到完整 attempt、RequestId、TX/RX、RTT、实际值和失败原因。
7. 真机阈值用例结束后独立读取确认恢复；恢复失败使套件结果至少为 ERROR 并保留证据。
8. SessionLog 中 TEST 事件与 UI/结果状态一致。

## 验收标准

1. Host 全新配置、构建和全部 CTest 通过，无 Phase 4 回归。
2. UI 可加载有效套件并清晰显示无效 JSON 的结构化错误和 JSON 路径。
3. 自动化测试页正确显示 NOT_RUN、RUNNING、PASS、FAIL、SKIPPED、ERROR。
4. 执行选中、执行全部、跳过和中止均通过 offscreen 自动测试。
5. 监控→停止→TESTING→执行→释放→CONNECTED_IDLE 的交接符合状态机，同一时刻只有 Testing owner 发请求。
6. 真实 RS485 最小套件至少 5 条用例全部进入明确终态，结果包含实际值、耗时、失败原因和 TX/RX。
7. 温度/版本读取和非法地址异常的实际结果与 JSON 预期一致。
8. 阈值测试完成写入、独立回读和原值恢复，最终值与执行前一致。
9. UI 在批量执行和中止期间保持响应，不在主线程阻塞等待通信。
10. 诊断视图、会话日志、TestResultManager 和 UI 的用例 ID/RequestId/状态一致。
11. 测试记录明确 Phase 5 只完成基础 5 条套件，Phase 6/7/8 仍未实施。
12. 最终 Review 无必须修复项，Phase 5“至少可以执行 5 个自动化测试”验收通过。

## 测试要求

- Loader/UI：有效/无效套件、错误路径、用例列表和详情。
- 执行 UI：单条、批量、跳过、中止、进度和全部终态展示。
- 模式交接：监控停止、Testing owner、执行完成、异常清理和可选恢复监控。
- 真机套件：至少 5 条读取、异常和安全写回读用例，阈值最终恢复。
- 证据核对：结果、诊断、JSONL 日志和 UI 的 ID、TX/RX、RTT、状态一致。
- 回归：全部 Host CTest、Phase 4 offscreen 测试和 smoke test。

## 当前状态

已完成。2026-09-05 已实现 Technical Spec、异步套件加载、测试工作流编排、自动化测试页面、8 条 Phase 5 基础套件、offscreen UI 测试和真实 RS485 验收；全新 Host 构建与 19/19 CTest 通过，COM6 实测 8/8 用例 PASS，阈值已恢复且结果/诊断/TEST 日志 RequestId 一致。Phase 6/7/8 仍未实施。
