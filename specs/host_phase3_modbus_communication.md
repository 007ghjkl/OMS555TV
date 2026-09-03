# Host Phase 3 Modbus 异步通信技术规范

> 状态：已评审，ADR 已确认，可作为后续实现基线
> 版本：1.0
> 日期：2026-09-03
> 对应任务：`TASK-006 Host Phase 3 Modbus 后端决策与异步通信规范`
> 对应 ADR：`docs/decisions/2026-09-03-Host-Modbus后端.md`

## 1. 目的

本规范定义 Host Modbus RTU 通信边界，包括：

- 串口与设备端点配置；
- `IModbusClient` 异步契约；
- 单通信线程和串行请求队列；
- 连接、请求、超时、取消和关闭生命周期；
- 完整 RTU TX/RX、CRC、时间戳和 RTT 证据；
- 结构化错误与 Modbus 异常响应；
- Monitor、TestEngine 和 ManualDebug 的通信所有权交接；
- Fake Client 和分层验证策略。

本规范按已接受 ADR 的“QSerialPort 受控 RTU 后端”编写。用户已于 2026-09-03 确认该架构；生产实现仍必须由独立任务执行，不属于 TASK-006。

## 2. 依据

1. `tasks/TASK-006-host-phase3-modbus-backend-decision.md`；
2. `docs/prd.md` 和 `PROJECT_SPEC.md`；
3. `docs/architecture.md`；
4. `docs/test_plan.md`；
5. `docs/hardware_baseline.md`；
6. `research/qt_serialbus_raw_frame.md`；
7. TASK-004 设备模型、寄存器编解码及其 Technical Spec；
8. Qt 6.8.3 公开 `QSerialPort`、`QIODevice`、`QThread` 和 Qt Core API。

若本规范与用户确认后的 ADR 冲突，先修订本规范，不允许生产代码静默选择另一后端。

## 3. 范围

### 3.1 范围内

- MVP 功能码 `0x03 Read Holding Registers` 和 `0x06 Write Single Register`。
- Modbus RTU Master ADU 编解码与 CRC16。
- 一个连接对应一个串口、一个工作线程、一个活动请求和一个串行队列。
- 完整 RTU 证据、结构化结果与错误。
- 异步打开、关闭、所有权切换、读、写和取消契约。
- 可确定复现成功、超时、CRC、异常、取消和模式切换的 Fake。

### 3.2 范围外

- `0x04`、`0x10`、Modbus TCP 和多串口并行。
- 未经独立任务批准的公共 Raw Frame/错误 CRC 发送入口。
- Monitor 轮询、参数配置 UI、通信调试 UI、TestEngine 和报告实现。
- Firmware、TASK-002、RS485 DE/RE、终端与偏置。
- 自动重连策略；MVP 故障恢复由上层显式关闭后重新打开。

## 4. 总体结构

```text
Main/Application thread
┌──────────────────────────────────────────────────────────────┐
│ AppStateController                                          │
│ Monitor / TestEngine / ManualDebug                          │
│                    │                                         │
│                    ▼                                         │
│ IModbusClient Facade                                         │
│ - 立即校验和分配 ID                                           │
│ - 所有权门禁                                                  │
│ - 发出异步完成信号                                             │
└────────────────────┬─────────────────────────────────────────┘
                     │ Qt::QueuedConnection
                     ▼
Communication thread
┌──────────────────────────────────────────────────────────────┐
│ CommunicationWorker                                          │
│ - 有界串行队列                                                │
│ - 最多一个在途请求                                             │
│ - RTU 状态机、超时、取消、重新同步                              │
│ - RtuCodec / Modbus CRC16                                    │
│ - QSerialPort（在线程内创建、使用和销毁）                       │
└────────────────────┬─────────────────────────────────────────┘
                     ▼
             VCP/UART；后续 RS485
```

边界约束：

