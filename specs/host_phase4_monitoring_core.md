# Host Phase 4 应用状态与监控核心技术规范

> 状态：实现前评审通过
> 版本：1.0
> 日期：2026-09-04
> 对应任务：`TASK-011 Host Phase 4 应用状态与监控核心`

## 1. 目的与范围

本规范定义不依赖 QWidget 和真实串口的应用状态控制与周期监控核心，包括：

- `AppStateController` 的连接、监控、测试占位和错误恢复状态；
- `MonitorService` 的五块串行轮询、完整快照、健康状态和停止行为；
- 可审计通信统计、批次元数据和结构化错误；
- 生产 Qt 定时调度器与无 `sleep()` 的手动测试调度边界。

本规范不实现 UI、参数写入、通信调试、TestEngine、报告、自动重连和 Firmware 修改。

## 2. 既有边界与依赖

1. 连接、所有权和请求只通过 `communication::IModbusClient` 完成。
2. Monitor 使用 `CommunicationOwner::Monitor`，不持有 `QSerialPort` 或 Worker。
3. 每批读取地址和数量只来自 `device::deviceSnapshotReadBlocks`。
4. 完整领域快照只通过 `device::decodeDeviceSnapshot()` 生成。
5. 不修改 `IModbusClient` 的公共契约、线程模型、队列语义和错误类型。
6. `AppStateController`、`MonitorService` 和调度器均属于 Qt Core 层，不依赖 Qt Widgets。

## 3. 应用状态模型

```cpp
enum class AppState {
    Disconnected,
    ConnectedIdle,
    Monitoring,
    Testing,
    Stopping,
    Error,
};
```

控制命令采用“同步接受/拒绝、异步终态通知”：接受时分配应用操作 ID，拒绝时不分配 ID并返回结构化错误。任一时刻最多存在一个控制操作。

### 3.1 状态转换

| 当前状态 | 命令/事件 | 条件与动作 | 终态 |
|---|---|---|---|
| `Disconnected` | connect | 调用异步 `open` | 成功 `ConnectedIdle`；失败 `Error` |
| `ConnectedIdle` | disconnect | 调用 `close(CancelAll)` | 成功 `Disconnected`；失败 `Error` |
| `ConnectedIdle` | startMonitoring | 获取 Monitor owner，成功后启动 Monitor | `Monitoring`；失败 `Error` |
| `Monitoring` | stopMonitoring | 进入停止流程，Monitor 受控取消唯一请求并释放 owner | `ConnectedIdle` |
| `Monitoring` | disconnect | 先停止 Monitor、释放 owner，再关闭连接 | `Disconnected` |
| `ConnectedIdle` | startTesting | 获取 Testing owner；不启动 TestEngine | `Testing` |
| `Testing` | stopTesting | `CancelInFlight` 释放 Testing owner | `ConnectedIdle` |
| `Testing` | disconnect | 先 `CancelInFlight` 释放 owner，再关闭 | `Disconnected` |
| 任意已连接状态 | backend Faulted | 停止调度，不再提交请求 | `Error` |
| `Error` | recover | 后端已断开则清错；否则异步关闭 | `Disconnected` |

连接打开、owner 转换和关闭期间不增加额外公开状态，使用 `commandInProgress` 标志门禁；需要停止活动模式时公开状态为 `Stopping`。

### 3.2 拒绝规则

- 有控制操作未完成时拒绝新命令为 `CommandInProgress`。
- 非 `Disconnected` 状态连接、非 `ConnectedIdle` 状态启动模式、模式不匹配的停止命令均为 `InvalidState`。
- 后端连接状态或 owner 与应用状态不一致时为 `BackendStateMismatch`，进入 `Error`。
- `Testing` 与 `Monitoring` 互斥；所有权转换期间不得提交监控请求。
- 重复命令不隐式视为成功，必须返回结构化拒绝。

## 4. 监控配置与调度

```cpp
struct MonitorConfig {
    std::chrono::milliseconds targetPeriod{1000};
    std::optional<std::chrono::milliseconds> requestTimeout;
};
```

