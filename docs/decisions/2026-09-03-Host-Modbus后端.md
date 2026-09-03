# Host Modbus RTU 后端选择

> 状态：已接受
> 日期：2026-09-03
> 对应任务：`TASK-006 Host Phase 3 Modbus 后端决策与异步通信规范`

## 1. 背景

Host 必须同时满足两类需求：

1. 监控和参数配置所需的标准 Modbus RTU 0x03、0x06、超时和异常响应；
2. 测试平台所需的完整 TX/RX RTU ADU、CRC 结果、时间戳、RTT，以及未来可能增加的错误 CRC 和异常时序注入。

当前 `IModbusClient` 只有连接状态接口，尚未绑定具体后端。架构草案推荐“单通信工作线程 + 串行请求队列”，并要求监控与测试共用同一通信边界。

Phase 0 已验证：Qt SerialBus 的 `sendRawRequest()` 和 `rawResult()` 处理的是 PDU，不是包含 Slave 地址和 CRC 的完整 RTU ADU。公开 API 不能取得线上完整收发字节，也不能发送故意损坏 CRC 的帧。Qt 私有 API 不属于可选方案。

本 ADR 比较 Qt SerialBus 标准后端与 QSerialPort 受控 RTU 后端，并提出单一推荐方案。用户已于 2026-09-03 确认该方案，核心通信架构已同步到 `docs/architecture.md`。

## 2. 决策驱动因素

按优先级排序：

1. 完整、可追溯且不伪造的 TX/RX RTU ADU 证据；
2. 同一串口最多一个在途请求，监控、测试和手动调试共用同一路径；
3. UI 不阻塞，线程归属、取消和关闭行为可证明；
4. 正确支持 0x03、0x06、CRC16、标准异常响应和结构化错误；
5. 能在无硬件环境用 Fake 和纯协议测试覆盖队列、分帧、超时和错误路径；
6. 为未来错误 CRC/Raw Frame 注入保留受控扩展点，但不在本任务擅自增加 MVP 功能；
7. 控制实现与长期维护成本。

## 3. 方案 A：Qt SerialBus 作为主后端

### 3.1 核心思路

使用 `QModbusRtuSerialClient` 承担串口、RTU 帧、标准请求、超时和 Modbus 异常响应。Host 在其上封装 `IModbusClient`、队列和领域结果。

### 3.2 优点

- Qt 已实现标准 Modbus Client 生命周期和请求/响应对象。
- 0x03、0x06 和标准异常响应的初始实现量较少。
- 与 Qt 对象模型、信号槽和错误枚举集成自然。
- 普通监控功能可以较快落地。

### 3.3 缺点

- `sendRawRequest()` 只接收 PDU，不能控制完整 RTU ADU。
- `rawResult()` 返回解析后的 PDU，不是串口线上实际接收的完整字节。
- 公开 API 无法保存包含 Slave 地址和 CRC 的真实 TX/RX ADU。
- 无法发送故意损坏 CRC 的 RTU 帧，也无法精确控制帧间字节时序。
- `intermediateErrors()` 可报告部分 CRC 类错误，但不能提供损坏响应的完整原始字节，也不能支持主动故障注入。

### 3.4 补齐缺口的候选方式

| 补齐方式 | 结论 | 原因 |
|---|---|---|
| 根据 PDU、Slave 地址和 CRC 重新构造 ADU | 不接受 | 重构值不是线上实际字节，不能作为原始证据，尤其无法证明收到的坏帧内容 |
| 使用 Qt 私有头文件或内部日志 Hook | 不接受 | 违反任务门禁，版本兼容性和可维护性不可控 |
| 同时打开第二个 QSerialPort 旁路监听 | 不接受 | 同一端口通常不能被两个拥有者可靠打开，且引入第二条时序路径 |
| 标准请求用 SerialBus，Raw/故障注入用另一套 QSerialPort | 不接受 | 形成两套协议与队列路径，监控与测试行为不再一致，缺陷可能只存在于其中一套 |
| 外部串口分析仪被动采集 | 仅可作额外验证 | 增加硬件依赖，不能替代 Host 自身每条请求的证据模型，也不适合日常无硬件测试 |

### 3.5 适用场景