- `communication` 只解释 RTU、PDU 和原始寄存器，不解释温度、告警或版本。
- 读成功结果交给 TASK-004 `device` 模块解码，Monitor 和 TestEngine 不复制寄存器语义。
- 日志与报告消费不可变通信结果，不重新解析串口缓冲区。
- `QSerialPort` 不暴露给 UI、Monitor、TestEngine 或 ManualDebug。

## 5. 配置模型

### 5.1 串口配置

```cpp
enum class SerialParity { None, Even, Odd };
enum class SerialStopBits { One, Two };
enum class SerialFlowControl { None };

struct SerialPortConfig {
    QString portName;
    qint32 baudRate;
    quint8 dataBits;
    SerialParity parity;
    SerialStopBits stopBits;
    SerialFlowControl flowControl;
};
```

约束：

- `portName.trimmed()` 非空，不写死 COM3、COM5 或其他枚举结果。
- `baudRate > 0`；实际值还必须被当前 QSerialPort/驱动接受。
- Modbus RTU MVP 只接受 8 data bits，其他值返回 `UnsupportedConfiguration`，避免 CRC 和任意字节被截断。
- Parity 接受 None、Even、Odd。
- Stop bits 接受 1 或 2；当前基线为 1。
- Flow control MVP 只接受 None。
- 打开后配置不可局部修改；变更配置必须关闭并重新打开。

### 5.2 连接配置

```cpp
struct ModbusConnectionConfig {
    SerialPortConfig serial;
    quint8 serverAddress;
    std::chrono::milliseconds defaultResponseTimeout;
    qsizetype maxPendingRequests;
};
```

约束：

- `serverAddress` 为 1～247；广播地址 0 不属于 MVP。
- `defaultResponseTimeout` 为 1～60000 ms；当前硬件基线默认 500 ms。
- `maxPendingRequests` 为 1～1024；应用默认 64，防止无界积压。
- 当前默认值为 COM 端口由用户选择、115200、8N1、无流控、Slave ID 1、500 ms。

### 5.3 请求选项

```cpp
struct RequestOptions {
    CommunicationOwner owner;
    std::optional<std::chrono::milliseconds> responseTimeout;
    QString correlationId;
};
```

- 单请求超时未提供时使用连接默认值，提供时仍必须位于 1～60000 ms。
- `correlationId` 用于关联测试用例、轮询批次或 UI 操作，不作为请求唯一标识。
- `owner` 必须等于当前活动所有者。

## 6. 标识、时间和不可变结果

### 6.1 标识

- `OperationId`：异步打开、关闭和所有权切换的非零 `quint64` 标识。
- `RequestId`：读写请求的非零 `quint64` 标识。
- ID 在单进程生命周期内单调递增且不复用；到达上限时拒绝新操作并返回 `IdExhausted`，不得回绕。
- ID 在 Facade 接受命令时分配，因此调用方可以在异步完成前建立关联。

### 6.2 时间

每个请求同时保存：

- UTC 墙钟时间：用于日志和报告展示；
- 单调时钟时间点：用于队列等待、超时和 RTT 计算，不受系统时间调整影响。

至少记录：

- `enqueuedAt`；
- `txStartedAt`：调用 `QSerialPort::write()` 前；
- `txAcceptedAt`：完整 ADU 被 `write()` 接受后；
- 可选 `firstRxAt`；
- `completedAt`。

`queueDelay = txStartedAt - enqueuedAt`。`rtt = completedAt - txAcceptedAt`；只有完整 ADU 已被接受时 RTT 才有值。该 RTT 是 Host API 观测值，不宣称为示波器测得的物理线路时延。

### 6.3 结果不可变性

请求进入终态后，结果对象不再修改。日志、TestEngine 和报告共享该结果的值副本或只读共享对象，禁止后续补写造成报告与 UI 不一致。

## 7. `IModbusClient` 设计契约

### 7.1 Facade 线程约束

