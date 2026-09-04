# Firmware USART1/RS485 传输迁移技术规范

> 状态：已评审、实现并验证
>
> 版本：1.0
>
> 日期：2026-09-04
>
> 对应任务：`TASK-009 Firmware USART1/RS485 传输迁移`
>
> 前置规范：`specs/rs485_hardware_interface.md` 1.0

## 1. 目标

把 TASK-005 已验证的 Modbus RTU Slave 传输端点从 USART2/ST-LINK VCP 迁移到 USART1/自动换向 RS485，同时保留 USART2 VCP 回归构建。迁移只改变 HAL 平台适配和 CubeMX 外设配置，不复制或修改 CRC、RTU 分帧、寄存器映射、设备模型和 Modbus 业务语义。

## 2. 固定约束

| 项目 | 值 |
|---|---|
| MCU | STM32F411RET6 / NUCLEO-F411RE |
| RS485 UART | USART1，PA9 TX、PA10 RX，AF7 |
| 回归 UART | USART2，PA2 TX、PA3 RX，ST-LINK VCP |
| RS485 模块 | MAX13487EESA 系列自动换向模块，5 V 供电 |
| 方向 GPIO | 无；PC8 不使用 |
| 串口 | 115200 8N1，无流控 |
| Slave ID | 1 |
| RTU 最大 ADU | 256 bytes |
| 帧结束 | 最后字节后 3 个 SysTick 计数，沿用 TASK-005 |
| 发送超时 | 10 ms，沿用 TASK-005 |
| 动态内存 | 禁止 |

模块 R16 未短接，当前三线约 20 cm，A↔T/R+、B↔T/R-、GND↔GND。电气事实只记录在硬件规范和测试报告中，不写死到 Firmware。

## 3. 构建选择

### 3.1 CMake 接口

固件顶层 CMake 增加字符串缓存变量：

```text
OMS555TV_MODBUS_TRANSPORT=USART2_VCP | USART1_RS485
```

- 默认值为 `USART2_VCP`，保持历史构建和 IDE 打开行为；
- 非法值在配置阶段 `FATAL_ERROR`；
- `USART2_VCP` 定义 `OMS555TV_MODBUS_USART2_VCP=1`；
- `USART1_RS485` 定义 `OMS555TV_MODBUS_USART1_RS485=1`；
- C 源码必须在两个定义同时存在或均不存在时编译失败；
- 单个镜像只把一个 UART 注册为 Modbus 端点。

### 3.2 外设初始化

CubeMX 工程同时配置 USART1 和 USART2，二者均为115200 8N1、TX/RX、无流控，并生成对应句柄、中断、MSP GPIO 和 NVIC。两个 UART 可以在同一镜像中初始化，但只有编译选择的句柄进入 `ModbusPlatform`、接收回调和发送路径。

USART2 不输出 ASCII；RS485 构建中它只作为保留的诊断/回归硬件资源，不接收或响应 Modbus。VCP 构建中 USART1 不接收或响应 Modbus。

## 4. 平台绑定

`platform_stm32.c` 通过编译期选择提供三个内部绑定：

```c
static UART_HandleTypeDef *modbus_uart(void);
static IRQn_Type modbus_uart_irq(void);
static bool is_modbus_uart(const UART_HandleTypeDef *huart);
```

允许用等价宏或静态函数实现，但禁止复制两套 `stm32_modbus_start/poll/transmit`。所有以下路径必须使用同一个选中句柄：

- `HAL_UARTEx_ReceiveToIdle_IT()`；
- `HAL_UART_AbortReceive()`；
- IRQ 临界区的 disable/enable；
- `HAL_UART_Transmit()`；
- `HAL_UARTEx_RxEventCallback()` 句柄过滤；
- `HAL_UART_ErrorCallback()` 句柄过滤。

未选中 UART 的中断和回调不得写入 Modbus RX 状态、错误计数或发送响应。

## 5. USART1 与中断配置

