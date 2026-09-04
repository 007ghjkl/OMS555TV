# TASK-002 RS485 硬件基线与迁移验收总任务

## 目标

在安全低压环境中确认实际 TTL-RS485 收发器、USB-RS485 转换器、接线、方向控制、终端和偏置方案，并通过分阶段任务把已验证的 Modbus RTU 协议从 USART2/ST-LINK VCP 迁移到 USART1/真实半双工 RS485，最终形成 Firmware、Host 与电气层均可追溯的验收闭环。

## 背景

TASK-005 已证明 STM32 Modbus RTU Slave 在 USART2/ST-LINK VCP 上工作；TASK-008 已证明 Host `QSerialPortModbusClient` 可通过 VCP 完成 0x03、0x06、异常响应、超时恢复和 500 次连续读取。两项结论都不包含 RS485 收发器、半双工方向切换、终端、偏置或真实 RS485 线路。

2026-09-04 已获得新的前置证据：

- 用户已使用 USART1 持续发送消息到 COM6，并确认接收端能够持续收到；
- Windows 当前将 COM6 枚举为 `MacroSilicon USB Serial Ports`，实例 ID 为 `USB\VID_345F&PID_3020\A02001JS`；
- 该证据证明当前接线至少具备“STM32 USART1 TX → 外部链路 → COM6”单向可达性，并支持继续迁移；
- 该证据尚不能单独证明 USART1 RX、双向半双工换向、DE/RE 时序、A/B 定义、终端/偏置或 Modbus RTU 请求响应已经通过。

## 任务定位

TASK-002 是 RS485 迁移的总验收门禁，不在一个提交中混合完成全部 Firmware 与 Host 工作。执行拆分为：

1. TASK-002：确认硬件身份、电气兼容性、接线和方向方案，输出硬件基线与电气接口 Spec；
2. TASK-009：实现 Firmware USART1/RS485 传输迁移，并用第三方 Master 完成基础协议验证；
3. TASK-010：使用 TASK-008 Host 生产后端完成真实 RS485 全量闭环、恢复和稳定性验证，关闭总门禁。

TASK-002 只有在 TASK-009 和 TASK-010 均通过、文档完成同步后才标记为全部完成。

## 范围

- 记录 TTL-RS485 收发器模块的完整型号、芯片/模块丝印、供电电压、逻辑电平和数据手册来源。
- 记录 USB-RS485 转换器的产品/芯片身份；区分 Windows USB 串口身份与 RS485 收发器身份。
- 确认 STM32 侧 USART1 PA9/PA10 与实际模块 DI/RO 的接线。
- 确认方向控制属于以下哪一种，并记录依据：
  - 外部 DE 与 `/RE`：评审是否合并控制，候选 PC8 只有在引脚冲突检查通过后才能固化；
  - 模块自动换向：不虚构 DE/RE GPIO，必须核对自动换向条件和收发 turnaround 限制。
- 记录 A/B 端子定义、两端连接、公共参考地或隔离方案。
- 记录线缆类型、线长、拓扑、终端电阻、偏置电阻及模块板载跳线/电阻状态。
- 在断电和安全低压条件下核对终端/偏置配置，避免重复终端、多个偏置源或逻辑电平超限。
- 对现有 USART1→COM6 连续发送验证补齐日期、串口参数、测试字节/文本、持续时间和原始记录。
- 增加反向原始字节验证，证明 COM6→USART1 RX 可达。
- 对需要外部方向控制的模块，验证 TX、RX、空闲三种状态；对自动换向模块，验证连续请求下的 turnaround。
- 输出并评审 `specs/rs485_hardware_interface.md`，作为 TASK-009/010 的实施基线。
- 更新 `docs/hardware_baseline.md`，不得继续保留“当前无实物”的过期结论。

## 非范围

- TASK-002 硬件确认阶段不修改 Modbus 业务、寄存器表、Host 公共通信契约或 Firmware 设备模型。
- 不在模块型号、逻辑电平和方向方式未确认时写死 PC8、DE/RE 时序或电气参数。
- 不把单向持续文本输出写成 Modbus RTU、双向 RS485 或系统稳定性通过。
- 不要求示波器或逻辑分析仪作为最低完成条件；若使用，必须记录探测点和口径。
- 不接入高压、市电、真实电力线路或非安全低压系统。

## 依赖

