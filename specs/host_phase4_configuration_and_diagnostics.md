# Host Phase 4 参数配置、通信诊断与会话日志技术规范

> 状态：已实现并验收
> 版本：1.0
> 日期：2026-09-04
> 对应任务：`TASK-013 Host 参数配置、通信调试与会话日志`

## 1. 目的与范围

本规范定义四路告警阈值的读取、写入和独立回读验证，基于不可变 Modbus 请求结果的通信诊断模型，以及可在 UI 查看并保存到本地文件的会话日志。实现复用既有 `IModbusClient`、`RegisterCodec`、集中应用状态和通信所有权，不在 UI、日志或诊断层重新解析串口缓冲区、RTU 帧或 CRC。

不实现设备地址或通信参数写入、公共 Raw Frame、错误帧注入、TestEngine、报告、数据库、远程日志和 Firmware 修改。

## 2. 组件与依赖

```text
MainWindow（Qt Widgets）
   ├── MonitoringViewModel
   ├── ConfigurationService ── AppStateController + IModbusClient + RegisterCodec
   ├── CommunicationDiagnosticsModel ── IModbusClient::requestCompleted
   └── SessionLogService ←── DiagnosticRecord（同一不可变请求结果的派生记录）
```

- `ConfigurationService` 属于 Qt Core 业务层，不依赖 Widgets、QSerialPort、RTU Codec 或硬编码地址。
- `CommunicationDiagnosticsModel` 只读取 `ModbusRequestResult` 已包含的描述符、结构化错误与 `RtuTransactionEvidence`；TX/RX 的权威类型始终为 `QByteArray`。
- `SessionLogService` 记录应用事件和同一 `DiagnosticRecord`，不对 TX/RX 做第二套协议解析。
- `MainWindow` 只调用服务命令和格式化展示，不包含 CRC、寄存器地址、RTU Codec 或串口访问。

## 3. 配置输入与状态

### 3.1 输入

输入由四个 `TemperatureChannel` 和摄氏温度组成。每个值必须通过 `device::encodeAlarmThreshold()`：有限值、-40.0～80.0 ℃、0.1 ℃ 精度。任一输入非法时整批在本地拒绝，不获取 owner、不分配 RequestId、不产生 TX。

### 3.2 服务状态

```cpp
enum class ConfigurationState {
    Idle, AcquiringOwner, ReadingCurrent, Writing,
    Verifying, Cancelling, ReleasingOwner
};
```

服务命令采用同步接受/拒绝、异步终态通知。任一时刻最多一个配置操作；每个操作分配单调配置操作 ID。只有以下条件全部满足时接受：

1. `AppStateController::state() == ConnectedIdle`；
2. 无应用控制命令进行；
3. 后端为 `Connected`；
4. 当前 owner 为 `None`；
5. 配置服务为 `Idle`。

监控中配置按钮禁用，不自动停止监控，也不在配置后自动恢复监控。用户必须先通过现有受控停止命令回到 `ConnectedIdle`，从而避免隐含的模式转换和 owner 覆盖。

## 4. 单项与批量流程

读取命令和写入命令均先以 `FinishInFlight` 获取 `ManualDebug` owner；由于接受门禁要求 owner 为 `None`，该动作不覆盖其他 owner。

### 4.1 读取四路阈值

1. 获取 `ManualDebug` owner；
2. 使用 `device::thresholdsReadBlock` 发出一次 0x03；
3. 用 `decodeThresholds()` 解码；
4. 保存并发布四路已验证值；
5. 释放 owner，回到 `Idle`。

### 4.2 写入与回读