### 5.1 CubeMX/生成代码

- 增加 `UART_HandleTypeDef huart1`；
- 增加 `MX_USART1_UART_Init()`，在 `app_init()` 前调用；
- PA9/PA10 配置为 AF7、推挽、无上下拉、低速或 CubeMX UART 默认速度；
- 使能 USART1 时钟；
- 使能 `USART1_IRQn`，优先级与 USART2 当前配置一致；
- 增加 `USART1_IRQHandler()` 并调用 `HAL_UART_IRQHandler(&huart1)`；
- `.ioc` 中的 IP、Pin、NVIC 和初始化顺序必须与生成代码一致。

PA9/PA10 与当前 I2C1/2/3、ADC1、SWD 无冲突。PC8 不配置为方向输出。

### 5.2 接收状态

启动时清空接收器、错误和重挂标志，然后在选中 UART 上调用 `HAL_UARTEx_ReceiveToIdle_IT()`。ISR 回调仍只执行：

1. 判断句柄是否为选中 Modbus UART；
2. 对本次 chunk 的每个字节调用纯 C `modbus_rtu_rx_push_byte()`；
3. 使用一次 `HAL_GetTick()` 作为该 chunk 时间；
4. 重新挂接 Receive-to-IDLE；失败时设置 `rearm_required`。

协议解析、寄存器访问和响应发送仍在主循环。

## 6. 自动换向与发送

MAX13487E 模块内部根据 DI 自动控制驱动器，无 DE/RE GPIO。Firmware 发送算法为：

1. 验证 data、length 和上限；
2. 在选中 UART 调用阻塞式 `HAL_UART_Transmit()`；
3. 只有 HAL 返回 `HAL_OK` 才报告成功；
4. 不添加固定前导/尾随延时，不操作 PC8，不调用 RTS/DTR；
5. 不在平台层隐式重试。

仓库使用的 STM32 HAL `HAL_UART_Transmit()` 在发送全部数据后等待 `UART_FLAG_TC`，因此返回成功时最后停止位已经离开发送器，满足自动换向释放前提。若实机出现响应截断、回显污染或总线长期占用，必须登记缺陷并定位模块 turnaround；不得用延长 Host 超时或隐式重试替代根因修复。

接收在发送期间保持挂接。MAX13487E AutoDirection 在发送时管理驱动/接收状态；若实测产生本地回显，则把“发送前暂停 RX、TC 后重新挂接”作为缺陷修复候选，必须增加回归测试后才能采用，不在无证据时预先增加竞态窗口。

## 7. 错误和恢复

沿用 TASK-005 语义：

- UART overrun/framing/noise/parity 回调：丢弃候选帧、上报一次 UART 错误事件、主循环饱和计数一次并重新挂接；
- Receive-to-IDLE 重挂失败：设置 `rearm_required`，下一次主循环在选中 UART IRQ 临界区内 abort 并重挂；
- CRC 错误、短帧、溢出、错误地址、异常响应的计数口径不变；
- 发送失败丢弃本次响应，不自动重发，接收路径继续可用；
- 错误后下一合法帧必须成功。

IRQ 临界区只能屏蔽选中的 UART IRQ，不能同时关闭 USART1/USART2 或影响 I2C/ADC。

## 8. 生产发送所有权

- `PHASE1_DEBUG_LOG=0` 继续固定；
- 选中的 Modbus UART 唯一发送者是 Modbus 响应路径；
- 不允许 boot、采集或 `printf` 文本混入 USART1 或 USART2 的 Modbus 流；
- RS485 构建不允许 USART2 VCP 同时启动第二个 Slave；
- VCP 构建不允许 USART1 对总线请求响应。

## 9. 纯 C 与静态测试

现有35个纯 C测试保持35/35。新增的构建选择和 HAL绑定不引入 HAL 到纯 C目标。静态复核包括：

