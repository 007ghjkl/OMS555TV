# Firmware Phase 2 Modbus RTU Slave 技术规范

> 对应任务：`tasks/TASK-005-firmware-phase2-modbus-rtu-slave.md`
>
> 状态：评审完成，通过（允许进入实现）
>
> 版本：1.0
>
> 日期：2026-09-03
>
> 实物验证状态：VCP/UART 协议验证已通过；RS485 电气层未验证且不在本任务范围

## 1. 目的

本规范定义 STM32F411 Phase 2 的 Modbus RTU Slave、Holding Register 映射、USART2/ST-LINK VCP 适配、接收帧边界、错误计数、并发一致性和验证方法。实现完成后，第三方 Modbus Master 应能通过 115200 8N1 的 VCP/UART 链路，以 Slave ID 1 使用 0x03 和 0x06 访问当前设备模型。

### 1.1 输入

- USART2 收到的 Modbus RTU 字节和 HAL UART 错误事件；
- 1 ms 单调模 2^32 时基；
- Phase 1 的 `DeviceModel` 当前快照；
- 0x03、0x06 请求 ADU；
- `docs/modbus_register_map.md` 中已定义的 Holding Register 地址和语义。

### 1.2 输出

- 0x03 正常响应；
- 0x06 成功回显；
- 0x01、0x02、0x03 标准异常响应；
- CRC、帧长度、缓冲区溢出和 UART 错误对应的通信错误饱和计数；
- 不依赖 STM32 HAL 的 CRC、帧接收、协议和寄存器映射纯 C 测试；
- VCP/UART 第三方 Master 验证记录及原始请求、响应证据。

### 1.3 非范围

- 不实施 TTL-RS485、DE/RE、终端电阻、偏置或 RS485 电气测试；
- 不实现 0x04、0x10、广播处理、在线修改 Slave ID 或通信参数；
- 不修改 Host 生产代码；
- 不实现阈值持久化、FreeRTOS、DMA 或动态内存；
- 不把 VCP/UART 结果描述为 RS485 验证通过。

## 2. 依据与优先级

实现按以下优先级服从项目资料：