1. 获取 owner 后先读取完整四路阈值，保存为 `before`；初始读取失败则不写任何寄存器。
2. 按 A、B、C、环境固定顺序处理每个选中项。
3. 每项发出独立 0x06，保存写请求 ID 和完整结果。
4. 仅当 0x06 成功回显后，发出独立 0x03 单寄存器回读，保存回读请求 ID 和完整结果。
5. 回读按有符号定点温度解码，并以 `deciCelsius` 与编码期望值比较。
6. 匹配时该项为 `Succeeded`；写失败、回读失败或不一致时该项失败，UI 保留该项的原已验证值，不把期望值显示为已生效。
7. 单项失败不阻止后续项，形成明确的批量部分成功结果。
8. 全部项终态后释放 owner；操作终态以所有选中项都成功为整体成功条件。

每个请求使用 `configuration/<operation-id>/<channel>/<stage>` correlation ID。服务严格等待当前请求终态后再发下一请求，任一时刻最多一个配置请求。

## 5. 失败、取消与清理

配置错误至少包含：`InvalidState`、`Busy`、`InvalidInput`、`OwnerRejected`、`InitialReadFailed`、`WriteRejected`、`WriteFailed`、`ReadbackRejected`、`ReadbackFailed`、`ReadbackMismatch`、`Cancelled`、`ReleaseFailed`、`ResultMismatch` 和 `InvariantViolation`。错误保留适用的通道、期望值、实际值、写/回读 RequestId、`CommunicationError` 或 `CodecError`。

- Slave 异常 0x02/0x03、超时、CRC、协议和串口错误沿用通信层结构化分类，不根据文本猜测。
- `cancel()` 只在操作进行时接受。若有已接受请求，调用一次 `cancelRequest()` 并等待终态；无请求则直接进入释放。未开始项标为取消，已成功项保持成功。
- 普通项失败、取消和回读不一致均执行 `CancelInFlight` 释放 `ManualDebug` owner，最终 owner 必须为 `None`，应用状态保持 `ConnectedIdle`。
- owner 释放失败或后端意外断开属于不可完整清理错误；结果明确报告当前 owner/连接状态，不伪造 `ConnectedIdle` 恢复。
- `before` 值始终进入操作结果，供真机验收恢复。产品 UI 不自动回滚，因为部分写入后再次写入也可能失败；恢复通过显式提交 `before` 值并再次回读完成。

## 6. 通信诊断记录

每个 `requestCompleted` 恰好转换为一条 `DiagnosticRecord`，至少包含：完成时间、RequestId、owner、请求状态、功能码、描述符中的地址和数量/值、TX/RX `QByteArray`、TX/RX CRC 状态、RTT、结构化错误类别/错误码、异常码和诊断文本。

- 级别映射：成功为 `DEBUG`；受控取消为 `INFO`；超时与远端异常为 `WARN`；CRC、协议、串口、内部错误为 `ERROR`。
- 默认保留最近 1000 条，新增超限时从最旧记录移除；上限可在构造时为测试设置，但生产不得为零或无界。
- 支持按级别、成功/失败结果和精确 RequestId 组合筛选；筛选只产生索引/副本视图，不修改权威记录。
- 清空只清除诊断模型的当前内存记录，不改变已发出的不可变结果、会话文件或通信统计。
- 十六进制仅在展示和 JSON 序列化时生成大写空格分隔文本。

## 7. 日志事件与会话文件

### 7.1 结构化事件

`LogEntry` 至少包含 UTC 时间戳、`DEBUG/INFO/WARN/ERROR/TEST`、模块、事件名、消息，以及可选 RequestId、错误码、TX、RX 和可扩展元数据。旧的基础格式化接口保留兼容。

### 7.2 生命周期与保留

- UI 内存日志生产上限为最近 2000 条，超限移除最旧项。
- `startSession(metadata)` 在无活动会话时创建会话 ID，写入 `session_start`；重复开始返回结构化 `AlreadyActive`。
- `endSession()` 写入 `session_end`、刷新并关闭；无活动会话返回 `NotActive`。
- 会话活动期间，诊断记录自动写入内存和文件；应用/配置事件通过显式 `append()` 进入相同通道。
- UI 清空仅清内存，不截断已保存文件。