- `IModbusClient` 是 Qt Core `QObject` Facade，归属于应用线程。
- 公共方法非阻塞，仅执行快速参数/状态校验、分配 ID 和排队。
- 公共方法必须在 Facade 所在线程调用；其他线程通过 queued signal/invocation 进入 Facade。
- 完成、状态和所有权通知在 Facade 线程发出；消费者不得直接连接 Worker。

### 7.2 设计层 API

以下为语义合同，不在 TASK-006 中修改生产头文件：

```cpp
class IModbusClient : public QObject
{
    Q_OBJECT
public:
    virtual ConnectionState connectionState() const noexcept = 0;
    virtual CommunicationOwner activeOwner() const noexcept = 0;

    virtual ControlSubmission open(const ModbusConnectionConfig &config) = 0;
    virtual ControlSubmission close(CloseMode mode = CloseMode::CancelAll) = 0;

    virtual ControlSubmission acquireOwnership(CommunicationOwner owner,
                                                HandoffMode mode) = 0;
    virtual ControlSubmission releaseOwnership(CommunicationOwner owner,
                                                HandoffMode mode) = 0;

    virtual RequestSubmission readHoldingRegisters(
        device::PduAddress startAddress,
        quint16 count,
        const RequestOptions &options) = 0;

    virtual RequestSubmission writeSingleRegister(
        device::PduAddress address,
        quint16 rawValue,
        const RequestOptions &options) = 0;

    virtual CommandAcceptance cancelRequest(RequestId requestId) = 0;

signals:
    void connectionStateChanged(ConnectionState state);
    void controlCompleted(const ControlResult &result);
    void ownershipChanged(const OwnershipResult &result);
    void requestStateChanged(RequestId id, RequestState state);
    void requestCompleted(const ModbusRequestResult &result);
};
```

`ControlSubmission`、`RequestSubmission` 和 `CommandAcceptance` 均为“已接受的 ID/确认”或结构化同步拒绝错误的联合类型，不能用 0、空对象或错误字符串表示失败。

### 7.3 同步拒绝与异步失败

同步拒绝发生在命令未进入 Worker 时，例如：

- 参数非法；
- 未连接时提交读写；
- 所有者不匹配；
- 客户端正在关闭或不接收新请求；
- 队列已满；
- ID 耗尽。

命令被接受后发生的打开失败、串口错误、超时、CRC 错误、远端异常和取消必须通过对应完成结果返回，确保每个已接受 ID 恰好有一个终态结果。

## 8. 连接生命周期

### 8.1 状态

```text
Disconnected → Opening → Connected → Closing → Disconnected
                     └──────→ Faulted ──────┘
```

- `Connected` 且所有者为 None 映射到应用的 `CONNECTED_IDLE`。
- 非法状态调用返回 `InvalidState`，不得静默忽略。
- 自动重连不是 MVP；Faulted 后由上层显式 close，再 open。

### 8.2 打开

1. Facade 完整校验配置并进入 Opening。
2. Worker 在线程内创建 QSerialPort，设置全部参数后打开 ReadWrite。
3. Facade 同步参数校验失败时命令不被接受，状态保持 Disconnected。命令已接受后，任一 QSerialPort 参数设置或打开步骤失败时，Worker 关闭并销毁端口，返回结构化 `SerialConfigurationRejected`/`OpenFailed` 并进入 Faulted。
4. 成功后清空接收缓冲和队列，进入 Connected、owner=None。

禁止在已打开端口上只修改部分参数。

### 8.3 关闭

`CloseMode::CancelAll` 是 UI 断开和应用退出的默认行为：

1. Facade 立即停止接收新请求并进入 Closing。
2. 队列中尚未发送的请求按入队顺序完成为 Cancelled，TX/RX 为空。
3. 在途请求停止超时计时，保留已有 TX/RX，完成为 Cancelled。
4. Worker 断开信号处理、清理缓冲并在线程内关闭 QSerialPort。
5. owner 置 None，进入 Disconnected，发出 controlCompleted。

