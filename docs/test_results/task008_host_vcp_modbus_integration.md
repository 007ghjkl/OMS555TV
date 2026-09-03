# TASK-008 Host VCP/UART Modbus RTU 联调记录

> 日期：2026-09-03
>
> 结论：通过
>
> 范围声明：本文仅证明 Qt/QSerialPort 经 ST-LINK VCP/USART2 的 Modbus RTU 协议行为，不代表 TTL-RS485 收发器、DE/RE、终端、偏置或 RS485 电气层已经验证。

## 1. 测试对象与环境

| 项目 | 实测值 |
|---|---|
| 开发板 | NUCLEO-F411RE / STM32F411RET6 |
| 实物标识 | `MB1136-F411RE-C04`，编号 `A232203276` |
| ST-LINK VCP | 运行时枚举为 COM3；SN `0671FF555185754867222928` |
| USB 标识 | VID `0x0483`，PID `0x374B` |
| 串口参数 | 115200 8N1，无流控 |
| Modbus | RTU，Slave ID 1，默认响应超时 500 ms，不重试 |
| Firmware | TASK-005 固件，版本寄存器 0.2 |
| Host | Qt 6.8.3、MSVC 19.51、C++17、TASK-008 工作树 |
| 生产后端 | `QSerialPortModbusClient` + 单 `CommunicationWorker` 线程 |

联调程序未使用历史 COM 号。运行时枚举结果只有一个串口：

```text
PORT name=COM3
description=STMicroelectronics STLink Virtual COM Port
manufacturer=STMicroelectronics
serial=0671FF555185754867222928
vid=0x0483 pid=0x374B
```

## 2. 无硬件构建与自动测试

- 使用全新 `build-host-task008-final` 目录配置并构建；Qt 6.8.3、MSVC 19.51、Ninja 构建成功，无新增编译警告。
- Host CTest 9/9 通过；新增 `host.communication.qserialport_backend`。
- 新后端测试证明 Facade 和完成信号位于应用线程，传输对象、Worker 和计时器位于独立通信线程。
- 覆盖参数同步拒绝、打开失败、逐字节分片、FIFO/单在途、QueueFull、queued/in-flight 取消、重复取消、响应超时、超时后恢复、部分写、串口资源错误、CRC 错误、尾随字节、重新同步、FinishInFlight/CancelAll、所有权交接和 20 轮快速创建/关闭/销毁。
- 测试未使用 `sleep()`、阻塞串口等待、`QThread::terminate()` 或真实硬件。
- `host.application.smoke` 通过；生产串口路径没有在 UI 线程执行阻塞等待。

## 3. 全部合法读块与领域模型

生产 Facade 依次读取 `RegisterMap::deviceSnapshotReadBlocks` 的五个合法块，并由 TASK-004 `RegisterCodec` 组合快照。代表性结果如下：

| PDU 范围 | TX | RX | RTT |
|---|---|---|---:|
| 0～4 | `01 03 00 00 00 05 85 C9` | `01 03 0A 01 0F 01 0D 01 0E 00 FA 03 E1 A1 16` | 6.134 ms |
| 9～12 | `01 03 00 09 00 04 94 0B` | `01 03 08 02 58 02 58 02 58 01 90 6C 73` | 4.923 ms |
| 19～20 | `01 03 00 13 00 02 35 CE` | `01 03 04 00 00 00 21 3A 2B` | 5.599 ms |
| 29～31 | `01 03 00 1D 00 03 95 CD` | `01 03 06 00 00 05 98 00 00 A0 56` | 5.605 ms |
| 39～40 | `01 03 00 27 00 02 74 00` | `01 03 04 00 00 00 02 7B F2` | 4.943 ms |

该次快照解析为：A/B/C 温度 27.1/26.9/27.0 ℃，模拟环境温度 25.0 ℃，光敏电压 993 mV，告警字 `0x0000`，状态字 `0x0021`，通信错误计数 0，运行时间 1432 s，Firmware 0.2。原始寄存器、字序、缩放、状态位和领域模型一致。

所有成功结果均保存完整 TX/RX、CRC 状态、UTC 入队/发送/首字节/完成时间、单调时钟 queue delay 和 RTT。报告中的 RTT 是 Host API 观测值，不表述为物理线路测量。

## 4. 四路阈值写入、回读与恢复

