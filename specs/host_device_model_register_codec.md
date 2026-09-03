# Host 设备模型与寄存器编解码技术规范

> 对应任务：`TASK-004 Host 设备模型与寄存器编解码`
> 状态：已实现并验证
> 版本：1.0
> 日期：2026-09-01

## 1. 目的

本规范定义 Host `device` 模块的设备领域模型、Holding Register 地址、连续读块、寄存器解码、告警阈值编码和结构化错误边界。

本模块是原始 Modbus `quint16` 寄存器与 Host 领域数据之间唯一的数据解释层。后续监控、参数配置和 TestEngine 必须复用本模块，不得各自实现地址偏移、符号转换、缩放、位解析或 32 位字组合。

## 2. 依据与优先级

本规范依据以下已存在基线编写：

1. `tasks/TASK-004-host-device-model-register-codec.md`；
2. `PROJECT_SPEC.md`；
3. `docs/architecture.md`；
4. `docs/modbus_register_map.md`；
5. 当前 `host/qt` C++17、Qt 6.8.3 和 CMake 工程骨架。

若寄存器地址、数据类型、缩放或位定义发生变化，必须先更新 `docs/modbus_register_map.md`，再同步修改本规范和实现。

## 3. 范围

### 3.1 范围内

- 定义 PDU 0-based Holding Register 地址和文档编号换算。
- 定义合法连续读块。
- 定义四路温度、四路告警阈值、光敏电压、告警、设备状态、通信错误计数、运行时间和固件版本模型。
- 将原始寄存器块解码为强类型领域数据。
- 将告警阈值工程值编码为功能码 `0x06` 所需的单寄存器地址和值。
- 返回可由调用方稳定判定的结构化编解码错误。
- 提供无串口、无硬件依赖的 Qt Test 单元测试边界。

### 3.2 范围外

- 串口、Qt SerialBus、QSerialPort 和 Modbus RTU Master 后端。
- 请求发送、队列、超时、重试、取消、线程和在线状态。
- 写入后的回读调度及写入值与回读值的比较；本模块只提供编码与回读解码能力。
- UI 格式化、本地化、弹窗、趋势图和日志展示。
- Firmware、功能码 `0x04`、`0x10` 和 Raw Frame。

## 4. 模块与文件边界

计划新增以下最小文件，具体拆分允许在不改变公共语义的前提下按实现审查微调：

```text
host/qt/src/device/
├── DeviceTypes.h
├── RegisterMap.h
├── RegisterCodec.h
└── RegisterCodec.cpp

host/qt/tests/device/
└── tst_registercodec.cpp
```

约束：

- `device` 只依赖 C++17 标准库和 Qt Core 基础类型，不依赖 Widgets、SerialPort、SerialBus 或 `communication` 的具体实现。
- 编解码器无可变全局状态、无 I/O、无日志副作用，可从任意线程独立调用。
- 公共模型保存数值语义，不保存本地化文本或带单位的展示字符串。

## 5. 地址模型

### 5.1 地址语义

代码中的协议地址一律表示 Modbus PDU 0-based 地址。文档编号只用于文档和诊断展示：

```text
文档编号 = 40001 + PDU 地址
PDU 地址 = 文档编号 - 40001
```

必须使用强类型 `PduAddress` 或具有相同约束的类型包装 `quint16`。禁止在 UI、监控或测试代码中散落 `40001 - 1`、`address - 40001` 等隐式换算。

`HoldingRegister` 使用强类型枚举维护全部已定义地址：

| 标识 | PDU 地址 | 文档编号 |
|---|---:|---:|
| `PhaseATemperature` | 0 | 40001 |
| `PhaseBTemperature` | 1 | 40002 |
| `PhaseCTemperature` | 2 | 40003 |
| `AmbientTemperature` | 3 | 40004 |
| `LightMillivolts` | 4 | 40005 |
| `PhaseAThreshold` | 9 | 40010 |
| `PhaseBThreshold` | 10 | 40011 |
| `PhaseCThreshold` | 11 | 40012 |
| `AmbientThreshold` | 12 | 40013 |
| `AlarmStatus` | 19 | 40020 |
| `DeviceStatus` | 20 | 40021 |
| `CommunicationErrorCount` | 29 | 40030 |
| `UptimeLow` | 30 | 40031 |
| `UptimeHigh` | 31 | 40032 |
| `FirmwareMajor` | 39 | 40040 |
| `FirmwareMinor` | 40 | 40041 |