适用于只要求标准 Modbus 读写、对完整 RTU ADU 留证和错误注入没有要求的普通业务客户端。本项目的测试证据要求超出该边界。

## 4. 方案 B：QSerialPort 受控 RTU 后端

### 4.1 核心思路

使用一个通信工作线程独占一个 `QSerialPort`。Host 自行实现最小 Modbus RTU Master 状态机、CRC16、0x03/0x06 ADU 编解码、串行队列、超时、分帧、异常响应和结构化证据。

标准业务请求先转换为受控 RTU 事务，再进入唯一队列。未来若经独立任务批准 Raw Frame/故障注入，也必须复用同一线程、串口、队列、收发缓冲和证据模型，不创建第二套后端。

### 4.2 优点

- TX 为实际交给 `QSerialPort::write()` 的完整 ADU，RX 为 `readAll()` 收到的原始字节，可保存 Slave 地址、PDU 和 CRC。
- CRC 计算值、接收值和校验结果可完整进入证据模型。
- 可以统一控制请求队列、帧边界、超时、取消、恢复静默期和未来故障注入。
- 监控、测试和手动调试共享同一协议实现和物理通道所有权。
- 协议核心可以拆为无 Qt I/O 的纯函数/状态机，用测试向量验证。

### 4.3 缺点

- 需要自行维护 RTU Master 状态机和错误恢复，初始实现量明显大于方案 A。
- 必须正确处理串口分片、粘连、迟到响应、部分写、CRC 错误、异常响应和关闭竞态。
- 若模块边界控制不严，容易把串口、协议、队列和业务解释混成大类。
- 真实 RTU 时序仍需 VCP/UART 和后续 RS485 实物验证；单元测试不能替代电气层验证。

### 4.4 风险缓解

- 只实现 MVP 功能码 0x03、0x06，不提前加入 0x04、0x10。
- 将 CRC、ADU 编解码和响应校验拆为无 I/O 的纯 C++ 组件。
- 使用已知 Modbus 测试向量、逐字节分片、任意分片组合、粘连帧、坏 CRC 和异常响应测试。
- 串口、队列和状态机只存在于一个工作线程；UI 只通过异步 Facade 交互。
- 所有完成路径共用不可变结果和证据模型，避免成功、超时、取消分别拼装日志。
- 与第三方 Modbus Master/Slave 交叉验证，但不把第三方工具作为生产依赖。
- 生产实现单独立项，接受专门的线程、协议和关闭顺序代码审查。

## 5. 能力矩阵

| 能力 | 方案 A：Qt SerialBus | 方案 B：QSerialPort 受控 RTU |
|---|---|---|
| 串口打开、关闭、参数配置 | 支持 | 支持，需封装 |
| 标准 0x03、0x06 | 支持 | 支持，需实现 |
| 异步请求 | 支持 Qt Reply | 支持，需状态机和队列 |
| Modbus 异常响应 | 支持解析 | 支持，需实现 |
| 本地超时与串口错误 | 支持 | 支持，需实现 |
| 完整 TX RTU ADU | 公开 API 不提供 | 支持 |
| 完整 RX RTU ADU | 公开 API 不提供 | 支持 |
| CRC 接收值与计算值留证 | 不完整 | 支持 |
| 主动发送错误 CRC | 不支持 | 技术上支持，功能仍需单独批准 |
| 精确管理 RTU 静默期和恢复 | 不透明 | 支持，需实现和验证 |
| 监控与测试同一路径 | 标准请求可共用，Raw 会分叉 | 可以统一 |
| 初始实现成本 | 低 | 中高 |
| 长期协议维护责任 | 主要由 Qt 承担 | 由项目承担 |
| 满足当前完整证据要求 | 否 | 是 |

## 6. 推荐决策

推荐选择方案 B：QSerialPort 受控 RTU 后端。

推荐理由不是功能数量，而是证据真实性和单路径约束。完整 TX/RX、CRC 和 RTT 是本项目的核心产品价值，而不是可选调试信息。方案 A 无法通过公开 API 给出线上完整 ADU；任何重构或双后端补丁都会削弱证据可信度或制造两套协议行为。

推荐的逻辑结构为：