`CloseMode::FinishInFlight` 只允许受控服务关闭：停止接收新请求、取消排队请求、允许当前在途请求到终态后关闭。它不能无限等待，仍受原请求超时约束。

## 9. 所有权与模式交接

### 9.1 所有者

```cpp
enum class CommunicationOwner {
    None,
    Monitor,
    Testing,
    ManualDebug,
};
```

任一时刻至多一个活动所有者。读写请求必须携带当前 owner；不匹配返回 `OwnerMismatch`。

### 9.2 获取和释放

- 只有 Connected 状态可以获取所有权。
- owner=None 时可直接获取目标 owner。
- 同一 owner 重复获取返回幂等成功，但不生成第二份租约。
- owner 不得从 A 直接覆盖为 B；必须执行受控交接。

### 9.3 交接算法

`HandoffMode::FinishInFlight` 为 Monitoring → Testing 的默认方式：

1. 应用状态机先停止旧所有者产生新请求。
2. Facade 对旧所有者关闭接收门禁。
3. 取消旧所有者的全部排队请求。
4. 允许唯一在途请求成功、失败或超时。
5. 旧 owner 置 None，再切换到新 owner。
6. 发出 ownershipChanged 后，新所有者才能提交请求。

`HandoffMode::CancelInFlight` 用于用户明确中止测试或错误恢复：排队和在途请求均取消；Worker 完成重新同步后才切换 owner。

交接过程中提交请求返回 `NotAcceptingRequests`。ManualDebug 与 Monitor/Testing 同样互斥，不允许绕过应用状态机直接使用串口。

## 10. 请求模型与校验

### 10.1 0x03 读取

输入：owner、PDU 0-based 起始地址、寄存器数量和请求选项。

约束：

- `count` 为 1～125；
- `start + count - 1` 不得超过 65535，计算前先扩展到 32 位；
- 成功响应 byte count 必须等于 `count * 2`；
- 每个寄存器按高字节在前解析为 `quint16`；
- 返回 `QVector<quint16>` 和完整事务证据，再由 TASK-004 解码。

### 10.2 0x06 写单寄存器

输入：owner、PDU 0-based 地址、原始 `quint16` 值和请求选项。

- 通信层不判断该地址是否为业务可写；调用方使用 TASK-004 `RegisterWrite`，真实 Slave 对非法地址返回异常。
- 正常响应必须原样回显地址和值，否则为 `WriteEchoMismatch`。
- 写后回读验证由后续参数配置/TestEngine 调度，不在通信层自动追加请求。

### 10.3 队列

- 接受的请求按 RequestId/入队顺序 FIFO。
- Worker 同一时刻最多一个非终态请求。
- 只有当前请求进入终态或完成重新同步后才发送下一请求。
- 队列达到 `maxPendingRequests` 时同步拒绝 `QueueFull`，不分配可观察的 RequestId。
- 不实现隐式重试；重试策略属于 Monitor/TestEngine，且每次重试必须获得新的 RequestId 和独立证据。

## 11. RTU ADU 和帧处理

### 11.1 请求 ADU

```text
0x03: [Slave][03][Start Hi][Start Lo][Count Hi][Count Lo][CRC Lo][CRC Hi]
0x06: [Slave][06][Addr Hi][Addr Lo][Value Hi][Value Lo][CRC Lo][CRC Hi]
```

- CRC 使用 Modbus CRC16，初值 `0xFFFF`，多项式反转表示 `0xA001`。
- 线上的 CRC 低字节先发送。
- `txAdu` 必须是实际传给 `QSerialPort::write()` 的不可变字节数组。
- `write()` 未接受完整 ADU 时，立即产生 `PartialWrite`/`SerialWriteFailed`，不得等待正常响应。

### 11.2 响应长度

