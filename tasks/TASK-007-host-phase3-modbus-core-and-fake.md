# TASK-007 Host Phase 3 Modbus RTU 核心、通信契约与 Fake

## 目标

依据已确认的 Host Modbus ADR 和 `specs/host_phase3_modbus_communication.md`，实现不依赖真实串口的 Modbus RTU Master 核心、完整异步通信契约及确定性 Fake，为后续 QSerialPort 工作线程和 VCP 实机联调提供稳定、可复用、可合同测试的基础。

## 背景

`TASK-006` 已确认采用“QSerialPort 受控 RTU 后端 + 单通信工作线程 + 串行请求队列”，但当前生产 `IModbusClient` 仍只有连接状态接口。若直接把协议编解码、线程队列、QSerialPort 和实机联调放入同一任务，协议错误与并发错误将难以隔离。

本任务先完成无硬件核心：由 `communication` 模块统一生成和解析完整 RTU ADU，定义请求、结果、证据和结构化错误，并提供与生产契约一致的 Fake。真实串口、工作线程和硬件访问留给 `TASK-008`。

## 范围

- 按已评审 Spec 扩展生产 `IModbusClient` 异步契约。
- 定义串口/连接配置、请求选项、连接状态、请求状态、所有权、提交结果、控制结果及稳定 ID 类型。
- 定义不可变的请求描述、成功结果、事务证据和结构化通信错误。
- 实现 Modbus CRC16 和完整 RTU ADU 编解码核心。
- 实现 0x03 Read Holding Registers 请求生成与正常/异常响应解析。
- 实现 0x06 Write Single Register 请求生成与正常/异常响应解析。
- 固化候选响应的长度、尾随字节、CRC、Slave、功能码、byte count 和写回显判定顺序。
- 实现确定性 `FakeModbusClient`，支持成功、远端异常、超时、CRC、协议、串口、Pending 和取消脚本。
- 为 Fake 提供可注入的单调时钟或手动调度器，测试不得依赖真实 `sleep()`。
- 建立可复用于 Fake 和后续生产 Facade 的 `IModbusClient` 合同测试。
- 复用 `TASK-004` 的 `device::PduAddress` 和原始寄存器边界，不复制领域编解码。

## 非范围

- 不实现或打开 QSerialPort，不创建生产通信工作线程。
- 不连接 STM32，不执行 VCP/UART 或 RS485 实机测试。
- 不实现 Monitor、参数配置、通信调试 UI、TestEngine、日志归档或报告。
- 不增加公共 Raw Frame/错误 CRC 发送接口。
- 不实现 0x04、0x10、广播、多串口、自动重连或隐式重试。
- 不修改 Firmware、`TASK-002` 或寄存器语义。

## 依赖

- `TASK-004` 已完成，Host 设备模型、PDU 地址和寄存器编解码可复用。
- `TASK-006` 已完成，ADR 与 `specs/host_phase3_modbus_communication.md` 已确认并评审。
- Qt 6.8.3、MSVC 2022 x64、CMake 和 Ninja 的 Host 构建基线可用。
- `TASK-005` 不阻塞本任务的无硬件实现；它是后续 `TASK-008` 实机联调的依赖。

## 实现门禁

1. 开始编码前必须逐项对照 Host Phase 3 通信 Spec；若发现契约不可实现或存在冲突，先修订并评审 Spec，不得在代码中静默改变语义。
2. `IModbusClient` 公共接口属于已确认架构的实现，不得额外暴露 QSerialPort、Worker 或 UI 类型。
3. 每个已接受的 OperationId/RequestId 必须恰好产生一个终态结果；同步拒绝不得伪造已入队 ID。
4. Fake 和生产接口必须共享结果、证据和错误类型，不得为测试创建第二套简化契约。
5. Fake 的时间推进必须确定且快速，禁止依靠墙钟等待规避调度设计。
6. 新增类型应按职责拆分，避免把接口、Codec、Fake 和所有模型堆入单个大文件。

## 实现要求

1. CRC16 使用初值 `0xFFFF`、反转多项式 `0xA001`，线上 CRC 低字节先发送。
2. 0x03 数量只接受 1～125；地址区间计算必须防止 16 位溢出。
3. 0x06 正常响应必须原样回显地址和值。
4. 完整候选响应按 Spec 规定的终态错误顺序判定，坏 CRC 不得被误报为远端异常或业务错误。
5. 合法异常响应保留原始异常码，并稳定区分 0x01、0x02、0x03 与未知异常码。
6. 事务证据保存二进制 `QByteArray`、UTC 时间、可选单调时长、CRC 状态、请求描述和关联 ID，不以格式化日志字符串作为权威数据。
7. 参数、状态、所有权、QueueFull 和 ID 耗尽属于同步拒绝；已经接受后的超时、取消和通信错误通过异步终态返回。
8. Fake 严格匹配 owner、请求顺序、地址、数量、写入值和超时；脚本不匹配必须返回稳定错误。
9. Fake 测试结束时必须能够检查脚本已完全消费、没有未终态请求。
10. 所有新增公共 Qt 信号参数满足元类型和可复制要求，为 `TASK-008` 的 queued connection 做准备。

## 验收标准

1. Host 在 Qt 6.8.3/MSVC 2022 x64 下全新配置、构建和全部 CTest 通过。
2. 生产 `IModbusClient` 具备 Spec 定义的非阻塞打开、关闭、所有权、0x03、0x06 和取消契约，但不依赖真实串口实现。
3. CRC16 已知向量、空输入和单字节变化测试通过。
4. 0x03/0x06 请求 ADU 与标准字节序、CRC 和 TASK-005 实测帧一致。
5. 正常响应、0x01/0x02/0x03、未知异常、坏 CRC、错误 Slave/功能码/长度/byte count/写回显及尾随字节均有测试。
6. 逐字节和所有关键分片位置产生一致解析结果；解析核心不假设一次输入就是完整帧。
7. 参数边界覆盖数量 0/1/125/126、地址末端和地址溢出。
8. Fake 可确定复现 FIFO、单在途、QueueFull、成功、超时、异常、取消及所有权交接。
9. 合同测试证明每个已接受 ID 恰好一个终态结果，已终态请求不会二次完成。
10. 测试不使用不受控 `sleep()`，不访问串口或真实硬件。
11. `TASK-004` 的既有设备编解码测试和 Host 应用 smoke test 无回归。
12. 最终 Review 无必须修复项，且改动不包含 Firmware、监控 UI、TestEngine 或 `TASK-002`。

## 测试要求

- Codec 单元测试：CRC、请求 ADU、正常响应、异常响应、长度、分片、尾随字节和错误判定优先级。
- 模型单元测试：配置校验、ID、不可变证据、结构化错误和请求描述。
- Fake 单元测试：脚本匹配、虚拟时间、Pending、取消、队列满和脚本消费检查。
- 合同测试：同步拒绝、FIFO、单在途、恰好一次完成、关闭和所有权交接。
- 回归测试：运行全部 Host CTest。

## 文档同步

- 若实现暴露 Spec 缺口，先更新 `specs/host_phase3_modbus_communication.md` 并记录评审原因。
- 将新增模块、测试命令和真实结果同步到 README 与本任务。
- 不得把无硬件测试描述为 QSerialPort、VCP/UART 或 RS485 已验证。

## 当前状态

待实施。任务文档已于 2026-09-03 创建，尚未派发。