必须提供显式的 `documentationNumber(PduAddress)` 辅助函数。是否提供反向换算由实现决定；若提供，非法文档编号必须返回结构化失败，不能发生无符号下溢。

### 5.2 连续读块

由于保留地址读取必须返回 `0x02 Illegal Data Address`，不得通过一次 0～40 的连续请求读取完整设备数据。模块公开以下只读 `ReadBlockDefinition` 常量：

| 读块 | 起始 PDU 地址 | 数量 | 内容 |
|---|---:|---:|---|
| `Measurements` | 0 | 5 | 四路温度、光敏电压 |
| `Thresholds` | 9 | 4 | 四路告警阈值 |
| `Status` | 19 | 2 | 告警字、设备状态字 |
| `Diagnostics` | 29 | 3 | 通信错误计数、运行时间低字和高字 |
| `Version` | 39 | 2 | 固件主版本和次版本 |

`ReadBlockDefinition` 至少包含强类型起始地址和 `quint16` 数量。通信层只能把这些定义转换为具体后端请求，不得重新解释字段含义。

## 6. 输入模型

### 6.1 原始读块

解码输入为 `RegisterBlock`：

```cpp
struct RegisterBlock {
    PduAddress startAddress;
    QVector<quint16> values;
};
```

- `values[0]` 对应 `startAddress`。
- 单个 `quint16` 已是 Modbus 后端完成高字节优先解析后的寄存器值；本模块不处理 RTU 字节流或 CRC。
- 每个专用读块解码函数要求起始地址和数量与对应 `ReadBlockDefinition` 完全一致。数量过少或过多均返回 `RegisterCountMismatch`，防止响应与请求上下文错配。

### 6.2 阈值写入输入

阈值编码输入包含：

- `TemperatureChannel`：`PhaseA`、`PhaseB`、`PhaseC`、`Ambient`；
- `double celsius`：用户或测试提供的摄氏温度工程值。

调用方不直接传入可写地址。编码器必须根据通道选择 9～12 的唯一合法地址，从 API 设计上避免向只读寄存器生成写入值。通过显式转换或外部反序列化得到的其他枚举底层值视为非法通道，必须返回结构化错误。

## 7. 输出领域模型

### 7.1 温度

`Temperature` 的权威值为 `qint16 deciCelsius`，单位 0.1 ℃，有效范围为 -400～800。可提供只读 `celsius()` 便利访问器返回 `double`，但相等性、范围校验和回读验证以 `deciCelsius` 为准，避免浮点比较歧义。

### 7.2 分块模型

- `Measurements`：A/B/C 相温度、环境温度、`quint16 lightMillivolts`。
- `AlarmThresholds`：A/B/C 相阈值和环境阈值，字段类型均为 `Temperature`。
- `AlarmStatusWord`：原始 `quint16 raw`，并提供位 0～3 的类型化查询。
- `DeviceStatusWord`：原始 `quint16 raw`，并提供位 0～5 的类型化查询。
- `Diagnostics`：`quint16 communicationErrorCount`、`quint32 uptimeSeconds`。
- `FirmwareVersion`：`quint16 major`、`quint16 minor`；版本字符串格式属于 UI 或展示层，不是权威数据。

### 7.3 完整设备快照

`DeviceSnapshot` 组合上述全部分块模型，覆盖当前寄存器表的所有已定义字段。

完整快照由五个读块组装。输入读块顺序不影响结果，但必须恰好各出现一次；缺失、重复或未知读块均返回结构化错误。

五个读块来自多次通信请求，因此 `DeviceSnapshot` 只表示一组组合数据，不宣称跨读块时间原子性。请求时间、设备 ID、请求 ID、数据新鲜度和通信证据由后续通信或监控模型持有，不放入本模块。

### 7.4 阈值写入值

编码输出为：

```cpp
struct RegisterWrite {
    PduAddress address;
    quint16 rawValue;
    Temperature value;
};
```

`rawValue` 保留负数的 16 位二进制补码位模式。后续通信层使用 `address` 和 `rawValue` 发起单寄存器写入；`value` 用于无浮点歧义的回读比较。

## 8. 编解码规则

### 8.1 16 位有符号温度解码

