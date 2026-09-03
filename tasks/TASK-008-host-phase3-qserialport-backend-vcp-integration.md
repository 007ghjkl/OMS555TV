# TASK-008 Host Phase 3 QSerialPort 异步后端与 VCP 联调

## 目标

在 `TASK-007` 的通信契约、RTU 核心和合同测试基础上，实现 QSerialPort 生产后端、单通信工作线程、串行请求队列和完整事务证据，并通过 USART2/ST-LINK VCP 与 TASK-005 固件完成真实 Phase 3 联调，使 Qt 能稳定读取和写入 STM32 寄存器。

## 背景

PRD 和架构要求 UI 非阻塞、同一串口只有一个请求执行器、监控与测试复用同一通信层，并保存完整 TX/RX、时间戳和 RTT。`TASK-006` 已完成架构决策，`TASK-005` 已提供通过 VCP/UART 验证的 Modbus Slave。完成 `TASK-007` 后，剩余风险集中在 QSerialPort 生命周期、跨线程信号、超时/取消、串口分片、重新同步和真实 Windows VCP 行为。

本任务关闭这些 Phase 3 风险，但仍不进入实时监控 UI、TestEngine 或 RS485 电气层。

## 范围

- 实现应用线程中的 `IModbusClient` Facade 和通信线程中的 Worker。
- Worker 在线程内创建、配置、打开、访问、关闭和销毁 QSerialPort。
- 实现有界 FIFO 请求队列，同一时刻最多一个在途请求。
- 实现异步打开、关闭、0x03、0x06、取消和所有权获取/释放/交接。
- 实现请求超时、串口错误、分片接收、候选帧完成、静默期和 Resynchronizing。
- 实现完整 TX/RX、时间戳、queue delay、RTT、CRC 和结构化错误证据。
- 实现应用退出时的受控 Worker/QThread 清理，不使用 `QThread::terminate()`。
- 使用 Fake 或可控传输替身完成队列、线程、生命周期和错误路径测试。
- 使用运行时枚举出的 ST-LINK VCP 与 TASK-005 固件完成真实 115200 8N1、Slave ID 1 联调。
- 复用 `TASK-004` 的读块和 `RegisterCodec`，验证原始寄存器与领域模型一致。
- 对阈值执行 0x06 写入、回读和恢复，测试结束后恢复基线默认值。
- 形成独立的 VCP/UART Phase 3 联调记录，保存环境、固件/Host 版本、请求、响应、RTT 和结果。

## 非范围

- 不实现实时监控轮询、趋势曲线、参数配置 UI 或通信调试 UI。
- 不实现 TestEngine、JSON 用例、半自动测试、日志归档或 HTML 报告。
- 不增加公共 Raw Frame、错误 CRC 注入、0x04、0x10、广播或自动重连。
- 不实施 TTL-RS485 收发器、DE/RE、终端、偏置或真实 RS485 验证。
- 不修改 Firmware 协议和寄存器语义；联调发现缺陷时先登记并按归属修复。
- 不将 VCP/UART 结果表述为 RS485 电气层通过。

## 依赖

- `TASK-005` 已完成，Firmware Phase 2 Slave 可通过 VCP/UART 访问。
- `TASK-006` ADR 和 Host Phase 3 通信 Spec 已确认。
- `TASK-007` 已完成并提供 RTU Codec、公共类型、Fake 和合同测试。
- `TASK-004` 的寄存器读块与领域编解码可复用。
- ST-LINK VCP 可用；COM 端口必须运行时枚举，不能写死 COM3、COM5 或历史端口号。
- `TASK-002` 保持延期，不阻塞本任务。

## 实现门禁

1. `TASK-007` 未完成或合同测试未通过时不得开始生产串口后端。
2. 开始实现前必须复核 QSerialPort、Worker、计时器和 QThread 的创建/销毁线程归属。
3. 必须先用无硬件测试证明队列、超时、取消、交接和关闭状态机，再访问真实 VCP。
4. 实机联调前记录当前端口枚举、板卡标识、固件版本和通信参数；不得沿用历史 COM 号猜测。
5. 写阈值前保存当前值，测试完成或失败清理时尽力恢复，并在记录中写明最终值。
6. 若需改变已确认的线程模型、公共通信契约或协议行为，必须停止实现并请求用户确认。

## 实现要求