- 只存在一套 `modbus_slave`、`register_map` 和 `modbus_rtu_rx`；
- `platform_stm32.c` 不再把所有路径写死为 `huart2/USART2_IRQn`；
- 两个编译定义互斥且非法构建值配置失败；
- `.ioc`、`main.c`、MSP、IT 源/头对 USART1/USART2 一致；
- PC8 没有 RS485 方向配置；
- 选中 UART 没有 ASCII 发送路径。

## 10. ARM 构建矩阵

使用互相独立的构建目录：

| 配置 | CMake 值 | 要求 |
|---|---|---|
| VCP 回归 | `USART2_VCP` | 干净配置、编译、链接，无新增警告 |
| RS485 目标 | `USART1_RS485` | 干净配置、编译、链接，无新增警告；作为烧录对象 |

记录两个 ELF 的 RAM/Flash 使用。构建产物不进入 Git。

## 11. TASK-009 实机验证

### 11.1 环境记录

- NUCLEO-F411RE 实物标识；
- TTL 模块丝印、5 V、PA9/PA10、R16开路；
- USB 转换器当前 COM、VID/PID、实例 ID；
- A/B/GND、约20 cm、点对点拓扑；
- Firmware 构建选择和版本；
- Master 名称/版本、115200 8N1、Slave1、500 ms、0重试。

### 11.2 第三方 Master

1. 运行时枚举并打开 USB-RS485 端口；
2. 读取全部合法块0～4、9～12、19～20、29～31、39～40；
3. 保存阈值，分别写入/回读四路和边界值，最终恢复；
4. 验证0x02、0x03；Raw接口验证0x01、错误CRC、短帧与恢复；
5. 连续至少100次0x03，无隐式重试，记录成功/失败/超时、RTT和通信错误计数；
6. 连续访问跨至少三个2秒采集周期，状态与数据继续更新；
7. 监听无请求窗口，确认没有ASCII或异常 unsolicited bytes；
8. 专门覆盖5字节异常响应、8字节0x06和连续短请求，验证自动换向 turnaround。

记录写入 `docs/test_results/task009_firmware_rs485_transport_validation.md`。未实测项目不得标记通过。

## 12. 回退

- VCP 回退不依赖代码撤销，只需以 `OMS555TV_MODBUS_TRANSPORT=USART2_VCP` 重新构建并烧录；
- 两种构建共享协议和应用代码；
- RS485 测试失败时先保留失败日志，再烧录 VCP 构建确认协议/采集基线，不能修改寄存器语义规避问题。

## 13. 评审记录

### 13.1 必须修复

评审识别并在本版关闭：

1. **不得把自动换向模块按外部 DE/RE 实现。** 已明确不使用 PC8，发送依赖 MAX13487E AutoDirection 和 HAL TC。
2. **构建选择可能形成两个同时响应的 Slave。** 已规定单一编译期端点，回调按句柄过滤。
3. **仅替换发送句柄会遗漏接收、错误和 IRQ 临界区。** 已列出所有必须统一绑定的 HAL 路径。
4. **TXE 返回可能截断末尾。** 已核对当前 HAL 阻塞发送成功前等待 `UART_FLAG_TC`。
5. **预先暂停接收可能引入重挂窗口。** 当前保持接收挂接，以实测判断是否存在本地回显；无证据不增加该状态切换。
6. **VCP 回退可能依赖手工改代码。** 已定义 CMake 构建选择和独立构建矩阵。

### 13.2 建议与可选项

- 若未来需要同一镜像运行时切换端点，应单独设计持久配置、互斥和安全切换；本任务不实现。
- DMA、非阻塞TX和示波器时序不是当前最低门禁；出现吞吐或时序缺陷后再评审。

### 13.3 结论

本规范符合 TASK-002 硬件接口、TASK-005 协议语义和 TASK-009 范围，方向、电平、构建选择、句柄绑定、TC、错误恢复、回退和实测均无未决实现选择。

评审结论：**通过。实现与本规范一致，TASK-009 的构建、回归和实机验收均已完成。**