- 0x03 正常响应：`5 + byteCount` 字节。
- 0x06 正常响应：固定 8 字节。
- 异常响应：固定 5 字节，功能码为请求功能码 OR `0x80`。
- 在功能码和 byte count 足以判断前，不得猜测完整长度。
- 0x03 响应第三字节出现后，若 byte count 不等于请求数量乘 2，应立即进入协议错误收集/重新同步，不得按错误 byte count 无限等待。

### 11.3 分片、粘连和多余字节

- `readyRead` 每次取得的字节追加到当前请求 RX 缓冲，不能假设一次信号对应一帧。
- 任意逐字节分片和多段分片必须得到同一结果。
- 达到预期长度后立即校验 Slave、功能码、长度、CRC 和请求关联字段。
- 同一请求收到超过预期长度的字节时保留全部原始字节并返回 `UnexpectedTrailingBytes`，不得静默丢弃或把第二段当成下一请求响应。
- 当前无在途请求时收到的字节作为 `UnsolicitedData` 诊断事件保存并丢弃，不能分配给后续请求。

### 11.4 RTU 静默期

- 正常发送前必须确保接收侧已静默至少 3.5 个字符时间。
- 波特率高于 19200 时使用至少 1.75 ms；否则按当前数据位、奇偶校验和停止位计算 3.5 字符时间并向上取整。
- Windows/Qt 定时器只提供 Host 侧调度精度，真实线上时序必须通过 VCP/UART 和 RS485 验证。
- 超时、取消、CRC 或协议帧错误后进入 Resynchronizing：停止发送、丢弃后续输入，直到再次满足静默期，再启动下一请求。

## 12. 超时与取消

### 12.1 超时

- 完整请求 ADU 被 `write()` 接受后启动单请求响应计时。
- 在时限内未形成可判定终态响应时返回 Timeout。
- Timeout 结果保留完整 TX 和已收到的部分 RX；无 RX 时 `rxAdu` 为空。
- 超时不自动重发，不自动断开；Worker 完成重新同步后继续队列。
- 连续超时是否使连接 Faulted 由后续运行策略任务决定，当前不隐式累计阈值。

### 12.2 取消

- Queued 请求：从队列移除并完成为 Cancelled，TX/RX 为空。
- InFlight 请求：标记取消，停止该请求计时，保留已有证据，进入 Resynchronizing 后再处理下一请求。
- 已终态或未知 RequestId：返回 `RequestNotFoundOrCompleted`，不得产生第二个完成结果。
- 取消是尽力而为的本地行为，不能声称已撤回串口线上已经发送的字节。

## 13. 证据模型

```cpp
enum class CrcStatus { NotAvailable, Valid, Invalid };

struct RtuTransactionEvidence {
    RequestId requestId;
    CommunicationOwner owner;
    quint8 serverAddress;
    quint8 functionCode;
    QByteArray txAdu;
    QByteArray rxAdu;
    CrcStatus txCrcStatus;
    CrcStatus rxCrcStatus;
    std::optional<quint16> receivedRxCrc;
    std::optional<quint16> calculatedRxCrc;
    QDateTime enqueuedUtc;
    std::optional<QDateTime> txStartedUtc;
    std::optional<QDateTime> firstRxUtc;
    QDateTime completedUtc;
    std::optional<std::chrono::nanoseconds> queueDelay;
    std::optional<std::chrono::nanoseconds> rtt;
};
```

还必须保存与请求类型对应的 PDU 地址、数量或写入值，建议放在不可变 `RequestDescriptor` 变体中。

证据规则：

- TX CRC 由本端生成，完整 ADU 构造成功时标记 Valid。
- RX 不足 2 个 CRC 字节时为 NotAvailable；足够形成候选帧后记录接收值、计算值及 Valid/Invalid。
- 参数同步拒绝没有事务证据，因为没有 RequestId，也未进入队列。
- 所有异步终态结果，即使失败或取消，也携带当前可获得的证据。
- 日志可格式化为十六进制，但模型保存 `QByteArray`，不以格式化字符串为权威数据。