### 7.3 文件格式与命名

文件为 UTF-8 JSON Lines，每行一个独立 JSON 对象，便于逐行恢复和未来扩展。字段包含 `schema_version=1`、`session_id`、`timestamp_utc`、`level`、`module`、`event`、`message` 及存在的可选字段。TX/RX 使用十六进制字符串，但内存权威值仍为 `QByteArray`。

固定输出根为应用当前工作目录下 `output/logs/`：

```text
output/logs/YYYY-MM-DD/session-YYYYMMDD-HHmmss-zzz-<session-id>.jsonl
```

会话 ID 使用不含个人目录信息的 UUID。元数据仅保存调用方明确传入的可扩展键值；TASK-013 不引入测试人员等产品输入流程。

单文件最大 10 MiB；下一条写入将超过上限时关闭当前文件并以 `.partNNN.jsonl` 继续，同一会话最多 100 个分片，超过后返回 `RetentionLimitReached` 并停止文件写入但继续有界内存记录。

文件错误包含 `CreateDirectoryFailed`、`OpenFailed`、`WriteFailed`、`FlushFailed`、`AlreadyActive`、`NotActive` 和 `RetentionLimitReached`。错误通过信号和最近错误状态显示，不抛异常、不阻塞或停止通信。每条写入后刷新，换取掉电时更完整的本地证据；不得调用同步等待或跨线程访问通信对象。

## 8. UI 行为

- 参数配置页显示四路当前已验证值和可编辑值，提供“读取阈值”“写入并回读”“取消”；按钮完全由应用/配置状态派生。
- 结果区逐项显示成功、写失败、回读失败、不一致或取消，并显示期望/实际值和相关 RequestId。
- 通信调试页显示有界列表、级别/结果/RequestId 筛选、清空和选中详情；详情显示完整 TX/RX、CRC、RTT、地址和值及错误。
- 会话日志页提供开始、结束、清空 UI 和当前文件路径，显示结构化日志与文件错误。
- 所有异常状态使用文字，不只依赖颜色；列表更新不得等待串口或文件长期操作。

## 9. 测试与真机验收

1. Fake 测试覆盖合法/非法输入、初始读取、四路成功、写失败后继续、回读失败、不一致、取消和 owner 释放。
2. 断言非法输入不产生 RequestId/TX，配置只在 `ConnectedIdle` 接受，Monitoring 时拒绝。
3. 诊断测试覆盖成功、远端异常、超时、取消、CRC、筛选、清空和有界保留。
4. 日志测试覆盖生命周期、JSONL 重新读取、命名、内存上限、文件错误和可选字段。
5. offscreen UI 测试覆盖配置门禁、逐项结果、诊断筛选详情和会话状态。
6. 全新 MSVC/Qt 配置、构建和全部 Host CTest 通过。
7. 真实 RS485 先读取保存四路原值，再写入安全且不同的合法值、逐路独立回读，最后显式写回原值并完成最终四路回读。全过程不得与监控并发。

## 10. 评审结论

本规范未修改 Firmware、寄存器表、`IModbusClient` 公共契约或既有应用状态枚举，也未增加第三方依赖。监控停止策略、ManualDebug owner 生命周期、批量部分失败、取消、UI 最终值、日志格式、命名、内存与文件上限、文件失败行为均已确定。实现可在既有 Qt Core/Widgets、Fake Client 和真实 QSerialPort 后端上完成，无待实现选择，内部评审通过。

实现后复审确认代码符合上述边界。全新 Host 构建与 15/15 CTest 通过，COM6 真实 RS485 四路阈值写入、独立回读、原值恢复和最终整块读取通过；会话日志 20/20 请求均保留 TX/RX。最终 Review 无必须修复项，详细证据见 `docs/test_results/task013_configuration_diagnostics_rs485.md`。