```text
应用状态机 / Monitor / TestEngine / Manual Debug
                    │
                    ▼
          IModbusClient 异步 Facade
                    │
          所有权门禁 + 串行请求队列
                    │ queued invocation
                    ▼
          CommunicationWorker（单线程）
          ├── RtuCodec / CRC / 响应校验
          ├── 超时、取消、恢复静默期
          └── QSerialPort（线程内创建和销毁）
                    │
                    ▼
            VCP/UART；后续 RS485
```

`device` 模块继续负责寄存器到领域模型的解释，通信后端只返回原始 `quint16` 和 RTU 证据，不复制 TASK-004 编解码逻辑。

## 7. Raw Frame 与故障注入边界

- 当前生产 `IModbusClient` 只承诺 0x03 和 0x06，不因本 ADR 自动增加 Raw Frame 产品功能。
- 受控后端内部以完整 ADU 事务运行，因此为未来扩展保留能力。
- 若后续批准 Raw Frame/错误 CRC 注入，应增加受权限和所有权约束的诊断接口，并复用同一 `CommunicationWorker`、串行队列、QSerialPort 和证据模型。
- Raw 请求只允许 Testing 或 ManualDebug 所有者，禁止 Monitor 使用。
- 未经单独 Spec 和测试，不得暴露任意字节发送入口给 UI。

## 8. 影响

### 8.1 正面影响

- 可以逐字节证明实际交给串口和从串口收到的 RTU 数据。
- 标准请求、协议异常和未来故障注入共享同一通信路径。
- 日志、测试结果和报告可以消费统一证据模型。
- 线程和所有权模型可在 Fake 环境复现。

### 8.2 负面影响

- Host Phase 3 实现任务规模增大，需要独立的协议、线程和集成测试。
- 项目承担 RTU Master 正确性和长期维护责任。
- 在真实硬件联调完成前，只能证明纯协议和调度逻辑，不能声称串口/RS485 时序已经验证。

## 9. 回退方案

`IModbusClient` 保持后端无关。若受控 RTU 后端在后续实现中出现无法接受的进度或质量风险，可重新评审 Qt SerialBus 适配器，但必须满足以下条件：

1. 不降低已确认的证据要求，或由产品所有者明确批准降低范围；
2. 同一运行实例仍只有一个活动后端和一个串口所有者；
3. 不在 Monitor/TestEngine 内复制协议实现；
4. 通过同一 `IModbusClient` 合同测试；
5. ADR 重新进入评审，不静默切换。

外部串口分析仪可以作为联调交叉证据，但不是生产回退后端。

## 10. 对后续任务的影响

- 已更新 `docs/architecture.md`，把生产后端标记为“QSerialPort 受控 RTU”。
- 新建独立的 Host Phase 3 实现任务；TASK-006 不实现生产后端。
- 实现任务先完成纯 CRC/ADU/响应验证，再实现工作线程和 QSerialPort。
- TASK-005 完成后，通过 VCP/UART 验证协议和真实 TX/RX。
- TASK-002 完成后追加 RS485 电气层、DE/RE、终端和偏置验证；本 ADR 不实施 TASK-002。
- TestEngine 和 Monitor 只依赖 `IModbusClient`，继续复用 TASK-004 `device` 编解码。

## 11. 已验证证据

- Qt 6.8.3 公开 API 的 `QModbusClient::sendRawRequest()` 和 `QModbusReply::rawResult()` 已由 `host.technical.qtserialbus_raw_pdu` 编译测试验证，其边界为 PDU。
- Qt 6.8.3 公开 `QSerialPort`/`QIODevice` API 提供串口参数、`write(QByteArray)`、`readAll()`、`readyRead`、`bytesWritten` 和 `errorOccurred`；由 TASK-006 最小测试进行编译与无硬件验证。
- 上述测试只证明公开 API 可用，不证明真实串口、VCP/UART 或 RS485 行为。

## 12. 确认记录

用户于 2026-09-03 明确批准以下决策：

> Host Phase 3 采用“QSerialPort 受控 RTU 后端 + 单通信工作线程 + 单串行请求队列”，未来 Raw/错误注入复用同一后端。

确认后的执行边界：

- 本 ADR 状态为“已接受”；
- `docs/architecture.md` 已同步后端决策；
- `specs/host_phase3_modbus_communication.md` 作为后续生产实现基线；
- TASK-006 到此完成，生产通信后端必须在独立实现任务中开发和验收。
