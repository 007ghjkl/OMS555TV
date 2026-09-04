# TASK-009 Firmware USART1/RS485 传输迁移

## 目标

在 TASK-002 硬件接口 Spec 通过后，把已验证的 Firmware Modbus RTU Slave 从 USART2/ST-LINK VCP 迁移到 USART1/真实半双工 RS485，同时保持协议核心、设备模型和寄存器语义不变，并保留可回归的 VCP 构建路径。

## 背景

当前 `platform_stm32.c`、CubeMX 工程、中断和 HAL 回调都直接绑定 `huart2`/USART2。TASK-005 的纯 C 协议核心已经稳定，因此迁移应只改变平台传输适配、CubeMX 外设和必要的方向控制，不复制或重写 Modbus Slave。

用户已通过 USART1 持续向 COM6 发送消息，证明单向 TX 路径具备继续验证的基础；双向半双工 Modbus 和方向切换仍需本任务实现和验证。

## 范围

- 实现前创建并评审 `specs/firmware_rs485_transport_migration.md`。
- 在 CubeMX/HAL 工程中配置 USART1 PA9/PA10、115200 8N1、无流控和接收中断。
- 根据 TASK-002 已确认方案实现以下之一：
  - 外部 DE/RE：使用评审后的 GPIO，明确上电、空闲、发送、发送完成和错误状态；
  - 自动换向：不增加虚构 GPIO，只实现并验证 USART1 收发适配。
- 将 Firmware 平台层从固定 `huart2` 重构为明确的 Modbus UART 传输配置，不修改 HAL 无关的 CRC、RTU RX、寄存器映射和 Slave 逻辑。
- 把 Receive-to-IDLE、UART 错误、Abort/重新挂接和发送路径迁移到所选 USART1 句柄。
- 外部方向控制方案必须等待 UART TC/最后停止位完成后再释放发送方向。
- 保留 USART2/ST-LINK VCP 作为诊断或回归通道；不得在 RS485 Modbus 通道混入 ASCII 文本。
- 提供明确的 VCP/RS485 构建选择，单个固件镜像只启用一个 Modbus 传输端点。
- 复跑 TASK-005 的全部纯 C 测试和 ARM 干净构建。
- 烧录 RS485 构建，使用第三方 Modbus Master 通过 COM6 完成基础 0x03、0x06、0x01/0x02/0x03 和连续请求验证。
- 创建 `docs/test_results/task009_firmware_rs485_transport_validation.md`。

## 非范围

- 不修改 Host 生产后端、Monitor、TestEngine 或 UI。
- 不修改 Slave ID、波特率、寄存器地址、数据表示或阈值业务规则。
- 不增加 0x04、0x10、广播、自动重连、DMA 或 FreeRTOS。
- 不在 TASK-002 方向方案未确认时自行决定 PC8 或自动换向。
- 不把 USART1 原始文本发送结果替代为本任务的 Modbus 双向验收。

## 依赖

- TASK-002 的模块身份、接线和 `specs/rs485_hardware_interface.md` 已评审通过。
- TASK-005 Firmware Modbus RTU Slave 与 35 个纯 C 测试基线。
- 可用的 COM6 转换器；端口号仍需运行时枚举，不得写入固件或 Host 默认值。
- NUCLEO-F411RE 与安全低压 RS485 点对点链路。

## 实现门禁

1. 开始改 CubeMX 前确认 PA9/PA10 和方向 GPIO 与现有 I2C、ADC、SWD、板载连接器无冲突。
2. 开始方向控制前必须确认模块是外部 DE/RE 还是自动换向。
3. Spec 必须定义 GPIO 电平含义、上电默认状态、TX→RX 时序、发送失败、超时和 UART 错误恢复。
4. 迁移必须复用现有 `ModbusPlatform` 边界，不得复制 `modbus_slave` 或创建第二套协议栈。
5. 实机测试前保存 TASK-005 VCP 固件的可回退构建方式。
6. 若 CubeMX 重新生成导致无关大范围 Diff，必须先缩小并评审修改范围。

## 实现要求

1. USART1 参数保持 Slave ID 1、115200 8N1、无流控和默认 500 ms Host 超时口径。
2. RX 空闲时不得持续驱动总线；外部 DE/RE 方案上电后默认进入接收状态。
3. 发送前进入 TX，完整响应和最后停止位发送完成后回到 RX；不得仅依据 TXE 提前释放。
4. 自动换向方案必须通过最短异常响应和连续短请求验证 turnaround，不以“能看到文本”代替。
5. Receive-to-IDLE、分片、短帧、CRC 错误和 UART 错误计数保持 TASK-005 语义。
6. USART1/USART2 回调必须按句柄分流，诊断通道事件不得进入 Modbus RX 缓冲。
7. VCP 和 RS485 构建均能编译；RS485 构建是本任务实机验收对象。
8. 测试结束后四路阈值恢复为执行前保存值，并记录最终状态。

## 验收标准

1. Technical Spec 已评审通过，方向控制、电平和错误恢复没有未决实现选择。
2. CubeMX 引脚、生成代码、实物接线和硬件基线一致。
3. 35/35 既有 Firmware 纯 C 测试通过，ARM GCC 干净构建无新增警告。
4. VCP 回归构建和 USART1/RS485 构建均成功，协议核心没有重复实现。
5. 第三方 Master 经真实 RS485 可读取全部合法寄存器块。
6. 四路阈值可分别写入、回读并恢复，0x06 回显正确。
7. 非法功能、地址和值得到正确 0x01、0x02、0x03；错误 CRC/短帧无响应且后续可恢复。
8. 外部 DE/RE 方案没有响应截断或总线长期占用；自动换向方案没有短帧 turnaround 丢失。
9. 至少 100 次连续 0x03 请求无隐式重试，并记录成功、失败、超时、Firmware 错误计数和 RTT。
10. 三路传感器采集在 RS485 连续访问期间继续运行，设备状态和采样周期无明显异常。
11. 测试记录包含模块、转换器、COM 枚举、接线、线长、终端/偏置、TX/RX 和结果。
12. 最终 Review 无必须修复项；未修改 Host、监控、TestEngine 或报告功能。

## 测试要求

- 无硬件回归：全部 Firmware 纯 C 测试和两个传输配置的 ARM 构建。
- 方向状态测试：上电、空闲、TX、TC 完成、RX、错误和恢复。
- 第三方 Master：全部读块、四路阈值、异常响应、错误 CRC/短帧和连续 100 次读取。
- 并行采集：跨多个 2 秒采样周期执行 RS485 请求，核对设备状态与运行时间。
- 回退验证：保留并验证 USART2/VCP 构建方法，但不要求同时开放两个 Modbus Slave。

## 当前状态

已完成（2026-09-04）。

- `specs/rs485_hardware_interface.md` 与 `specs/firmware_rs485_transport_migration.md` 均已评审通过；
- USART1/PA9/PA10、自动换向 RS485 和互斥构建选择已实现，未使用 PC8/DE/RE；
- USART2 VCP 与 USART1 RS485 两种 ARM 构建均通过，非法构建值按预期拒绝；
- 既有纯 C 测试 35/35 通过，RS485 固件已烧录、校验并运行；
- 第三方 Master 完成全量读写、0x01/0x02/0x03、坏 CRC、短帧、500 次连续请求和采集并行验证；
- 测试后的四路阈值已恢复为 600/600/600/400。

详细证据见 `docs/test_results/task009_firmware_rs485_transport_validation.md`。最终 Review 无必须修复项，允许进入 TASK-010。