1. Facade 公共方法在应用线程快速返回，不阻塞等待串口、超时或线程退出。
2. Facade 与 Worker 使用 queued connection；Worker 不持有 QWidget 指针。
3. QSerialPort、超时计时器、静默期计时器和协议状态机只在通信线程访问。
4. 队列达到配置上限时同步拒绝 QueueFull；不实现无界积压或隐式重试。
5. 完整请求 ADU 被 `write()` 接受后才启动响应超时；部分写入必须形成结构化失败。
6. `readyRead` 数据追加到当前请求缓冲，不假设一次信号对应一帧。
7. 超时、取消、CRC 或协议错误后完成重新同步，再发送下一请求。
8. 关闭和模式交接遵守 Spec 的 CancelAll、FinishInFlight 和 CancelInFlight 语义。
9. 已接受命令必须恰好完成一次；关闭、迟到信号和对象销毁不得产生悬空回调。
10. 日志/测试记录消费不可变结果，不从 QSerialPort 缓冲区重新解释证据。
11. 实机读取使用 `RegisterMap::readBlocks()` 等既有地址定义，不在联调代码散落重复地址常量。
12. 真实 0x06 写入只针对四路阈值，先执行 Host 范围校验，再由 Slave 响应和回读共同判定。

## 验收标准

1. Qt 6.8.3/MSVC 2022 x64 全新配置、构建和全部 Host CTest 通过，无新增警告。
2. 自动测试证明 Facade 在应用线程、Worker/QSerialPort 在通信线程，完成信号回到 Facade 线程。
3. FIFO、单在途、QueueFull、超时、queued/in-flight 取消、所有权交接和重复开关均有测试。
4. 应用退出和快速创建/关闭压力测试不死锁、不使用 `QThread::terminate()`，无二次完成或明显资源泄漏。
5. 运行时成功枚举并打开当前 ST-LINK VCP，使用 115200 8N1、无流控、Slave ID 1。
6. Qt 通过 0x03 正确读取 TASK-005 的全部合法读块，完整 TX/RX、CRC 和 RTT 均可追溯。
7. Qt 使用 TASK-004 编解码得到的温度、光敏毫伏、状态、版本、运行时间和阈值与原始寄存器一致。
8. 四路阈值分别完成合法写入、0x06 回显、0x03 回读验证和最终基线恢复。
9. 非法地址、非法值等 Slave 0x02/0x03 响应被识别为结构化 RemoteException，不误判为本地协议错误。
10. 在当前 VCP 环境完成连续请求稳定性验证；至少复用 TASK-005 的 500 次 0x03 口径，并记录成功、失败、超时及 RTT 统计。
11. 人为制造的无响应或端口断开可进入明确超时/串口错误，后续显式关闭并重新打开后能够恢复通信。
12. UI smoke test 在通信测试期间仍可响应，生产通信路径没有在 UI 线程执行阻塞等待。
13. 联调报告明确标注“VCP/UART 协议验证”，RS485 仍为未验证。
14. 最终 Review 无必须修复项，Phase 3 达到“Qt 可以稳定读取 STM32 数据”的验收条件。

## 测试要求

- 无硬件线程测试：对象归属、queued connection、FIFO、单在途、队列满、超时、取消、交接和关闭。
- 无硬件错误测试：打开失败、配置拒绝、部分写、串口错误、分片、尾随字节、迟到响应和 unsolicited data。
- 生命周期测试：重复打开/关闭、请求中关闭、快速销毁和应用退出。
- VCP 功能测试：全部读块、四路阈值写回读、0x02/0x03 和版本/状态一致性。
- VCP 稳定性测试：连续 500 次 0x03，记录真实 RTT 分布和失败分类。
- VCP 恢复测试：无响应或断开、明确错误、显式关闭/重开和恢复读取。
- 回归测试：运行全部 Host CTest；Firmware 不因本任务无关原因重新修改。

## 文档同步

- 创建 `docs/test_results/task008_host_vcp_modbus_integration.md` 保存真实联调证据。
- 将 Host 构建、测试和 VCP 使用方法同步到 README。
- 若发现缺陷，按 Host/Firmware 归属登记到 `docs/bug_records.md`，包含复现、证据、根因和回归结果。
- 完成后将本任务标记为已完成，并明确 `TASK-002`/RS485 仍延期。
- 不得提前声称 Phase 4 监控、TestEngine 或报告已经实现。

## 当前状态

待实施。任务文档已于 2026-09-03 创建，尚未派发；必须等待 TASK-007 完成后再开始。