必须先按二进制补码解释原始位模式，再做范围校验：

```text
raw <= 0x7FFF 时：signed = raw
raw >  0x7FFF 时：signed = raw - 0x10000
```

禁止先把 `quint16` 与 -400 比较，也不得依赖超范围无符号到有符号转换的实现定义行为。

示例：

| 原始值 | 定点值 | 工程值 | 结果 |
|---:|---:|---:|---|
| `0x0000` | 0 | 0.0 ℃ | 成功 |
| `0x0160` | 352 | 35.2 ℃ | 成功 |
| `0xFFFF` | -1 | -0.1 ℃ | 成功 |
| `0xFE70` | -400 | -40.0 ℃ | 成功 |
| `0x0320` | 800 | 80.0 ℃ | 成功 |
| `0xFE6F` | -401 | -40.1 ℃ | `ValueOutOfRange` |
| `0x0321` | 801 | 80.1 ℃ | `ValueOutOfRange` |

四路测量温度和四路告警阈值使用相同解码规则。

### 8.2 光敏电压

光敏寄存器直接解释为毫伏：

- 0～3300：成功；
- 3301～65535：`ValueOutOfRange`。

不得转换为照度、百分比或伏特字符串。

### 8.3 告警状态字

定义位掩码：

- bit 0：A 相温度高告警；
- bit 1：B 相温度高告警；
- bit 2：C 相温度高告警；
- bit 3：环境温度高告警。

位 4～15 不映射为业务状态。解码器保留完整 `raw`，并允许调用方通过 `reservedBits()` 或等价只读接口观察未知位；未知位置位不导致整个状态字解码失败，以保留前向兼容性。

### 8.4 设备状态字

定义位掩码：

- bit 0：运行中；
- bit 1：A 相传感器故障；
- bit 2：B 相传感器故障；
- bit 3：C 相传感器故障；
- bit 4：光敏 ADC 故障；
- bit 5：环境温度为模拟值。

位 6～15 的处理与告警状态字相同：保留原始值，不创建业务含义，不因未知位置位而失败。

### 8.5 通信错误计数

直接保留 `quint16` 原始值。Host 编解码器不推断增量、不检测回绕，也不修改固件的饱和计数语义。

### 8.6 系统运行时间

`Diagnostics` 读块中的低字位于 PDU 地址 30，高字位于地址 31：

```text
uptimeSeconds = quint32(lowWord) | (quint32(highWord) << 16)
```

必须先扩展为 `quint32` 再左移。禁止交换高低字，也不得把两个寄存器内部的字节再次交换。

### 8.7 固件版本

主、次版本分别保留为 `quint16`，不做未经寄存器表规定的范围限制。`major.minor` 等展示格式不属于编解码结果。

### 8.8 告警阈值编码

按以下顺序校验：

1. 通道必须是四个已定义值之一，其他底层值返回 `InvalidChannel`。
2. 输入必须是有限值；`NaN` 和正负无穷返回 `NonFiniteValue`。
3. 计算 `scaled = celsius * 10.0`。
4. `scaled` 与最近整数的绝对差必须不大于 `1e-9`，否则返回 `InvalidPrecision`。
5. 最近整数必须位于 -400～800，否则返回 `ValueOutOfRange`。
6. 将该整数编码为 16 位二进制补码位模式，并根据通道返回 PDU 地址 9、10、11 或 12。

边界示例：

| 输入 | 定点值 | 原始值 | 结果 |
|---:|---:|---:|---|
| -40.0 ℃ | -400 | `0xFE70` | 成功 |
| -0.1 ℃ | -1 | `0xFFFF` | 成功 |
| 80.0 ℃ | 800 | `0x0320` | 成功 |
| -40.1 ℃ | -401 | — | `ValueOutOfRange` |
| 80.1 ℃ | 801 | — | `ValueOutOfRange` |
| 20.05 ℃ | — | — | `InvalidPrecision` |

不得截断、饱和或静默四舍五入非法输入。回读验证必须比较编码后的定点值与解码后的定点值。

## 9. 公共操作语义

公共 API 至少覆盖以下能力，名称可以按项目风格微调，但输入、输出和错误行为不得改变：

