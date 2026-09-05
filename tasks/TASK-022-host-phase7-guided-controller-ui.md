# TASK-022 Host Phase 7 半自动测试控制器与 UI

## 目标

基于 TASK-021 的 v3 规范，实现无 QWidget 依赖的引导式执行协调、人工动作命令和自动观察流程，并在现有自动化测试页面中提供清晰、安全、可取消的半自动测试交互。

## 背景

现有 TestAutomationController 能加载套件、取得 Testing owner、运行 TestEngine 并展示自动步骤，但没有“暂停等待用户操作”的业务状态。人工等待不能阻塞应用线程，也不能让 UI 直接发 Modbus 请求或修改 Engine 内部状态。Phase 7 需要在既有唯一通信所有权和不可变结果模型之上增加一层受控协调。

## 范围

- 实现前创建并评审 `specs/host_phase7_guided_execution_ui.md`。
- 实现引导式执行协调边界，可采用独立 `GuidedTestCoordinator` 或经 Spec 确认的等价模块。
- 协调器只管理人工步骤与自动观察切换；所有 Modbus 探测仍经 TestEngine、Testing owner 和唯一 `IModbusClient`。
- 实现状态流程：加载、进入 TESTING、提示人工断线、等待确认、观察中断、提示重连、等待确认、观察恢复、自动判定、释放 owner。
- 从进入引导式运行到终态持续持有 Testing owner；人工等待期间不发送请求，但不允许监控、配置或调试取得串口。
- 提供带一次性 token 的确认/继续与取消命令，拒绝 stale、重复、跨运行和错误步骤操作。
- 人工等待、观察 deadline 和探测间隔使用注入调度器，命令及通知保持非阻塞。
- 自动观察连续超时与连续成功；保存每次探测的 RequestId、TX/RX、RTT 和结构化结果。
- 把 prompt_shown、operator_confirmed、operator_cancelled、observation_started/finished、guided_case_finished 写入 TEST 日志。
- 扩展 Qt 自动化测试页面：
  - 显示当前人工步骤、明确操作说明与安全提示；
  - 显示等待/观察倒计时、连续样本进度和恢复耗时；
  - 提供明确“确认已完成”和“取消测试”操作；
  - 显示 PASS/FAIL/ERROR/SKIPPED、人工记录和自动证据；
  - 运行时保持连接、监控、配置和调试门禁。
- 取消后显示恢复接线提醒；如果软件尚未观察到链路恢复，不得显示为安全恢复完成。
- 使用 Fake、虚拟时间和 offscreen Qt 验证完整成功/失败/取消流程。

## 非范围

- 不创建正式 Phase 7 RS485 套件或执行真实人工拔插。
- 不实现自动串口重开、自动重连或 Windows 设备热插拔管理。
- 不实现 STM32 Reset、传感器断开或告警人工刺激页面。
- 不生成 HTML/PDF 报告。
- 不修改 Firmware、寄存器表或 QSerialPort 线程模型。

## 依赖

- TASK-021 Schema v3、guided 模型、动作 token 和结果契约已完成。
- TASK-018 TestEngine 调度、复合步骤与结构化 timeout。
- TASK-016/019 TestAutomationController、自动化测试页面和状态门禁。
- TASK-013 SessionLogService 与 TEST 日志。

## 实现门禁

1. Spec 必须明确协调器、TestAutomationController、TestEngine 与 UI 的所有权和信号方向，避免第二套通信状态机。
2. 人工等待期间必须持续持有 Testing owner，并由 TASK-021 的有界等待规则防止无限占用；同一时刻仍只能有一个请求执行器。
3. UI 不得根据按钮点击直接判 PASS；只有观察条件和 Engine 结果能决定最终状态。
4. 取消后的状态、owner 释放、迟到探测和恢复提醒必须在 Spec 中有唯一规则。
5. 所有跨线程动作必须携带 run/case/step/token，并在应用线程校验。
6. 人工步骤记录必须进入不可变结果与 TEST 日志，不能只保存在临时控件文本中。

## 实现要求

1. 不使用模态阻塞循环、`sleep()` 或主线程同步等待硬件动作。
2. 等待人工确认时不发送探测请求；确认后才进入对应观察阶段。
3. 观察阶段严格串行发请求，不追赶积压，达到连续次数或 deadline 后立即终止。
4. 中断观察只有 `ResponseTimeout` 计入命中；其他错误按 TASK-021 分类。
5. 恢复观察只有满足业务断言的合法响应计入连续成功，错误响应会重置连续计数或按 Spec 终止。
6. 恢复耗时从用户确认“已重连”开始，以单调时钟计算并同时保存首个成功与稳定恢复时刻。
7. 日志失败不得把失败流程变成 PASS，仍需受控释放 owner 并保留附加错误。
8. 页面在人工等待和自动观察期间可重绘、切换详情和取消。
9. v1/v2 自动套件的页面行为、状态和结果保持兼容。

## 验收标准

1. Technical Spec 已评审，模块边界、状态机、token、计时和清理无未决项。
2. Host 全新配置、构建和全部 CTest 通过，无 Phase 5/6 回归。
3. Fake 成功流程完整经过两次人工确认、连续 timeout、连续成功和自动 PASS。
4. 未检测到中断、恢复超时、致命错误和错误业务响应分别得到稳定 FAIL/ERROR。
5. 人工取消、等待超时、中止观察和窗口关闭均停止新请求并受控释放 owner。
6. stale/重复/跨运行确认被拒绝，不推进步骤或重复写日志。
7. 结果、诊断和 TEST 日志中的人工 step、token、RequestId、时间及状态一致。
8. offscreen UI 正确显示提示、安全说明、倒计时、连续样本、恢复耗时和全部终态。
9. 人工等待与观察期间 UI 心跳不中断，连接/监控/配置/调试门禁正确。
10. 最终 Review 无必须修复项，允许进入 TASK-023。

## 测试要求

- 状态机：成功、取消、人工超时、观察失败、致命错误和销毁。
- token：stale、重复、跨运行、错误 step 和完成后操作。
- 观察：连续超时、连续成功、计数重置、deadline 和迟到回调。
- 日志/结果：人工动作、探测证据、恢复耗时、附加错误和 owner 清理。
- UI：提示、按钮、倒计时、进度、终态、响应性和门禁。
- 回归：全部 Host CTest、v1/v2 套件和应用 smoke test。

## 当前状态

待实施。任务文档已于 2026-09-05 创建，尚未派发；必须等待 TASK-021 完成。