- 实际 TTL-RS485 收发器模块及可核对的型号/资料。
- 当前 COM6 对应的 USB 设备或 USB-RS485 转换器。
- NUCLEO-F411RE、USART1 PA9/PA10 和候选 PC8 可访问。
- TASK-005 Firmware Modbus RTU Slave 已完成。
- TASK-008 Host QSerialPort 后端与 VCP 联调已完成。

## 方案门禁

### 方案 A：外部 DE/RE 方向控制

- 适用条件：模块暴露 DE、`/RE`，且没有可验证的自动换向电路。
- 计划：评审 DE 与 `/RE` 合并控制，候选使用 PC8；Firmware 在发送前进入 TX，确认最后停止位发送完成后回到 RX。
- 风险：过早释放会截断 CRC/停止位，过晚释放会错过 Master 后续数据或占用总线。

### 方案 B：模块自动换向

- 适用条件：模块资料或电路可证明自动控制方向，不要求 MCU 驱动 DE/RE。
- 计划：USART1 仅连接 TX/RX，重点验证最短请求/响应间隔和连续帧 turnaround。
- 风险：自动换向延时可能不透明，不同波特率或短帧下行为可能不同。

必须依据实际模块确定 A 或 B。未获得模块型号、引脚照片/资料或可验证电路前，不得选择方案并开始 TASK-009。

## 实施顺序

1. 建档：记录两个模块的正反面丝印、端子标签、供电和跳线状态，并把资料放入 `docs/hardware_info/RS485/`。
2. 电气核对：确认 3.3 V 逻辑兼容、供电、公共地/隔离、A/B、终端和偏置。
3. 原始链路复核：补齐现有 USART1→COM6 证据，并验证 COM6→USART1 RX。
4. 方向方案评审：确定外部 DE/RE 或自动换向，输出 `specs/rs485_hardware_interface.md`。
5. 执行 TASK-009：Firmware USART1/RS485 迁移与第三方 Master 验证。
6. 执行 TASK-010：Host 真实 RS485 全量联调、恢复与稳定性验证。
7. 汇总证据：同步硬件基线、架构、测试计划、README 和本任务状态。

## 验收标准

1. TTL-RS485 模块和 USB-RS485 转换器身份、供电、逻辑兼容性与资料来源均已记录；不能只记录 COM 号。
2. PA9、PA10、可选 PC8、DI、RO、DE、`/RE`、A、B、VCC、GND 的最终接线表与实物一致。
3. 已确定外部 DE/RE 或自动换向方案，并有资料/电路/实测依据。
4. 线长、拓扑、终端、偏置、公共地或隔离方案已记录，不存在无说明的电阻和跳线状态。
5. USART1→COM6 和 COM6→USART1 两个方向的原始字节链路均有时间、参数、负载和结果证据。
6. `specs/rs485_hardware_interface.md` 已评审通过，未决项不会导致 TASK-009 选择不同 GPIO 或方向算法。
7. TASK-009 通过：Firmware 在 USART1/RS485 上可被第三方 Master 稳定执行 0x03、0x06 和异常请求。
8. TASK-010 通过：Host 生产后端在真实 RS485 上完成全量读写、证据、恢复和连续请求验证。
9. 所有报告明确区分 VCP/UART 历史结果与 RS485 新结果，未执行项目不标记通过。
10. 最终 Review 无必须修复项，TASK-002 才可标记完成。

## 当前状态

已完成，RS485 迁移总门禁已关闭（2026-09-04）。

- 硬件接口已唯一确定：MAX13487EESA 系列自动换向 TTL-RS485、5 V、PA9→RXD、PA10←TXD，不使用 PC8；
- 总线为 A↔T/R+、B↔T/R-、GND↔GND，约 20 cm 点对点三线，R16 未短接；
- DTECH USB-RS485 当前枚举为 COM6、MacroSilicon `345F:3020`；COM 号仅为运行时观察值；
- `specs/rs485_hardware_interface.md` 已评审通过；
- TASK-009 已通过 Firmware 双构建、35/35 回归、第三方 Master 和 500 次连续请求；
- TASK-010 已通过 Host 9/9 CTest、全量读写、500 次连续请求、物理断线恢复和设备复位恢复；
- 文档已同步，最终 Review 无必须修复项。

证据见：

- `docs/test_results/task009_firmware_rs485_transport_validation.md`；
- `docs/test_results/task010_host_rs485_system_integration.md`。