## 14. 结果与结构化错误

### 14.1 成功结果

```cpp
struct ReadHoldingRegistersResult {
    RequestId requestId;
    device::PduAddress startAddress;
    QVector<quint16> values;
    RtuTransactionEvidence evidence;
};

struct WriteSingleRegisterResult {
    RequestId requestId;
    device::PduAddress address;
    quint16 rawValue;
    RtuTransactionEvidence evidence;
};
```

### 14.2 错误类别

| 类别 | 典型错误码 |
|---|---|
| `InvalidParameter` | EmptyPortName、UnsupportedConfiguration、InvalidServerAddress、InvalidTimeout、InvalidQuantity、AddressRangeOverflow |
| `Connection` | NotConnected、AlreadyOpen、OpenFailed、ClosedByPeer、Closing |
| `Serial` | SerialReadFailed、SerialWriteFailed、PartialWrite、ResourceError、PermissionError |
| `Queue` | QueueFull、NotAcceptingRequests |
| `Ownership` | OwnerMismatch、OwnershipTransitionInProgress |
| `Timeout` | ResponseTimeout |
| `Cancelled` | CancelledByCaller、CancelledByHandoff、CancelledByClose |
| `Crc` | ResponseCrcMismatch、ResponseCrcMissing |
| `Protocol` | WrongServerAddress、UnexpectedFunction、InvalidByteCount、MalformedResponse、WriteEchoMismatch、UnexpectedTrailingBytes、UnsolicitedData |
| `RemoteException` | Modbus 异常码 0x01、0x02、0x03 或保留的未知异常码 |
| `InternalState` | InvalidState、IdExhausted、InvariantViolation、WorkerUnavailable |

### 14.3 错误上下文

`CommunicationError` 至少包含：

- 稳定 category 和 code；
- 可选 OperationId/RequestId；
- ConnectionState、RequestState 和 owner；
- 可选 QSerialPort 错误枚举值；
- 可选 Modbus 功能码、异常码、PDU 地址、期望/实际长度和值；
- 当前可获得的 `RtuTransactionEvidence`；
- 辅助诊断文本。

调用方只依据枚举和结构化字段分支。平台错误文本用于展示和日志，不能成为程序判断条件。

### 14.4 远端异常与本地错误

- 合法 CRC、正确 Slave、功能码带 `0x80` 的响应是 `RemoteException`，表示 Slave 明确拒绝请求。
- 本地超时、坏 CRC、串口错误和解析错误不是 Modbus 异常响应。
- 未知异常码原样保留，不映射为成功或通用协议错误。
- `0x01`、`0x02`、`0x03` 必须有稳定枚举，同时保留原始异常码。

### 14.5 终态错误判定顺序

对同一候选响应按以下顺序返回首个错误，保证生产后端和 Fake 一致：

1. 是否已达到可判定的最小/预期长度；未达到且计时结束为 Timeout；
2. 是否存在预期帧后的多余字节；存在时为 `UnexpectedTrailingBytes`；
3. CRC 是否存在且正确；失败时为 Crc 类错误；
4. Slave 地址、功能码和异常响应结构；
5. 0x03 byte count/数据长度或 0x06 地址和值回显；
6. 成功载荷解析。

若头部已明确不可能形成当前请求的合法响应，Worker 保存当前字节并进入 Resynchronizing，等静默期确定候选帧边界后按上述顺序完成，不把不可信头部直接报告为业务异常。

## 15. 工作线程与对象销毁

### 15.1 创建

1. 应用线程创建 `QThread` 和不持有打开串口的 Worker 壳对象。
2. Worker 移入通信线程。
3. 在线程启动后的初始化槽中创建 QSerialPort 和超时/静默期计时对象。
4. QSerialPort、计时器和协议状态机只在通信线程访问。