- `targetPeriod` 合法范围为 10～60000 ms，默认 1000 ms。
- 单请求超时为空时沿用连接默认值；非空时必须为 1～60000 ms。
- 第一个批次在 Monitor owner 获取成功后立即调度。
- 周期定义为期望的“批次开始到下一批次开始”间隔，不是完成后的固定等待。
- 一个批次内按 `deviceSnapshotReadBlocks` 顺序提交五次 0x03；只有收到前一请求终态后才提交下一请求。
- 任一时刻最多一个 Monitor 请求，不预先向通信队列提交剩余四块。
- 批次耗时小于目标周期时，下一批在 `batchStarted + targetPeriod` 调度。
- 批次耗时大于或等于目标周期时记一次 overrun，完成后以零延迟调度下一批；不补发错过的周期、不形成积压。
- `effectiveInterval` 是相邻两个批次实际开始单调时间之差；首批为空。

调度器接口同时提供单调时间、UTC 时间、一次性任务和取消。生产实现使用应用线程 `QTimer` 与 `QElapsedTimer`；测试实现由手动虚拟时钟提供，不依赖真实等待。

## 5. 批次、快照与完整性

每批分配单调递增、零保留的 `PollBatchId`，所有请求的 `correlationId` 为 `monitor/<batch-id>`。批次记录至少包含：

- 批次 ID、开始/完成 UTC；
- 实际开始间隔、批次耗时和是否 overrun；
- 按完成顺序保存的原始 `ModbusRequestResult`；
- 五个请求 ID；
- 完整性和结构化错误；
- 成功时的不可变 `device::DeviceSnapshot`。

只有五个块全部成功、地址/数量与定义一致且 `decodeDeviceSnapshot()` 成功时，才发布新的 `MonitoringSnapshot`。不得发布部分快照，也不得将不同批次寄存器拼接。

失败批次保留上一份成功快照及原 `completedUtc`/批次 ID；失败批次只更新批次结果、健康状态、错误和统计，不能更新 `lastSuccessfulUtc`。

以下任一情况使当前批次失败并停止读取其余块：

1. 请求同步拒绝；
2. 请求失败、超时或远端异常；
3. 成功结果类型、地址或寄存器数量与当前块不匹配；
4. 五块完成后的领域编解码失败；
5. 内部批次/请求 ID 不变量破坏。

## 6. 设备健康判定

```cpp
enum class DeviceHealth { Unknown, Online, Degraded, Offline };
```

- 监控刚启动且尚无终态批次：`Unknown`。
- 任一完整成功批次：`Online`，连续失败批次数清零。
- 第 1～2 个连续失败批次：`Degraded`。
- 第 3 个及以后连续失败批次：`Offline`。
- 后端进入 `Faulted` 或意外 `Disconnected`：立即 `Offline`，不等待三批。
- 用户主动停止监控并成功释放 owner：`Unknown`；最后成功快照仍可读取，但不代表当前在线。
- 之后任一完整成功批次立即从 `Degraded`/`Offline` 恢复为 `Online`。

“连续三批”是本版本确定阈值，按批次而不是按单个寄存器块计数；后续修改必须更新 Spec 和测试。

## 7. 通信统计口径

`CommunicationStatistics` 使用无符号 64 位饱和计数，达到最大值后保持最大值，不回绕。统计至少包含：

- 已接受请求数 `requests`；
- 成功、失败、超时和受控取消请求数；
- 连续请求失败数；成功请求清零；用户停止导致的取消不增加连续失败；
- 有 RTT 的结果数量、最近/最小/最大 RTT、累计 RTT 纳秒；累计值饱和，可由累计值和样本数重建平均值；
- 开始、成功、失败批次数；连续失败批次数；
- overrun 次数、最近批次耗时、最近有效开始间隔。

请求同步拒绝不计入 `requests`，但使批次失败。已接受请求的每个终态结果恰好统计一次。远端异常、CRC、协议、串口和内部请求错误均计为失败；Timeout 同时计入失败和超时；停止取消单列为 cancelled，不计入 failed。