```cpp
CodecResult<Measurements> decodeMeasurements(const RegisterBlock &block);
CodecResult<AlarmThresholds> decodeThresholds(const RegisterBlock &block);
CodecResult<Status> decodeStatus(const RegisterBlock &block);
CodecResult<Diagnostics> decodeDiagnostics(const RegisterBlock &block);
CodecResult<FirmwareVersion> decodeFirmwareVersion(const RegisterBlock &block);
CodecResult<DeviceSnapshot> decodeDeviceSnapshot(const QVector<RegisterBlock> &blocks);
CodecResult<RegisterWrite> encodeAlarmThreshold(TemperatureChannel channel,
                                                double celsius);
```

`CodecResult<T>` 使用 C++17 可用的显式值/错误联合类型，例如 `std::variant<T, CodecError>`；不得通过异常、日志文本、空对象或魔法数表示预期的输入错误。

分块解码采用全有或全无语义：任一字段失败时不返回部分成功对象。调用方可保留上一次有效数据，但该策略不属于编解码层。

## 10. 结构化错误

### 10.1 错误类别

`CodecErrorCode` 至少包含：

| 错误码 | 触发条件 |
|---|---|
| `AddressMismatch` | 专用解码函数收到错误起始地址 |
| `RegisterCountMismatch` | 寄存器数量与读块定义不相等 |
| `MissingReadBlock` | 完整快照缺少必需读块 |
| `DuplicateReadBlock` | 完整快照包含重复读块 |
| `UnknownReadBlock` | 完整快照包含未定义起始地址的读块 |
| `ValueOutOfRange` | 温度、阈值或光敏值越界 |
| `InvalidPrecision` | 阈值输入不是 0.1 ℃ 的整数倍 |
| `NonFiniteValue` | 阈值输入为 `NaN` 或无穷 |
| `InvalidChannel` | 阈值编码收到未定义的通道枚举值 |

### 10.2 错误上下文

`CodecError` 至少提供：

- 稳定的 `CodecErrorCode code`；
- 稳定的字段标识 `DeviceField field`，无法定位单字段时使用 `None`；
- 适用时的实际和期望 PDU 地址；
- 适用时的实际和期望寄存器数量；
- 适用时的实际数值及允许上下界。

错误文本只能作为辅助诊断，调用方必须依据错误码和结构化上下文分支。`device` 模块不得弹窗，也不得把本地化字符串作为唯一错误信息。

若同一输入同时存在多个错误，按以下稳定顺序返回第一个错误：读块身份/地址、数量、字段从低地址到高地址的数值校验。完整快照依次检查未知读块、重复读块、缺失读块，再按五个读块的地址顺序解码；同一类别有多个候选时返回 PDU 地址最小者。阈值编码按 8.8 节列出的顺序返回首错。

## 11. 边界与异常行为

- 空读块：`RegisterCountMismatch`。
- 正确起始地址但少一个或多一个寄存器：`RegisterCountMismatch`。
- 数据块地址偏移一位：`AddressMismatch`，不得尝试猜测或自动平移。
- 完整快照块顺序变化：成功。
- 状态字保留位置位：成功，原始值保留，未定义业务访问器仍只反映已定义位。
- `0xFFFFFFFF` 运行时间：成功，结果为 4294967295 秒。
- 固件主次版本为 0 或 65535：成功。
- 任意失败均不修改输入，也不产生部分输出或外部副作用。

## 12. 测试规范

单元测试使用 Qt Test，经 CTest 运行，不创建 `IModbusClient`，不访问串口、COM5 或真实硬件。

### 12.1 地址和读块

- 每个 `HoldingRegister` 的 PDU 地址和文档编号。
- 40001 显式映射到 PDU 地址 0。
- 五个读块的起始地址、数量和末地址。
- 错误起始地址、空块、少一个和多一个寄存器。
- 完整快照读块乱序、缺失、重复和未知读块。

### 12.2 温度与阈值

- 0、35.2 ℃、`0xFFFF`/-0.1 ℃、-40.0 ℃和 80.0 ℃。
- -40.1 ℃和 80.1 ℃解码失败。
- 四个阈值通道的写地址映射。
- 未定义的通道枚举值返回 `InvalidChannel`。
- -40.0 ℃、-0.1 ℃、0、80.0 ℃编码位模式。
- `NaN`、正负无穷、-40.1 ℃、80.1 ℃和 20.05 ℃被拒绝。
- 编码后再解码得到相同 `deciCelsius`。

### 12.3 光敏、状态、诊断和版本