1. `tasks/TASK-005-firmware-phase2-modbus-rtu-slave.md`；
2. 本规范；
3. `docs/architecture.md`；
4. `docs/prd.md`；
5. `PROJECT_SPEC.md`；
6. `docs/modbus_register_map.md`；
7. `tasks/TASK-003-firmware-phase1-basic-acquisition.md`；
8. `specs/firmware_phase1_acquisition.md`；
9. `docs/hardware_baseline.md`；
10. [Modbus Application Protocol Specification V1.1b3](https://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)；
11. [Modbus Serial Line Protocol and Implementation Guide V1.02](https://www.modbus.org/docs/Modbus_over_serial_line_V1_02.pdf)。

若寄存器语义与更高优先级资料冲突，必须先停止实现并评审文档；协议通用规则与项目明确裁剪冲突时，以本任务的功能范围为准，但不得生成违反 Modbus 线路格式的响应。

## 3. 已确认协议约束

### 3.1 串口参数

| 参数 | 固定值 |
|---|---:|
| 当前物理通道 | ST-LINK VCP + USART2 TTL |
| Slave ID | 1 |
| Baud rate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| 最大 RTU ADU | 256 bytes |
| 最大 PDU | 253 bytes |
| 高波特率帧间静默判定 | 3 个 SysTick 计数（真实下界大于 2 ms） |
| Host 初始响应超时 | 500 ms |

官方串行规范对波特率大于 19200 的链路推荐固定 `t1.5 = 750 us`、`t3.5 = 1.750 ms`。当前系统时基为 1 ms，字节回调时间戳存在最多接近 1 ms 的向下量化误差，因此接收端要求 `now_ms - last_byte_ms >= 3`。其真实静默下界大于 2 ms，保证不小于推荐的 1.750 ms。

本任务不增加微秒定时器来强制检测 750 us 的帧内间隔。发送方仍必须连续发送请求；接收端以 3 个 SysTick 计数的帧边界、请求定长、CRC 和功能码字段共同拒绝损坏或不完整请求。这一取舍不改变有效请求和响应的线路格式。

### 3.2 ADU 布局和字节序

```text
请求/响应 ADU = Address(1) + Function/PDU(1..253) + CRC Low(1) + CRC High(1)
```

- 地址、数量、寄存器值在线路上均为高字节先传输；
- CRC16 数值的低字节先发送，高字节后发送；
- 40001 对应 PDU 地址 0；不得在实现中引入 40001 偏移；
- 一个 RTU ADU 不得超过 256 字节。

### 3.3 支持矩阵

| 功能 | 行为 |
|---|---|
| 0x03 Read Holding Registers | 支持，数量 1～125；实际范围中的每个地址都必须已定义 |
| 0x06 Write Single Register | 支持，仅四路阈值地址可写 |
| 其他功能码 | 返回 0x01 Illegal Function |
| 广播地址 0 | 本任务不支持，按地址不匹配静默丢弃 |
| 非本机地址 | 静默丢弃，不修改设备模型或通信错误计数 |

## 4. 模块边界

### 4.1 目录与职责

```text
firmware/stm32/App/
├── modbus/
│   ├── modbus_crc.c/.h
│   ├── modbus_rtu_rx.c/.h
│   ├── register_map.c/.h
│   └── modbus_slave.c/.h
├── platform/
│   ├── platform.h
│   └── platform_stm32.c/.h
├── device_model/
└── app/
```

| 模块 | 职责 | HAL 依赖 |
|---|---|---|
| `modbus_crc` | Modbus CRC16 计算与校验 | 禁止 |
| `modbus_rtu_rx` | 固定缓冲区、逐字节接收、3 tick 帧结束、溢出状态 | 禁止 |
| `register_map` | `DeviceModel` 与 PDU 地址之间的唯一映射和阈值写入 | 禁止 |
| `modbus_slave` | ADU 校验、功能码处理、异常响应和响应组帧 | 禁止 |
| `platform` | USART2 中断、临界区、UART 错误采集和有界发送 | 允许 |
| `app` | 服务顺序、通信错误唯一入账点及模块组装 | 仅通过 `platform` |

地址常量只能由 `register_map` 定义；`modbus_slave` 不得直接访问 `DeviceModel` 字段或散落寄存器地址。

### 4.2 依赖方向

```text
main -> app -> modbus_slave -> register_map -> device_model
            -> platform -> USART2 HAL
platform -> modbus_rtu_rx
modbus_slave -> modbus_crc
```

`modbus_crc`、`modbus_rtu_rx`、`register_map` 和 `modbus_slave` 必须能与现有 Phase 1 纯 C 代码一起由本机编译器构建。

## 5. 规范级接口和常量

实现名称可按现有风格小幅调整，但不得改变语义。

```c
#define MODBUS_SLAVE_ID 1u
#define MODBUS_RTU_MAX_ADU 256u
#define MODBUS_RTU_FRAME_GAP_MS 3u
#define MODBUS_READ_MAX_QUANTITY 125u

typedef enum {
    MODBUS_RX_NONE = 0,
    MODBUS_RX_FRAME_READY,
    MODBUS_RX_OVERFLOW
} ModbusRxResult;

typedef struct {
    uint8_t bytes[MODBUS_RTU_MAX_ADU];
    uint16_t length;
    uint32_t last_byte_ms;
    bool receiving;
    bool overflow;
} ModbusRtuReceiver;

typedef enum {
    MODBUS_PROCESS_NO_RESPONSE = 0,
    MODBUS_PROCESS_RESPONSE_READY,
    MODBUS_PROCESS_COMMUNICATION_ERROR
} ModbusProcessResult;

typedef struct {
    ModbusProcessResult result;
    uint16_t response_length;
} ModbusProcessOutcome;

uint16_t modbus_crc16(const uint8_t *data, size_t length);
void modbus_rtu_rx_init(ModbusRtuReceiver *receiver);
void modbus_rtu_rx_push_byte(
    ModbusRtuReceiver *receiver, uint8_t byte, uint32_t now_ms);
ModbusRxResult modbus_rtu_rx_poll(
    ModbusRtuReceiver *receiver,
    uint32_t now_ms,
    uint8_t *frame,
    size_t capacity,
    size_t *frame_length);

bool register_map_read_holding(
    const DeviceModel *model,
    uint16_t start_address,
    uint16_t quantity,
    uint16_t *values);

typedef enum {
    REGISTER_WRITE_OK = 0,
    REGISTER_WRITE_ILLEGAL_ADDRESS,
    REGISTER_WRITE_ILLEGAL_VALUE
} RegisterWriteResult;

RegisterWriteResult register_map_write_single(
    DeviceModel *model, uint16_t address, uint16_t raw_value);

ModbusProcessOutcome modbus_slave_process_adu(
    DeviceModel *model,
    const uint8_t *request,
    size_t request_length,
    uint8_t *response,
    size_t response_capacity);
```

所有指针和容量必须校验。非法本机调用不得越界、不得修改输出对象，也不得被伪装成收到的 UART 通信错误。

## 6. USART2 接收和帧边界

### 6.1 接收方案

USART2 使用 `HAL_UARTEx_ReceiveToIdle_IT()` 把连续字节接收到固定 256 字节 chunk：

1. `platform` 初始化时清空 `ModbusRtuReceiver` 并启动首个 Receive-to-IDLE 接收；
2. `HAL_UARTEx_RxEventCallback()` 只处理 `huart2`，用同一个 `HAL_GetTick()` 时间戳把本次 `Size` 个字节逐个送入 `modbus_rtu_rx_push_byte()`；
3. 回调立即重新挂起下一个 Receive-to-IDLE 接收；
4. 主循环轮询时，在 USART2 临界区内调用 `modbus_rtu_rx_poll()`；
5. 从最后一个字节起达到 3 个 SysTick 计数后，接收器把当前 ADU 一次性转交主循环并清空自身；
6. ISR 不解析 Modbus、不访问 `DeviceModel`、不发送响应。

USART2 全局中断必须在 `.ioc`、NVIC 和 `USART2_IRQHandler()` 中一致启用，IRQ handler 只调用 `HAL_UART_IRQHandler(&huart2)`。

### 6.2 接收状态

```text
EMPTY
  | 首字节
  v
RECEIVING --后续字节且未满--> RECEIVING
  |                              |
  | 3 tick 静默                  | 第 257 个字节
  v                              v
FRAME_READY                  OVERFLOW
                                  |
                                  | 3 tick 静默
                                  v
                           DISCARD_AND_REPORT
```

- 长度 1～256 的数据均先作为候选 ADU；完整性由协议层判断；
- 第 257 个及后续字节不写入数组，只保持 `overflow=true` 并更新时间；
- 一个溢出字节流在静默结束时只报告一次通信错误；
- `uint32_t` 时间比较使用无符号差值，必须通过毫秒回绕测试；
- 主循环复制/清空接收器时必须短暂禁止 USART2 IRQ，禁止全局长时间关中断；
- 响应发送期间仍保留 RX 中断。符合请求/响应顺序的 Master 不应在响应前发送下一请求。

### 6.3 UART 错误恢复

`HAL_UART_ErrorCallback()` 只处理 `huart2`：

- overrun、framing、noise、parity 的一次 HAL 错误回调视为一个 UART 错误事件；同一回调中的多个错误位不重复计数；
- 回调把待处理 UART 错误事件数作饱和累积，不直接修改 `DeviceModel`；
- 主循环消费待处理事件并在唯一入账点增加通信错误计数；
- 必要时在主循环中受控终止并重新挂起 Receive-to-IDLE 接收；
- 错误发生时当前候选帧作废，避免把错误前后的字节拼成合法 ADU；
- 接收重启失败不得死循环，下一次 `app_service()` 继续尝试。

## 7. ADU 处理顺序

收到候选帧后必须按以下顺序处理：

1. `request_length == 0`：内部无事件，不响应；
2. 第一个字节不是 Slave ID 1：静默丢弃，不校验 CRC，不修改任何设备状态；
3. 长度小于 4：判为不完整帧，不响应，通信错误计数增加一次；
4. CRC 不正确：不响应，通信错误计数增加一次；
5. 功能码不支持：返回 0x01；
6. 0x03 或 0x06 长度小于 8：判为不完整帧，不响应，通信错误计数增加一次；
7. 0x03 或 0x06 长度大于 8：返回 0x03 Illegal Data Value；
8. 长度恰为 8：按对应功能码处理；
9. 构造响应后重新计算 CRC，不复制未经验证的请求 CRC。

该顺序确保地址不匹配的流量不改变设备状态，同时保证本机地址的 CRC 错误和不完整帧各只计数一次。合法异常响应不计通信错误。

## 8. CRC16

### 8.1 算法

```text
crc = 0xFFFF
for each byte:
    crc ^= byte
    repeat 8 times:
        if crc bit0 is 1: crc = (crc >> 1) XOR 0xA001
        else:             crc = crc >> 1
```

返回值使用主机整数表示。写入 RTU ADU 时先写 `crc & 0xFF`，再写 `crc >> 8`。

### 8.2 规范向量

| 无 CRC 数据 | CRC 数值 | 线路 CRC 字节 |
|---|---:|---|
| 空输入 | `0xFFFF` | 不组成有效 ADU |
| `01 03 00 00 00 0A` | `0xCDC5` | `C5 CD` |

纯 C 测试必须先通过该标准请求向量，再接入 UART。

## 9. 0x03 Read Holding Registers

请求必须恰为 8 字节：

```text
01 03 StartHi StartLo QuantityHi QuantityLo CrcLo CrcHi
```

处理规则：

1. 数量为 0 或大于 125：返回 0x03；
2. 使用至少 32 位中间值计算 `start + quantity`，发生 16 位地址溢出：返回 0x02；
3. 先验证请求范围内每个地址均已定义；任一保留或越界地址：返回 0x02；
4. 验证全部成功后生成一次 `RegisterSnapshot`；
5. 从同一快照输出所有寄存器，每个值高字节先发送；
6. 响应长度为 `5 + 2 * quantity`，组帧前验证响应容量。

正常响应：

```text
01 03 ByteCount DataHi DataLo ... CrcLo CrcHi
```

当前寄存器表存在空洞，因此合法连续读块为各个已定义连续区间的子区间：0～4、9～12、19～20、29～31、39～40。跨越保留地址的读取必须整体失败，不返回部分数据。

## 10. 0x06 Write Single Register

请求必须恰为 8 字节：

```text
01 06 AddressHi AddressLo ValueHi ValueLo CrcLo CrcHi
```

仅以下 PDU 地址可写：

| PDU 地址 | 字段 | 合法值 |
|---:|---|---:|
| 9 | A 相阈值 | `int16_t` -400～800 |
| 10 | B 相阈值 | `int16_t` -400～800 |
| 11 | C 相阈值 | `int16_t` -400～800 |
| 12 | 环境阈值 | `int16_t` -400～800 |

- 线路 `uint16_t` 位模式转换为 `int16_t` 后执行范围校验；
- 只读、保留或越界地址返回 0x02；
- 阈值超范围返回 0x03；
- 失败路径不得修改阈值或告警状态；
- 成功路径必须调用 `device_model_set_threshold()`，使当前有效温度立即重新计算告警；
- 成功响应回显地址、功能码、地址和值，并重新生成 CRC。

## 11. 寄存器映射

### 11.1 读取映射

| PDU 地址 | 来源 | 线路解释 |
|---:|---|---|
| 0～3 | `temperatures[A..AMBIENT].value_deci_c` | `int16_t` 二进制补码位模式 |
| 4 | `light.value_mv` | `uint16_t` |
| 9～12 | `thresholds_deci_c[A..AMBIENT]` | `int16_t` 二进制补码位模式 |
| 19 | `alarm_bits` | 保留位必须为 0 |
| 20 | `status_bits` | 保留位必须为 0 |
| 29 | `communication_error_count` | `uint16_t` 饱和值 |
| 30 | `uptime_seconds` 低 16 位 | 同一快照 |
| 31 | `uptime_seconds` 高 16 位 | 同一快照 |
| 39 | `firmware_version_major` | `uint16_t` |
| 40 | `firmware_version_minor` | `uint16_t` |

Phase 2 固件版本固定为 major `0`、minor `2`；不得从构建时间动态推断。

温度或光敏当前无效时，设备模型仍保留最后一次有效值；寄存器读取返回该保留值，有效性和故障由状态字表达。启动后尚无有效真实温度时返回初始化值 0，并由状态/后续采集反映状态，不增加未定义的有效位寄存器。

### 11.2 一致性快照

`RegisterSnapshot` 在主循环上下文一次复制以下字段：四路温度、光敏电压、四路阈值、告警字、状态字、通信错误计数、32 位运行时间和固件版本。范围验证完成后，整个 0x03 响应只读取该快照。

因此：

- 40031/40032 在同一请求中来自同一个 `uptime_seconds`；
- 复合读取不会混入采集状态机在响应中途产生的新值；
- 两次独立请求之间允许数据自然更新；
- ISR 永远不写 `DeviceModel`，无需给模型增加锁。

## 12. 通信错误计数

### 12.1 唯一入账点

只有 `app` 的 Modbus 服务函数可以调用：

```c
void device_model_add_communication_errors(
    DeviceModel *model, uint16_t count);
```

该函数按 65535 饱和相加。CRC、帧接收器和 HAL 回调都只返回事件，不直接修改计数。

### 12.2 计数口径

| 事件 | 增量 | 响应 |
|---|---:|---|
| 本机地址 CRC 错误 | 1/帧 | 无 |
| 本机地址不完整帧 | 1/帧 | 无 |
| 超过 256 字节的接收流 | 1/静默结束的字节流 | 无 |
| UART overrun/framing/noise/parity 回调 | 1/回调 | 无 |
| 非本机地址或广播 | 0 | 无 |
| 0x01/0x02/0x03 合法异常响应 | 0 | 有 |
| 阈值写入成功或失败 | 0 | 正常/异常响应 |
| 响应发送 HAL 失败 | 0 | 无；保留平台错误供调试 |

UART 错误与其导致的候选帧作废属于同一个根事件：若 UART 回调已上报并清空候选帧，不得再把同一批字节记作“不完整帧”。

## 13. ASCII 调试输出与发送所有权

Phase 2 生产构建固定设置：

```text
PHASE1_DEBUG_LOG=0
```

- `app` 不注册 Phase 1 acquisition observer，不发送 boot 或采集 ASCII 行；
- USART2 的唯一生产发送者是 Modbus 响应路径；
- 不允许 `printf`、`HAL_UART_Transmit` 调试文本或其他未经组帧的数据写入 USART2；
- 如需调试，只能使用 SWD 观察、内存状态或未来单独评审的其他 UART；
- Modbus 发送使用固定响应缓冲区和有界 `HAL_UART_Transmit()`；按当前稀疏寄存器表，最大合法正常响应远小于 256 字节；
- 单次主循环最多处理和发送一个 ADU，避免连续请求饿死采集状态机。

## 14. 主循环、并发与资源

### 14.1 服务顺序

每次 `app_service()`：

1. 更新运行时间；
2. 消费 UART 错误和最多一个完整 RTU ADU；
3. 必要时增加通信错误计数；
4. 必要时发送一个响应；
5. 推进一次 Phase 1 采集服务。

即使 Master 连续访问，采集服务仍在每个主循环执行。Modbus 核心不得等待下一字节、不得重试发送、不得调用长延时。

### 14.2 静态资源

- RX ADU：256 bytes；
- TX ADU：256 bytes；
- 寄存器快照和临时值数组使用固定长度；
- 禁止 `malloc/free`；
- 响应构造必须先验证容量；
- ISR 只做有界的 chunk 写入、一次时间戳和重新挂接；正常请求 chunk 为 8 字节，绝不解析协议或访问设备模型。

### 14.3 超时与恢复

- 帧结束：最后字节时间戳后 3 个 SysTick 计数，真实静默下界大于 2 ms；
- Host 响应超时初始值：500 ms；
- 固件不为请求设置额外事务重试；
- 发送失败后丢弃该响应，接收状态必须继续可用；
- CRC、短帧、溢出或 UART 错误后，下一个满足帧边界的合法请求必须可正常响应。

## 15. 异常响应

异常 ADU 固定为 5 字节：

```text
Address, Function | 0x80, ExceptionCode, CrcLo, CrcHi
```

| 条件 | 异常码 |
|---|---:|
| 不支持的功能码 | 0x01 |
| 起始地址不存在、范围含保留地址、写只读/保留/越界地址 | 0x02 |
| 0x03 数量非法、定长请求含额外数据、阈值超范围 | 0x03 |

异常响应只能针对地址和 CRC 均正确的完整候选请求。CRC 错误和不完整帧不得返回异常响应。

## 16. 纯 C 测试规范

现有 Phase 1 测试必须继续全部通过。Phase 2 测试加入同一 `firmware/stm32/tests/` CMake/CTest 入口，不依赖 HAL 或真实串口，并维持 MSVC `/W4 /WX` 或等价 `-Werror`。

### 16.1 CRC

- 空输入得到 `0xFFFF`；
- `01 03 00 00 00 0A` 得到 `0xCDC5`，线路字节为 `C5 CD`；
- 单字节变化导致 CRC 变化；
- 正确帧通过，任一 CRC 字节损坏被拒绝。

### 16.2 接收器

- 单次连续输入后，tick 差值小于 3 不交付、等于 3 时交付；
- 分段输入更新最后字节时间，不提前分帧；
- 连续两帧分别交付，不粘包；
- 256 字节边界可交付；
- 第 257 字节进入溢出，静默后只报告一次；
- 32 位毫秒回绕；
- 空轮询不产生事件。

### 16.3 0x03

- 每个合法连续区间的单寄存器和多寄存器读取；
- A/B/C/环境负温度补码；
- 光敏、阈值、告警、状态、通信错误计数、版本；
- 运行时间低/高字快照；
- 数量 0、126 返回 0x03；
- 保留地址、跨空洞、地址加法溢出返回 0x02；
- 响应 byte count、寄存器高字节优先和 CRC。

### 16.4 0x06

- 四路阈值分别写入和再次读取；
- -400、800 两个边界成功；
- -401、801 返回 0x03 且模型不变；
- 只读、保留和越界地址返回 0x02；
- 成功回显与 CRC；
- 合法写入触发当前有效温度的告警重算。

### 16.5 帧和异常

- 非本机地址、广播无响应且模型和计数不变；
- 本机短帧、错误 CRC 无响应且各计数一次；
- 不支持功能返回 0x01；
- 0x03/0x06 多余数据返回 0x03；
- 合法异常不增加通信错误；
- 错误恢复后的下一合法帧成功；
- 响应容量不足时不越界且不部分发送。

### 16.6 模型和调度集成

- 通信错误从 65534 加 1 到 65535，再增加仍为 65535；
- 一个 UART 错误事件只计一次；
- 一次 Modbus 服务最多处理一帧；
- 连续请求期间采集服务仍被调用；
- Phase 2 构建中 `PHASE1_DEBUG_LOG=0`，不存在 USART2 ASCII 发送调用路径。

## 17. ARM 构建与静态复核

1. 在独立新构建目录配置 ARM GCC；
2. 编译和链接无新增警告；
3. 确认 `modbus`、`register_map` 和 USART2 IRQ 代码进入 ELF；
4. 检查 `.ioc`、`stm32f4xx_it.c/.h` 和 MSP 的 USART2 NVIC 配置一致；
5. 搜索 USART2 发送调用，确认生产路径仅有 Modbus 二进制响应；
6. 构建产物和实测日志不进入 Git。

## 18. VCP/UART 真实验证

### 18.1 前置记录

- 开发板：`MB1136-F411RE-C04`，编号 `A232203276`；
- 固件版本、提交或构建标识；
- 实际枚举 COM 端口，禁止沿用固定 COM3/COM5 假设；
- 第三方 Master 名称和版本；
- 115200 8N1、Slave ID 1、500 ms 初始超时；
- 明确标注“VCP/UART 协议验证”。

### 18.2 必测步骤

1. 连续读取 0～4，核对四路温度和光敏值；
2. 分块读取 9～12、19～20、29～31、39～40；
3. 读取运行时间低/高字并按低字在低地址合成；
4. 保存四路阈值原值，分别执行合法写入和回读；
5. 执行至少一个阈值边界值写入和回读；
6. 执行只读地址写入、保留地址读取、数量 0 或超范围请求，核对 0x02/0x03；
7. 若第三方工具支持 Raw Frame，发送不支持功能和错误 CRC；不支持时只把该项保留为纯 C 已验证，不虚构硬件结果；
8. 恢复四路阈值原值并回读；
9. 连续协议访问至少覆盖三个 2 s 采集周期，确认 A/B/C 温度仍更新且状态无新增故障；
10. 观察原始 RX，确认启动和采集期间没有 ASCII 文本混入。

### 18.3 证据文件

创建 `docs/test_results/task005_vcp_uart_modbus_validation.md`，至少记录：时间、端口、参数、工具版本、每条请求、响应、解析、判定、阈值恢复结果和未执行项。工具导出的原始日志保存在 `output/logs/`，不提交大体积运行日志。

若验证需要用户复位、重新插拔、选择 COM 端口或操作 Master，应暂停并给出单一步骤提示；不得代替用户声称已操作硬件。

## 19. 验收追踪

| TASK-005 验收项 | 本规范覆盖 | 完成证据 |
|---:|---|---|
| 1 | 第 16、17 节 | CTest、ARM 干净构建 |
| 2 | 第 8、16.1、16.2 节 | CRC、短帧、边界测试 |
| 3 | 第 9、11、16.3 节 | 全部已定义字段读取测试 |
| 4 | 第 10、16.4 节 | 四路阈值写入/回读 |
| 5 | 第 9、10、15、16 节 | 0x01/0x02/0x03 测试 |
| 6 | 第 9、10、15 节 | 只读/保留地址测试 |
| 7 | 第 11.2、16.3 节 | 运行时间快照测试 |
| 8 | 第 6、7、12、16 节 | 错误事件和饱和计数测试 |
| 9 | 第 13、17 节 | 编译配置和发送路径搜索 |
| 10 | 第 18 节 | 第三方 Master 记录 |
| 11 | 第 14、18.2 节 | 跨三个采集周期的访问记录 |
| 12 | 第 1.3、18 节 | 报告明确限定为 VCP/UART |

## 20. 实施顺序

1. 建立 CRC、接收器和规范向量测试；
2. 建立 `register_map`、快照和全地址测试；
3. 建立 `modbus_slave` 及 0x03/0x06/异常响应测试；
4. 为 `DeviceModel` 增加通信错误饱和接口及测试；
5. 设置 `PHASE1_DEBUG_LOG=0` 并移除 Phase 2 的 ASCII 发送路径；
6. 在 `.ioc` 和生成代码中启用 USART2 IRQ；
7. 实现 STM32 UART 接收、错误恢复和有界发送适配；
8. 组装 `app`，复跑全部纯 C 测试；
9. 执行独立 ARM 干净构建和最终静态复核；
10. 烧录并使用第三方 Master 完成 VCP/UART 实测；
11. 同步 TASK-005、测试记录和必要文档；
12. 最终 Review 通过后再标记任务完成。

## 21. Technical Spec 评审记录

### 21.1 评审结论

结论：**通过，允许进入生产代码实现。**

本规范覆盖 TASK-005 要求的最大 ADU、接收状态、帧结束、超时、缓冲区所有权、错误恢复、CRC、功能码、寄存器映射、快照一致性、通信错误唯一入账点、ASCII 日志关闭、纯 C 测试、ARM 构建和 VCP/UART 实测门禁。没有修改寄存器语义，不需要先更新 `docs/modbus_register_map.md`。

### 21.2 必须修复

评审中识别并已在本版关闭：

1. **115200 下不能把 3.5 字符时间直接向下取整为 0 ms，且 2 个 1 ms tick 不能抵消时间戳量化误差。** 已采用 3 tick 门限，使真实静默下界大于 2 ms，并覆盖官方推荐的 1.750 ms。
2. **Phase 1 ASCII 日志会破坏 RTU 二进制流。** 已规定 Phase 2 生产构建固定 `PHASE1_DEBUG_LOG=0`，USART2 发送所有权唯一归 Modbus 响应路径。
3. **错误计数可能在 ISR、协议层和应用层重复增加。** 已规定事件只上报，由 `app` 唯一饱和入账，并明确 UART 错误导致的候选帧不再次按短帧计数。
4. **运行时间低高字可能跨更新读取。** 已要求范围验证后一次生成完整 `RegisterSnapshot`。
5. **接收 ISR 与主循环存在缓冲区竞争。** 已规定 ISR 把有界 chunk 写入纯 C 接收器、主循环在 USART2 短临界区内复制和清空，ISR 不访问设备模型。
6. **逐字节 `HAL_UART_Receive_IT()` 在 115200 下存在重挂窗口。** 首轮 500 次连续读取出现 3 次超时且通信错误计数同步增加；改为 `HAL_UARTEx_ReceiveToIdle_IT()` 批量接收后，相同测试 500/500 成功、通信错误计数 0→0，问题已关闭并登记为 `BUG-001`。

### 21.3 建议修复

无阻塞实施的建议项。第三方 Master 的具体工具和实际 COM 号只能在运行时确认，已作为 VCP/UART 实测记录字段，不属于生产设计未决项。

### 21.4 可选优化

- 后续高吞吐量需求可单独评审 `HAL_UARTEx_ReceiveToIdle_DMA()` 和环形 DMA；当前中断式 Receive-to-IDLE 已满足短请求和固定内存约束。
- 后续迁移真实 RS485 时，再增加 DE/RE 时序、发送完成中断、终端和偏置设计；不得复用本任务结论替代 TASK-002。
- 如需严格拒绝 750 us 以上的帧内间隔，可单独引入微秒级硬件定时器；当前 MVP 不增加该资源。

### 21.5 评审范围限制

本节记录的是进入实现前的 Spec 评审边界。后续实现与验证结果见第 22 节及 `docs/test_results/task005_vcp_uart_modbus_validation.md`。

## 22. 实施后验证摘要

截至 2026-09-03：

- 既有 Phase 1 与新增 Phase 2 纯 C 测试共 35 个，35/35 通过；MSVC `/W4 /WX` 无警告；
- ARM GCC 14.3.1 干净构建通过，无新增警告；RAM 3,272 B，Flash 29,124 B；
- STM32CubeProgrammer 完成最终固件烧录、校验和复位；
- PyModbus 3.13.1 在 COM3、115200 8N1、Slave ID 1 下完成全部已定义寄存器读取、四路阈值写入/回读/恢复及 0x02/0x03 异常验证；
- 修复 `BUG-001` 后，500 次连续 0x03 全部成功，通信错误计数 0→0，响应时间最小/平均/最大为 6.045/7.868/9.708 ms；
- 错误 CRC 与短帧均静默丢弃且计数各增加一次，不支持功能返回 0x01，后续合法请求正常恢复；
- 三秒无请求监听收到 0 字节，确认 USART2 没有 ASCII 调试文本混入；
- 连续访问覆盖至少四个采集周期，设备状态字维持 `0x0021`，光敏值持续更新；
- 四路阈值最终恢复为 600/600/600/400，告警字恢复为 `0x0000`；故障注入后的通信错误计数最终为 2。

本规范的实现门禁和 TASK-005 验收项均已关闭。上述结论只适用于 VCP/UART 协议层；RS485 电气层后来由 TASK-002/009/010 独立验收。
