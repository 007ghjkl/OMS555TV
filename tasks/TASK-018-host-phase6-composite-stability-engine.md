# TASK-018 Host Phase 6 复合测试、超时与稳定性执行核心

## 目标

基于 TASK-017 的 v2 规范化模型扩展 TestEngine 与处理器边界，实现 sequence、预期超时、数据一致性组合和稳定性长循环，并产生可供 UI 与后续报告直接消费的不可变步骤及统计结果。

## 背景

Phase 5 TestEngine 已支持严格串行基础请求、单用例预算、显式重试、中止和写后恢复，但当前本地 timeout 一律归类为 ERROR，处理器只能在请求完成后立即产生下一步，结果模型也没有复合步骤或长循环汇总。Phase 6 需要在不阻塞 UI、不破坏唯一 Testing owner 的条件下增加这些能力。

## 范围

- 实现前创建并评审 `specs/host_phase6_execution_engine.md`。
- 扩展处理器/执行会话接口，使其可请求有界延迟、重复步骤和聚合结束条件。
- 实现 v2 `sequence` 执行：按声明顺序运行、逐步断言、失败策略和完整步骤证据。
- 实现 v2 `expect_timeout`：只有匹配的 ResponseTimeout 才 PASS，提前正常响应、远端异常或其他错误均得到明确 FAIL/ERROR。
- 实现 uint32/缩放/逐元素等数据一致性断言的执行结果映射。
- 实现 v2 `stability`：按配置间隔串行轮询，在 duration 或中止时结束并计算统计。
- 统计总请求、成功、失败、超时、失败率及最小/平均/最大 RTT；平均值使用溢出安全算法。
- 扩展不可变结果模型，保存复合步骤、稳定性摘要、代表性证据和完整日志关联信息。
- 明确长循环的内存上限、失败样本保留、首尾证据和 JSONL 全量记录策略。
- 支持 sequence/stability 在中止、连接错误、owner 释放和日志错误下受控终止。
- 使用 Fake、虚拟时间和注入调度器覆盖全部路径，不依赖真实 `sleep()`。

## 非范围

- 不创建完整 20 条 JSON 套件或修改自动化测试页面布局。
- 不执行真实 RS485、物理断线、设备复位或传感器操作。
- 不实现 Raw Frame、CRC 错误注入、自动重连或并发请求。
- 不生成 HTML/PDF 报告。
- 不修改 Firmware、寄存器表或 QSerialPort 通信线程模型。

## 依赖

- TASK-017 的 v2 Schema、Loader、模型、断言和覆盖矩阵已完成。
- TASK-015 TestEngine、处理器注册表、TestResultManager 和 SessionLog 集成。
- TASK-016 TestAutomationController 与应用状态交接。

## 实现门禁

1. Spec 必须定义 sequence/case/step/iteration 的状态机、错误优先级和唯一终态。
2. 预期 timeout 与通信 ERROR 的分类必须由结构化类型决定，不能匹配错误文本。
3. stability 的间隔语义必须明确为请求开始间隔或请求完成后延迟，并规定请求耗时超过间隔时的行为。
4. 中止时必须停止新迭代、取消普通在途请求并受控释放 Testing owner；不得等待完整 duration。
5. 结果保留策略必须同时满足内存有界、故障可定位和 Phase 8 报告输入要求。
6. 长时计时使用单调时钟；UTC 只用于审计时间戳，不能决定 duration。
7. 任何写步骤继续遵守写前读取、独立回读和恢复规则；sequence 不得绕过安全清理。

## 实现要求

1. Engine 公共命令继续非阻塞，所有 QWidget 之外的通知回到应用线程。
2. 同一时刻仍最多一个用例、一个 Modbus 请求在途，全部复用 Testing owner。
3. sequence 每个已接受请求产生唯一 RequestId 和不可变步骤结果；不得覆盖前序失败证据。
4. `expect_timeout` 仅把目标请求的 ResponseTimeout 判为 PASS，不接受 Cancelled、ClosedByPeer 或串口错误。
5. stability 不因单次失败立即结束，除非配置或不变量要求；最终按样本数和允许失败率自动判定。
6. RTT 只统计具有有效 RTT 的完成请求，缺失样本必须单独计数，不能当作 0。
7. 失败率分母、四舍五入和阈值比较遵循 TASK-017 的确定算法。
8. 内存结果只保留 Spec 确认的首尾/失败/抽样证据，完整 request_completed 与 TEST 事件写入滚动 JSONL。
9. 日志写入失败不得伪造测试成功；按既有优先级进入附加错误并继续受控清理。
10. v1 基础处理器和 Phase 5 结果结构的外部行为保持兼容。

## 验收标准

1. Technical Spec 已评审，复合步骤、预期错误、计时、统计、内存和中止规则无未决项。
2. Host 全新配置、构建和全部 CTest 通过，无 Phase 5 回归。
3. sequence 覆盖全通过、中途 FAIL、通信 ERROR、失败后继续/停止策略和总预算耗尽。
4. expect_timeout 覆盖目标超时 PASS、正常响应 FAIL、远端异常 FAIL 和非 timeout ERROR。
5. stability 使用虚拟时间验证 duration、interval、最低样本、允许失败率边界和零有效 RTT。
6. 10 分钟、1 小时、8 小时和 24 小时虚拟运行不溢出、不线性占用无界内存。
7. 中止 sequence/stability 后不再启动请求，并在有界时间内返回 `CONNECTED_IDLE`。
8. 结果包含可追溯步骤、聚合统计、证据保留说明和 SessionLog 关联 ID。
9. Phase 5 v1 套件与既有 offscreen UI 回归通过。
10. 最终 Review 无必须修复项，允许进入 TASK-019。

## 测试要求

- sequence：顺序、延迟、逐步断言、失败策略、预算和中止。
- expect_timeout：精确错误分类及迟到响应隔离。
- stability：采样、统计、失败率、RTT、时长、日志和有界内存。
- 数据一致性：逐元素、符号/缩放、uint32 字序与单调性。
- 清理：owner 释放、迟到回调、日志错误和销毁。
- 回归：全部 Host CTest、Phase 5 套件和应用 smoke test。

## 当前状态

已完成。2026-09-05 已实现 sequence、expect_timeout、consistency 与 stability 执行核心、注入式单调时钟、复合步骤结果、稳定性统计、代表性有界证据和 SessionLog 全量关联。全新 Host 构建及 19/19 CTest 通过，其中 TestEngine 33 个测试覆盖 10 分钟、1 小时、8 小时和 24 小时虚拟运行；未访问真实串口、RS485 或开发板，最终 Review 无必须修复项，允许进入 TASK-019。