- 光敏 0、3300 成功，3301 和 65535 失败。
- 告警字每个已定义位、组合位、全保留位和原始值保留。
- 设备状态字每个已定义位、组合位、全保留位和原始值保留。
- 运行时间 0、仅低字、仅高字、低字 `0xFFFF`、跨高低字和 `0xFFFFFFFF`。
- 通信错误计数 0 和 65535。
- 固件版本 0.0、典型值和 65535.65535。

### 12.4 错误模型

- 各错误路径断言 `CodecErrorCode`，不能只比较文本。
- 验证字段、地址、数量或范围上下文足以定位错误。
- 验证多错误输入的首错顺序稳定。

## 13. 验收追踪

| TASK-004 验收项 | 本规范对应章节 |
|---|---|
| 覆盖全部已定义寄存器字段 | 5、7、8 |
| 40001 与 PDU 0 明确区分 | 5.1、12.1 |
| 负温度及上下边界 | 8.1、12.2 |
| 运行时间低字在低地址 | 8.6、12.3 |
| 状态位和保留位 | 8.3、8.4、12.3 |
| 阈值范围和 0.1 ℃ 精度 | 8.8、12.2 |
| 缺失、长度、非法输入错误 | 10、11、12.4 |
| 无串口和硬件依赖 | 3.2、4、12 |
| 不实现 UI 或后端 | 3.2、4 |

实现阶段的构建和 CTest 结果见第 15 节。

## 14. 评审结论

### 14.1 一致性评审

- 与 `docs/modbus_register_map.md` 的地址、类型、缩放、位定义和字序一致。
- 与架构中的 `device` 模块职责、原始 `quint16` 边界和结构化错误要求一致。
- 与 TASK-004 的范围及非范围一致，未提前绑定 Modbus 后端或扩展功能码。
- TASK-004 的目标描述未单独列出阈值字段，但其范围和验收要求覆盖全部已定义字段；本规范将四路阈值纳入 `DeviceSnapshot`，以满足更完整且可验证的任务约束。

### 14.2 可实现性评审

- 当前工程使用 C++17、Qt Core 和 Qt Test，足以实现强类型、`std::variant`、`QVector` 和无硬件单元测试，不需要新增第三方依赖。
- 五个连续读块避开保留地址，符合 Slave 对保留地址返回 `0x02` 的约束。
- 明确的二进制补码算法不依赖编译器对超范围有符号转换的实现选择。
- 编解码器无 I/O 和共享状态，后续可供监控和 TestEngine 并发调用。

### 14.3 可测试性评审

- 每类正常值、边界值、负值和错误路径均有确定输入、输出或错误码。
- 完整快照的缺失、重复、未知和乱序读块行为已固化。
- 浮点输入精度容差、首错顺序和分块全有或全无语义已明确，测试不会依赖模糊行为。

### 14.4 风险和后续约束

- DHTC12 实物原始值解释仍待 Firmware 侧验证，但不阻塞对已定义 Host 寄存器二进制补码语义的实现和单元测试；真实数据一致性测试仍不得提前标记通过。
- 完整快照由五次请求组合，跨块一致性和新鲜度必须由后续监控/通信任务管理。
- 写入后的实际发送、响应处理和回读验证属于后续通信与配置任务，本任务不得以纯编解码测试冒充端到端写入验证。

### 14.5 结论

评审未发现阻塞实现的需求冲突或架构未决项。本规范达到 TASK-004 进入编码实现的条件；实现已保持上述公共语义，实际验证结果见第 15 节。

## 15. 实现与验证记录

> 验证日期：2026-09-01

- 已在 `host/qt/src/device` 实现设备类型、强类型 PDU 地址、五个连续读块、分块/完整快照解码、阈值编码和结构化错误。
- 已在 `host/qt/tests/device/tst_registercodec.cpp` 实现无串口、无硬件依赖的 Qt Test，覆盖第 12 节要求的正常、边界和错误路径。
- 使用全新目录 `build-host-task004` 完成 CMake 配置，编译器为 MSVC 19.51.36256.0，Qt 为 6.8.3。
- Host 构建成功；CTest 共发现 4 个测试目标并全部通过，其中 `host.device.registercodec` 为本任务新增测试。
- 本任务未访问 COM5 或真实硬件，也未实现 UI、Modbus 后端或写入回读调度。