写入前基线读取为 `[600, 600, 600, 400]`。测试值先通过 TASK-004 `encodeAlarmThreshold()` 做通道、范围和 0.1 ℃ 精度校验，再执行 0x06 和独立 0x03 回读：

| 通道 | 工程值 | PDU 地址/原始值 | 0x06 回显 | 0x03 回读 |
|---|---:|---|---|---|
| A | 55.0 ℃ | 9 / `0x0226` | 通过 | 通过 |
| B | 80.0 ℃ | 10 / `0x0320` | 通过 | 通过 |
| C | -40.0 ℃ | 11 / `0xFE70` | 通过 | 通过 |
| 环境 | 45.0 ℃ | 12 / `0x01C2` | 通过 | 通过 |

代表性负温度写入帧：

```text
TX 01 06 00 0B FE 70 B9 8C
RX 01 06 00 0B FE 70 B9 8C
```

测试结束时逐路恢复保存的原始基线，并最终整块回读为 `[600, 600, 600, 400]`。最终恢复结论为 `THRESHOLD_RESTORED=YES`。

## 5. 远端异常

| 场景 | TX | RX | Host 结构化结果 |
|---|---|---|---|
| 读取保留地址 5 | `01 03 00 05 00 01 94 0B` | `01 83 02 C0 F1` | `RemoteException / ModbusIllegalDataAddress / 0x02` |
| 阈值地址 9 写入 801 | `01 06 00 09 03 21 99 20` | `01 86 03 02 61` | `RemoteException / ModbusIllegalDataValue / 0x03` |

两者均先通过 CRC、Slave 和异常帧结构校验，再映射为远端异常；未误判为本地 CRC、协议或串口错误。

## 6. 500 次连续读取稳定性

使用生产后端连续执行 500 次 PDU 0～4 的 0x03 请求，不做隐式重试：

| 指标 | 实测结果 |
|---|---:|
| 请求数 | 500 |
| 成功数 | 500 |
| 失败数 | 0 |
| 超时数 | 0 |
| 最小 RTT | 3.348 ms |
| 平均 RTT | 5.674 ms |
| 最大 RTT | 6.941 ms |
| Firmware 通信错误计数 | 0 → 0 |

这组数据关闭当前 Windows ST-LINK VCP 环境下的连续请求稳定性门禁，不外推到真实 RS485 或其他 USB/串口驱动。

## 7. 无响应、关闭重开与恢复

为避免物理拔线影响阈值恢复，联调程序关闭正常连接后，以错误 Slave ID 247 重新打开同一 VCP并发送版本读取。Slave ID 1 对该请求保持静默，Host 在约 107.079 ms 后返回结构化 `ResponseTimeout`，TX 保留、RX 为空且 RTT 可用：

```text
TX F7 03 00 27 00 02 60 96
RX <empty>
error ResponseTimeout
```

随后显式关闭，按 Slave ID 1、115200 8N1 重新打开并再次读取版本：

```text
TX 01 03 00 27 00 02 74 00
RX 01 03 04 00 00 00 02 7B F2
```

回读为 0.2，结论为 `REOPEN_RECOVERED=YES`。自动重连未实现，符合 MVP 由上层显式关闭/重开的恢复策略。

## 8. 验收结论与限制

TASK-008 的 QSerialPort 生产后端、单通信线程、有界 FIFO、所有权交接、超时/取消/关闭、分片/重新同步、完整事务证据以及 VCP/UART 实板闭环均通过。Phase 3 达到“Qt 可以稳定读取 STM32 数据”的验收条件。

未实现、未验证项保持不变：实时监控 UI、TestEngine、报告系统、公共 Raw Frame/错误注入、TTL-RS485 收发器、DE/RE、终端、偏置和真实 RS485 电气层。`TASK-002` 继续延期。

## 9. 最终 Review

### 必须修复

无。

### 建议修复

无。开发过程中发现的 Facade queued 计数和 queued 取消 RTT 归属问题已在提交前修正，并由回归断言覆盖。

### 可选优化

- Phase 4 可在不绕过 `IModbusClient` 的前提下接入监控和 TestEngine；
- TASK-002 完成后追加真实 RS485 的方向控制、电气时序和长线稳定性验证；
- 若未来需要现场诊断，可单独评审 unsolicited data 的持久化诊断出口，不扩展本任务公共通信契约。