不得在应用线程创建并打开 QSerialPort 后再移动对象。

### 15.2 信号连接

- Facade → Worker 的命令使用 `Qt::QueuedConnection`。
- Worker → Facade 的状态和结果使用 queued connection。
- Worker 不保存 QWidget 指针，不调用 UI 方法。
- 跨线程信号参数必须可复制且按 Qt 元类型要求注册。

### 15.3 停止与销毁

正常应用退出顺序：

1. Facade 进入不接收状态。
2. 向 Worker 排队 shutdown 命令。
3. Worker 在线程内取消请求、停止计时器、清缓冲、关闭并销毁 QSerialPort。
4. Worker 发出 stopped。
5. Controller 请求 QThread 退出；使用标准 `finished`/`deleteLater` 生命周期回收 Worker。
6. 仅在应用最终退出阶段对线程执行有界 wait；普通 UI 操作不得阻塞等待。
7. 超过退出时限时记录 InternalState 错误并进入受控失败处理，禁止调用 `QThread::terminate()`。

所有 queued 回调必须通过 QObject 生命周期自动断开或以弱引用检查，禁止捕获裸 Worker 指针形成悬空回调。

## 16. Fake Client 设计

Fake 实现同一 `IModbusClient` 合同，但不创建 QThread、QSerialPort 或真实计时睡眠。

### 16.1 脚本步骤

```cpp
struct FakeStep {
    RequestMatcher expectedRequest;
    FakeOutcome outcome;
    std::chrono::milliseconds virtualDelay;
    QByteArray txAdu;
    QByteArray rxAdu;
};
```

`FakeOutcome` 至少支持：

- 读取成功和写入成功；
- 远端 0x01/0x02/0x03 异常；
- Timeout；
- CRC mismatch；
- Protocol error；
- Serial error；
- 保持 Pending 以测试取消和所有权交接。

### 16.2 确定性

- 使用可注入单调时钟/手动调度器推进虚拟时间，禁止测试依赖 `sleep()`。
- 严格匹配请求顺序、owner、地址、数量、值和超时；不匹配立即给出 FakeScriptMismatch。
- Fake 生成与生产结果相同的 RequestId、状态迁移、证据和错误结构。
- 测试结束时脚本必须完全消费且无未终态请求。

### 16.3 合同测试

同一套 `IModbusClient` 合同测试应同时适用于 Fake 和后续生产 Facade，覆盖：

- 参数同步拒绝；
- FIFO 和单在途；
- 每个接受 ID 恰好一个完成结果；
- 取消 queued/in-flight；
- FinishInFlight/CancelInFlight 交接；
- close 和对象销毁；
- 证据字段完整性。

## 17. 测试策略

### 17.1 无硬件单元测试

- CRC16 已知向量、空数据和单字节变化。
- 0x03/0x06 请求 ADU 和 CRC 字节序。
- 正常、异常、坏 CRC、错误 Slave/功能码/长度/回显响应。
- 逐字节分片、所有合理分片位置、一次读取完整帧和多余字节。
- 参数边界、地址溢出、数量 0/1/125/126。
- 超时、取消、重新同步和无请求时的 unsolicited bytes。

### 17.2 无硬件集成/线程测试

- Fake 队列 FIFO、QueueFull、单在途和所有权交接。
- Facade/Worker 信号线程 ID，证明 QSerialPort 访问不在 UI 线程。
- 打开失败、关闭、故障、应用退出和迟到 queued 回调。
- 重复打开/关闭、快速取消和销毁压力测试。
- QSignalSpy 或确定性调度器，不使用不受控 sleep。

### 17.3 最小公开 API 验证

- 保留 `host.technical.qtserialbus_raw_pdu`，证明 Qt SerialBus Raw 边界为 PDU。
- 新增 `host.technical.qserialport_rtu_public_api`，编译验证 QSerialPort 参数配置、QIODevice 原始读写及异步信号；不打开硬件。
- 此类技术探针不计入产品测试用例数量。