## 8. 停止、断开与迟到回调

TASK-011 统一采用受控取消策略：

1. 停止时先取消下一批定时任务并禁止提交新请求。
2. 无请求时立即完成 Monitor 停止。
3. 有 queued 或 in-flight 请求时只对当前记录的 RequestId 调用一次 `cancelRequest()`。
4. 收到该请求的终态结果后完成 Monitor 停止；取消结果保存到批次审计记录，但该批次标记为 `Stopped`，不改变在线失败阈值。
5. Monitor 停止完成后 Controller 才释放 Monitor owner。
6. 所有信号处理均校验运行代次、批次 ID 和 RequestId；停止后的定时器或非当前请求回调只可忽略和记录，不得重启轮询。
7. 断开命令复用上述顺序，owner 释放后再关闭连接。

## 9. 结构化错误

应用错误至少包含类别、稳定错误码、命令、应用状态、可选底层 `CommunicationError` 和诊断文本。监控错误至少包含：

- `RequestRejected`、`RequestFailed`、`ResultMismatch`、`DecodeFailed`、`BackendUnavailable`、`InvariantViolation`、`Stopped`；
- 批次 ID、块索引/地址、可选 RequestId；
- 可选通信错误或 `device::CodecError`；
- 诊断文本。

程序分支只依赖枚举和结构化字段，诊断文本只用于展示和日志。

不可恢复条件包括后端 `Faulted`、意外断开、所有权状态不一致和 Controller 内部不变量破坏。普通请求超时、远端异常、CRC/协议错误只使当前批次失败，不自动断开或进入应用 `Error`。

## 10. 线程与生命周期

- Controller、Monitor 和生产调度器在应用线程创建和使用。
- `IModbusClient` 的完成信号按其既有契约回到应用线程。
- 公共命令不得等待串口、线程或定时器。
- QObject 销毁自动断开信号；调度回调使用受控生命周期上下文，禁止保存悬空裸指针。
- Controller 不拥有 `IModbusClient`；Monitor 不拥有 Controller，二者由应用组装层管理生命周期。

## 11. 测试要求

1. 状态机覆盖连接成功/拒绝、重复命令、关闭、Monitor/Testing 合法与非法迁移和恢复。
2. Fake 脚本证明五块严格串行，地址只来自 `RegisterMap`，每批最多一个完整快照。
3. 覆盖五块成功、各类通信失败、远端异常、超时、编解码失败和下一批恢复。
4. 目标周期短于批次耗时时不得重叠或积压，并核对 overrun/effective interval。
5. 覆盖无在途、queued/in-flight 停止，owner 最终释放且迟到调度不发请求。
6. 逐项核对请求、成功、失败、超时、取消、RTT 累计和连续失败统计。
7. 全部测试使用 Fake 与手动调度，不使用 `sleep()`，并运行全部 Host CTest。

## 12. 评审结论

### 12.1 一致性

- 状态机符合 `docs/architecture.md` 的集中状态和 Monitoring/Testing 互斥要求。
- 轮询只使用既有 `IModbusClient`、Monitor owner、`RegisterMap` 和 `RegisterCodec`。
- 停止采用现有 `cancelRequest` 与 `releaseOwnership`，无需扩展公共通信契约。
- 100 ms 小周期场景已由 start-to-start 语义、单批串行和不补发规则消除重叠歧义。

### 12.2 风险与控制

- 连续三批离线阈值属于运行策略而非协议事实；已固定在本规范并要求测试，UI 不得另行解释。
- 当前统计存于内存且使用饱和累计；持久化和会话日志由后续任务实现。
- `Testing` 仅完成所有权状态边界，不代表 TestEngine 已实现。

### 12.3 门禁结论

本规范未引入架构变更、公开通信 API 变更或新第三方依赖；状态、快照完整性、健康判定、周期语义、停止策略和统计口径均已确定，无待实现选择。实现前内部评审通过，可以进入 TASK-011 编码。
