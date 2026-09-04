# TASK-011 Host Phase 4 应用状态与监控核心

## 目标

在不依赖 QWidget 和真实硬件的前提下，实现集中式应用状态控制、连接生命周期协调和周期监控服务，使 Host 能通过现有 `IModbusClient` 串行读取设备快照、维护在线状态与通信统计，并为后续监控 UI 和 TestEngine 所有权交接提供稳定边界。

## 背景

TASK-008 已完成异步 QSerialPort Modbus Master，TASK-010 已通过真实 RS485 验证其读写、超时和恢复能力。当前 `MainWindow` 仍是 Phase 0 占位界面，仓库也没有 `monitor` 或集中式应用状态模块。

PRD 和架构要求监控与测试互斥、UI 不阻塞、同一串口最多一个执行器、监控周期不重叠，并显示设备状态和通信统计。因此应先完成可由 Fake 验证的非 UI 核心，再接入真实界面。

## 范围

- 实现前创建并评审 `specs/host_phase4_monitoring_core.md`。
- 实现集中式 `AppStateController`，覆盖 `DISCONNECTED`、`CONNECTED_IDLE`、`MONITORING`、`TESTING`、`STOPPING` 和 `ERROR` 语义。
- 协调 `IModbusClient` 的异步 open、close、Monitor 所有权获取/释放和错误状态。
- 实现 `MonitorService`，按 `RegisterMap::deviceSnapshotReadBlocks` 串行读取五个合法块。
- 使用 TASK-004 `RegisterCodec` 组合不可变 `DeviceSnapshot`，不复制寄存器地址和领域解释。
- 定义不可变监控快照元数据，包括轮询批次 ID、开始/完成时间、各请求 ID、完整性和最后成功时间。
- 定义连接状态、设备在线/离线/退化状态和结构化监控错误。
- 定义并维护通信统计：请求数、成功、失败、超时、连续失败、最近 RTT 以及可审计的累计 RTT 指标。
- 支持可配置轮询目标周期；前一周期未结束时不得启动下一周期。
- 当实际读取耗时超过目标周期时，不积压周期；记录 overrun/effective interval，完成后再调度下一周期。
- 停止监控时停止产生新请求，按 Spec 完成或受控取消唯一在途请求，再释放 Monitor 所有权。
- 使用 `FakeModbusClient` 和手动调度器覆盖状态、轮询、错误和停止行为，测试不得依赖真实 `sleep()`。
- 为后续 Testing 所有权预留合法迁移边界，但不实现 TestEngine。

## 非范围

- 不实现或修改 QWidget、MainWindow 布局、趋势曲线和最终视觉样式。
- 不访问真实串口、VCP 或 RS485 硬件。
- 不实现阈值配置、通信调试界面、会话日志、TestEngine 或报告。
- 不修改 `IModbusClient` 的线程模型、公共传输语义或 Firmware。
- 不引入数据库、网络服务或重量级状态管理框架。

## 依赖

- TASK-004 设备模型、寄存器读块和 `RegisterCodec` 已完成。
- TASK-007 `IModbusClient`、通信模型和确定性 Fake 已完成。
- TASK-008 QSerialPort 生产后端已完成。
- TASK-010 真实 RS485 验收已通过，但本任务测试不依赖硬件。
- `docs/architecture.md` 的应用状态机和所有权模型。

## 实现门禁

1. Spec 必须先定义应用状态转换表、触发事件、拒绝条件和错误恢复，不得把状态散落在 UI 按钮槽函数中。
2. Spec 必须定义完整快照、部分失败、在线/离线/退化和恢复的判定规则；实现不得自行选择未记录阈值。
3. Spec 必须定义轮询周期的含义、超期行为和统计口径，尤其说明 100 ms 目标周期小于当前五块实际读取耗时时如何处理。
4. 监控只能通过 `IModbusClient` 和 Monitor owner 访问通信，不得持有 QSerialPort 或 Worker。
5. 不得发布由不同轮询批次拼接或部分成功伪装成完整的 `DeviceSnapshot`。
6. 若发现现有 `IModbusClient` 契约无法满足监控，应先提出最小扩展和影响分析，不得静默绕过所有权。

## 实现要求

1. 所有公共命令非阻塞；完成与状态通知在应用线程发出。
2. 连接成功且 owner=None 时进入 `CONNECTED_IDLE`；监控必须先获得 Monitor owner 才能进入 `MONITORING`。
3. 未连接、正在关闭、Testing 或所有权转换期间启动监控必须返回结构化拒绝。
4. 每个轮询批次顺序执行五个读块，同一时刻最多一个在途请求。
5. 每个请求使用同一批次 correlation ID，并保留原始 `ModbusRequestResult` 供后续诊断消费。
6. 只有五个读块全部成功并通过 `RegisterCodec` 时才发布新的完整快照。
7. 失败周期保留上一份成功快照及其时间戳，不得把旧值标记为新采样；同时发布健康状态和错误。
8. 停止、断开和不可恢复错误必须使轮询调度停止，不能留下迟到定时器再次发请求。
9. 统计必须可从结果重建，不以 UI 文本为权威数据；累计计数使用防溢出策略。
10. `TESTING` 迁移接口仅体现状态和所有权约束，不实现测试用例执行。

## 验收标准

1. Technical Spec 已评审通过，状态、轮询、快照完整性和在线判定没有未决实现选择。
2. Host 全新配置、构建和全部 CTest 通过，无既有通信、设备和 smoke test 回归。
3. Fake 测试覆盖连接成功/失败、重复连接、关闭、合法和非法状态迁移。
4. Fake 测试证明五个读块严格串行、地址来自 `RegisterMap`，每周期最多发布一个完整快照。
5. 配置周期小于实际批次耗时时不会重叠或积压，并记录 overrun/effective interval。
6. 单块失败、超时、远端异常和编解码失败不会发布混合快照，上一成功快照带原时间戳保留。
7. 停止监控覆盖无在途、queued 和 in-flight 场景，最终释放 owner 并回到 `CONNECTED_IDLE`。
8. 断开或不可恢复错误会停止调度并进入架构规定状态，迟到回调不会重新启动轮询。
9. 统计中的请求、成功、失败、超时和 RTT 与 Fake 脚本逐项一致。
10. 测试使用手动调度器或注入时钟，不依赖不受控 `sleep()`。
11. 新核心不依赖 Qt Widgets，不访问串口，不修改 Firmware。
12. 最终 Review 无必须修复项，允许进入 TASK-012。

## 测试要求

- 状态机：全部合法迁移、非法迁移、重复命令、连接失败、关闭和错误恢复。
- 轮询：五块成功、各块失败、超时、停止、周期超期和连续多周期。
- 快照：完整性、批次 ID、时间戳、旧值保留、编解码失败和恢复。
- 所有权：Monitor 获取/释放、Testing 冲突、交接期间拒绝和恰好一次完成。
- 统计：成功/失败/超时、连续失败、RTT、overrun 和计数边界。
- 回归：运行全部 Host CTest 和应用 smoke test。

## 当前状态

待实施。任务文档已于 2026-09-04 创建，尚未派发。