2026-09-03 已在 Qt 6.8.3/MSVC 19.51 环境完成全新 Host 配置和构建，包含上述两个技术探针在内的 5 个 CTest 目标全部通过。该结果只证明公开 API 和无硬件代码可用。

### 17.4 VCP/UART 真实验证

等待 TASK-005 提供可用 Slave 后执行：

- COM 端口枚举与 115200 8N1 打开/关闭；
- 0x03/0x06、异常响应、完整 TX/RX 和回读；
- 超时、复位、断开和恢复；
- 与第三方 Modbus 工具交叉核对 CRC 和寄存器值。

结果必须标记为“VCP/UART 协议验证”，不能写成 RS485 已通过。

### 17.5 RS485 真实验证

等待 TASK-002 完成后追加：

- 收发器、DE/RE、半双工方向切换；
- 终端、偏置、波形和长线稳定性；
- RS485 断线/恢复和连续运行。

未执行前必须标记“未在真实 RS485 环境验证”。

## 18. 可观测性与安全

- 日志记录 RequestId、owner、状态迁移、TX/RX、RTT 和结构化错误，但不复制业务解释。
- 原始字节以长度受控方式保存；长期稳定性测试应支持轮转或上限，防止内存/磁盘无界增长。
- 不记录无关敏感信息；串口名称不视为密钥，但报告应避免泄露用户目录等环境细节。
- Raw/错误注入若未来获批，只能在 Testing/ManualDebug 所有权下使用，并需要显式 UI 提示和独立测试用例。

## 19. 验收追踪

| TASK-006 验收项 | 对应章节 |
|---|---|
| 两方案可核验比较和单一推荐 | ADR 3～6 |
| 打开、关闭、配置、0x03、0x06、超时、错误、TX/RX、RTT | 5～14 |
| 同一串口最多一个在途请求 | 4、10.3 |
| UI 非阻塞和线程归属 | 7、15 |
| 关闭、取消、超时和销毁顺序 | 8、12、15 |
| 完整结果和证据 | 6、13、14 |
| 模式所有权交接 | 9 |
| Fake 队列、错误、取消和切换 | 16 |
| 无硬件/VCP/RS485 分层验证 | 17 |
| 不实现生产后端 | 3.2、20 |

## 20. 评审结论与门禁

### 20.1 一致性

- 符合 PRD 的异步通信、单通道所有权、0x03/0x06、完整证据和结构化错误要求。
- 延续架构推荐的单通信工作线程和应用状态机，不让 Worker 操作 QWidget。
- 使用 TASK-004 PDU 地址和原始寄存器边界，不复制领域编解码。
- 未把 Raw、0x04、0x10、自动重连或 RS485 实施擅自加入 MVP。

### 20.2 已知风险

- 自建 RTU Master 的实现与验证成本高于 Qt SerialBus。
- Windows 定时精度和真实串口分片行为只能通过后续实物验证。
- 超时后的极迟响应可能与后续事务相互干扰；本规范通过重新同步、严格 Slave/功能码/长度校验和不自动重试降低风险，但仍需压力测试。
- 证据中的 Host 时间戳不是物理层分析仪时间戳，报告必须使用准确口径。

### 20.3 门禁状态

ADR 和本 Spec 已完成内部一致性、可实现性和可测试性评审，用户已于 2026-09-03 确认推荐架构，文档门禁通过。

后续约束：

- `docs/architecture.md` 已同步为 QSerialPort 受控 RTU 后端；
- 生产 `IModbusClient`、CommunicationWorker、RtuCodec 和 QSerialPort 后端必须在独立实现任务中开发；
- 生产实现必须遵守本 Spec，并完成其中定义的纯协议、队列、线程、Fake、VCP/UART 和后续 RS485 分层验证；
- TASK-006 的文档门禁通过不等于生产通信功能已经实现。
