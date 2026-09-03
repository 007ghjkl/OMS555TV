# Firmware Phase 1 基础采集与设备模型技术规范

> 对应任务：`tasks/TASK-003-firmware-phase1-basic-acquisition.md`
>
> 状态：评审完成，通过
>
> 版本：1.0
>
> 日期：2026-09-03
> 硬件验证状态：未在真实硬件环境验证

## 1. 目的

本规范定义 STM32F411 Phase 1 的传感器采集、工程量转换、设备模型、告警、故障判定、调试输出和纯 C 测试边界。实现完成后，应稳定产生三路 DHTC12 温度、一路光敏模拟电压和一路明确标记的模拟环境温度，并为后续 `register_map` 提供与 `docs/modbus_register_map.md` 一致的内存模型。

### 1.1 输入

- 三个独立 I2C 控制器上的 DHTC12 原始帧及 I2C 事务结果；
- PA0/ADC1_IN0 的 12 bit 原始采样及 ADC 事务结果；
- 1 ms 单调模 2^32 时基；
- 四路默认温度阈值和固定模拟环境温度；
- 第 2 节列出的任务、需求、架构、硬件和寄存器基线。

### 1.2 输出

- 三路真实温度和一路模拟环境温度的定点设备模型；
- 光敏模拟电压、有效性、结构化采集错误和更新时间；
- 告警字、设备状态字、运行时间和固件版本；
- 足以复核原始值、CRC、工程值和状态转换的 USART2 调试记录；
- 不依赖 HAL 和真实硬件的纯 C 单元测试及可复现构建入口。

### 1.3 非范围

本规范不授权实现 Modbus RTU、RS485、Host 生产功能、参数持久化、FreeRTOS、看门狗或湿度公共寄存器。

## 2. 依据与优先级

实现必须同时遵守以下资料：

1. `tasks/TASK-003-firmware-phase1-basic-acquisition.md`；
2. 本规范；
3. `docs/architecture.md`；
4. `docs/prd.md`；
5. `PROJECT_SPEC.md`；
6. `docs/hardware_baseline.md` 基线 1.0；
7. `docs/modbus_register_map.md`；
8. `docs/hardware_info/温湿度传感器/DHTC12.pdf`。

若上述资料出现无法按优先级消解的冲突，必须先更新并重新评审本规范，不得通过代码中的隐含行为绕过。

## 3. 已确认约束与实施门禁

### 3.1 已确认约束

- MCU 为 STM32F411RET6，使用 C11、STM32 HAL 和裸机事件循环。
- A/B/C 三只 DHTC12 分别连接 I2C1、I2C2、I2C3，7 位地址均为 `0x44`，I2C 时钟为 50 kHz。手册给出的 100 kHz 是上限，且 SCK 高、低电平时间最小均为 5 us；真实硬件在 100 kHz 下出现命令写入成功、读取持续 NACK 后总线保持 BUSY，因此 GATE-03 阶段先降速留出时序裕量。
- DHTC12 同时测量命令为两个字节 `0x2C 0x10`。
- 同时测量返回 6 字节：温度高字节、温度低字节、温度 CRC、湿度高字节、湿度低字节、湿度 CRC。
- 温度与湿度两个 16 位数据字分别独立执行 CRC-8；参数为多项式 `0x31`、初值 `0xFF`、非反转、无最终异或。
- 对同一只 DHTC12，两个主动测量命令的起始时刻间隔不得小于 2000 ms。
- 光敏输入为 PA0/ADC1_IN0，12 bit，VDDA 理想值按 3300 mV 处理；结果只表示模拟电压，不表示照度。
- 温度公开表示为 `int16_t`、单位 0.1 ℃，有效范围为 `[-400, 800]`。
- 环境温度为软件模拟值，状态字 `AMBIENT_SIMULATED` 必须始终置位。
- 高温告警迟滞固定为 5.0 ℃，本阶段不可配置。

### 3.2 必须关闭的实施门禁

| 门禁 | 当前状态 | 关闭条件 | 影响 |
|---|---|---|---|
| GATE-01 DHTC12 原始温度有符号解释 | 已关闭（2026-09-03） | 空调设定 26 ℃、低风速作为现场参考；A/B/C 分别稳定得到 26.2/26.0/25.9 ℃，连续 18 帧温度、湿度 CRC 全部通过 | `St` 按大端 `int16_t` 解释已获实物证据，允许正式发布工程值并驱动告警 |
| GATE-02 ADC 采样时间配置 | 已关闭（2026-09-02） | `.ioc` 与生成的 `main.c` 均已改为 84 cycles；干净 ARM 构建及光敏实测日志已复核 | 已满足光敏硬件验收的配置前提 |
| GATE-03 DHTC12 转换等待参数 | 已关闭（2026-09-03） | A 路早期 13 个连续周期均在 70 ms 取得有效帧；B/C 复接后，三路连续 6 周期分别在 50/53/51 ms 取得有效帧；最终采用首次等待/轮询 50 ms、截止 250 ms | 5 ms 高频轮询会使器件持续 NACK；50 ms 参数已通过三路实物验证 |
| GATE-04 DHTC12 初始化与总线恢复 | 已关闭（2026-09-02） | 按手册在测量前发送 `0x30A2` 软件复位；对 SDA 锁低执行有界总线恢复；Fake 测试、ARM 构建和实物重连均通过 | 22 个本机测试、ARM 构建、断线错误路径、重连初始化和失败后恢复均已通过 |

### 3.3 温度原始值解释

手册给出的公式为：

```text
T(℃) = 40 + St / 256
```

温度原始字的高字节和低字节按大端拼接后，将 `St` 解释为 `int16_t`。该解释能覆盖手册声明的负温范围，并已由三路室温实物数据关闭 GATE-01。

GATE-01 关闭前的诊断版本遵循以下限制；2026-09-03 起生产构建不再使用该模式：

- 调试日志必须输出原始 16 位十六进制值；
- 可以输出带 `UNVERIFIED` 标签的候选工程值；
- 设备模型中的该通道仍标记为无有效工程值；
- 不得用候选值驱动生产告警；
- 候选转换测试只能标记为 `PENDING_HARDWARE_CONFIRMATION`，不能计入通过数。

## 4. 模块边界与目录

### 4.1 目标边界

| 模块 | 职责 | HAL 依赖 |
|---|---|---|
| `platform` | I2C、ADC、UART、毫秒时基的 STM32 HAL 适配；把 HAL 状态转换为项目错误类型 | 允许 |
| `acquisition` | DHTC12/ADC 调度状态机、帧接收、CRC 调用、转换调用、采样结果事件 | 禁止直接包含 HAL 头文件 |
| `device_model` | 保存工程值、有效性、阈值、状态字、告警字、运行时间和版本 | 禁止 |
| `alarm` | 四路温度告警的迟滞状态转换 | 禁止 |
| `app` | 组装模块并由 `main()` 周期调用 | 只通过 `platform` 使用 HAL |

推荐目录：

```text
firmware/stm32/
├── App/
│   ├── app/
│   ├── platform/
│   ├── acquisition/
│   ├── device_model/
│   └── alarm/
└── tests/
```

CubeMX 生成的 `Core/Src/main.c` 只在 `USER CODE` 区调用 `app_init()` 和 `app_service()`。应用源文件通过顶层 `firmware/stm32/CMakeLists.txt` 的用户源区加入构建，不写入 CubeMX 维护的生成源列表。

### 4.2 依赖方向

```text
main -> app -> acquisition -> platform 接口
            -> device_model -> alarm
acquisition -> dhtc12_codec（纯 C）
```

`device_model`、`alarm`、CRC、原始帧解析、定点转换和调度判断必须能由本机 C 编译器构建，不得包含 `stm32f4xx_hal.h`。

## 5. 公共数据类型

以下名称是规范级接口名称；实现可在不改变语义的前提下按现有代码风格调整局部命名。

```c
typedef enum {
    TEMP_CHANNEL_A = 0,
    TEMP_CHANNEL_B,
    TEMP_CHANNEL_C,
    TEMP_CHANNEL_AMBIENT,
    TEMP_CHANNEL_COUNT
} TemperatureChannel;

typedef enum {
    ACQ_ERROR_NONE = 0,
    ACQ_ERROR_I2C_TRIGGER,
    ACQ_ERROR_I2C_TIMEOUT,
    ACQ_ERROR_I2C_BUS,
    ACQ_ERROR_SHORT_FRAME,
    ACQ_ERROR_TEMP_CRC,
    ACQ_ERROR_HUMIDITY_CRC,
    ACQ_ERROR_TEMP_CONVERSION_UNCONFIRMED,
    ACQ_ERROR_TEMP_OUT_OF_RANGE,
    ACQ_ERROR_ADC_START,
    ACQ_ERROR_ADC_TIMEOUT,
    ACQ_ERROR_ADC_READ,
    ACQ_ERROR_ADC_OUT_OF_RANGE
} AcquisitionError;

typedef struct {
    int16_t value_deci_c;
    bool valid;
    uint32_t updated_at_ms;
    AcquisitionError last_error;
    uint8_t consecutive_crc_failures;
    uint8_t consecutive_invalid_cycles;
} TemperatureState;

typedef struct {
    uint16_t value_mv;
    bool valid;
    uint32_t updated_at_ms;
    AcquisitionError last_error;
} LightState;

typedef struct {
    TemperatureState temperatures[TEMP_CHANNEL_COUNT];
    LightState light;
    int16_t thresholds_deci_c[TEMP_CHANNEL_COUNT];
    uint16_t alarm_bits;
    uint16_t status_bits;
    uint16_t communication_error_count;
    uint32_t uptime_seconds;
    uint16_t firmware_version_major;
    uint16_t firmware_version_minor;
} DeviceModel;
```

约束：

- 业务模型不得使用浮点数。
- `communication_error_count` 在 Phase 1 初始化为 0，本任务不更新；其正式计数口径由 Modbus 任务实现。
- 失败采样不得覆盖最后一次有效工程值，只更新有效性、错误和故障计数。
- 所有对外位定义必须与 `docs/modbus_register_map.md` 完全一致，保留位始终为 0。
- 计数器达到其业务上限后饱和，不回绕；DHTC12 连续计数器的业务上限为 3。

## 6. 初始化与主循环

### 6.1 初始化顺序

1. HAL、时钟和 CubeMX 外设初始化成功；
2. `device_model_init()` 写入确定的默认值；
3. `alarm_init()` 清除告警状态；
4. `acquisition_init(now_ms)` 建立三个 DHTC12 初始化/采集状态机和 ADC 状态机；
5. `app_init()` 完成后置位 `RUNNING`；
6. 主循环持续调用 `app_service(platform_millis())`。

默认模型：

| 字段 | 默认值 |
|---|---:|
| A/B/C 温度 | 0，`valid=false` |
| 环境温度 | 250（25.0 ℃），`valid=true` |
| 光敏电压 | 0，`valid=false` |
| A/B/C 阈值 | 600（60.0 ℃） |
| 环境阈值 | 400（40.0 ℃） |
| 告警字 | 0 |
| 状态字 | `AMBIENT_SIMULATED`；调度启动后再加 `RUNNING` |
| 通信错误计数 | 0 |
| 运行时间 | 0 s |
| 固件版本 | 0.1 |

### 6.2 非阻塞约束

- 禁止使用 `HAL_Delay()` 等待 DHTC12 转换完成或等待下一个采样周期。
- 总线恢复只允许在 DHTC12 初始化/恢复状态执行，必须有界；不得把阻塞延时用于正常测量调度。
- 每次 `app_service()` 对每个采集器最多推进一个状态转换或执行一次短外设事务。
- I2C 单次 HAL 事务超时为 10 ms；ADC 单次转换等待超时为 1 ms。
- 调试 UART 发送必须使用有界缓冲区；单条日志不得超过 256 bytes。
- 所有截止时间比较使用无符号差值，必须正确处理 32 bit 毫秒时基回绕。

## 7. 调度状态机

### 7.1 DHTC12 通道状态

```text
INIT_RECOVER ----恢复失败----> INIT_FAILED -> 2000 ms 后重试 INIT_RECOVER
  |
  | 总线空闲或恢复成功
  v
INIT_RESET ----0x30A2 失败----> INIT_FAILED -> 2000 ms 后重试 INIT_RECOVER
  |
  | 软件复位成功，等待 2000 ms
  v
IDLE
  | 到达 next_trigger_ms
  v
TRIGGER ----发送失败----> CYCLE_FAILED -> INIT_RECOVER（下次动作仍距本次命令尝试 >= 2000 ms）
  |
  | 命令成功
  v
WAIT_READY ----尚未就绪且未超时----> WAIT_READY
  |                                  |
  | 收到 6 字节                       | 达到转换超时
  v                                  v
VALIDATE ------------------------> CYCLE_FAILED
  |
  | 两个 CRC 与转换/范围均有效
  v
CYCLE_SUCCEEDED -> IDLE
```

- 三个通道状态完全独立；通道与 I2C 控制器的映射固定为 A/I2C1、B/I2C2、C/I2C3。
- 首次初始化偏移分别为 0 ms、25 ms、50 ms，避免连续恢复事务和日志突发。
- 每个通道必须先完成总线恢复检查和 `0x30A2` 软件复位；软件复位成功后，首次 `0x2C10` 不得早于 2000 ms。
- 初始化失败按 2000 ms 周期重新从 `INIT_RECOVER` 开始；初始化成功前不得发送测量命令。
- `next_trigger_ms` 以本次命令尝试的起始时刻加 2000 ms 计算，无论本次命令是否成功。
- 一个测量周期从主动命令尝试开始，到有效帧产生或被判定失败结束。
- 在 2000 ms 周期内不得通过重新发送 `0x2C10` 做重试；只允许在转换截止时间内轮询读取。
- 触发失败、非 NACK 读取错误或转换截止超时均视为 I2C 传输链路不再可信；记录本周期失败后必须转入 `INIT_RECOVER`，到达原定的下一采样节点后执行总线恢复和软件复位。CRC、帧长度和工程值范围错误不表示总线失步，仍返回 `IDLE`。

### 7.2 ADC 状态

```text
IDLE -> START_SAMPLE -> WAIT_SAMPLE -> ACCUMULATE
          ^                              |
          |-------- 未满 16 次 -----------|
                                         |
                           满 16 次 -> PUBLISH -> IDLE
```

- ADC 批次周期为 2000 ms。
- 每批包含 16 个有效原始样本。
- 任一次 START、等待、读取或范围校验失败，立即丢弃整批，置 `LIGHT_ADC_FAULT`，保留最后有效电压，并等待下一批次。
- 一批成功后发布新值并清除 `LIGHT_ADC_FAULT`。

## 8. DHTC12 协议

### 8.1 地址规则

业务接口只接收 7 位地址 `0x44`。只有 `platform` HAL 适配层可以按 STM32 HAL 要求执行左移；业务代码禁止保存或传递已经左移的 `0x88/0x89`，以避免读写地址混淆。

### 8.2 命令与读取

初始化流程：

1. `platform` 暂停对应 I2C 外设并检查 SCL/SDA；
2. SDA 为低时，以开漏方式最多产生 9 个 SCL 脉冲，再生成 STOP；恢复总耗时必须小于 25 ms；
3. 恢复硬件 I2C 配置并验证 SCL/SDA 均为高；否则记为 `ACQ_ERROR_I2C_RECOVERY`；
4. 向 7 位地址 `0x44` 写入软件复位命令 `0x30 0xA2`；失败记为 `ACQ_ERROR_I2C_RESET`；
5. 软件复位成功后等待 2000 ms，再允许首次主动测量。

正常测量流程：

1. 向 7 位地址 `0x44` 写入 `0x2C 0x10`；
2. 命令成功后进入 `WAIT_READY`；
3. 每隔 50 ms 尝试一次 6 字节读取；
4. 传感器 NACK 映射为 `PLATFORM_I2C_NOT_READY`，在 250 ms 转换截止时间前不计为失败周期；
5. 总线错误、仲裁错误等非 NACK 错误立即结束本周期，并在下一采样节点重新执行总线恢复和软件复位；
6. 到达 250 ms 仍未取得 6 字节，记为 `ACQ_ERROR_I2C_TIMEOUT`，并在下一采样节点重新执行总线恢复和软件复位；
7. 读到的帧长度不是 6 时，记为 `ACQ_ERROR_SHORT_FRAME`。

首版 5 ms 高频轮询在 100 kHz 和 50 kHz 下都会使器件在 200 ms 内持续 NACK；改为首次等待 50 ms 后，A 路 13 个连续周期均在日志时间 70 ms 取得双 CRC 有效帧。最终采用 50 ms 轮询和 250 ms 截止，在已观测值之外保留三倍以上裕量。

### 8.3 帧布局与有效性

| 偏移 | 含义 | 校验 |
|---:|---|---|
| 0 | 温度原始值高字节 | 与偏移 1 一起计算 CRC |
| 1 | 温度原始值低字节 | 与偏移 0 一起计算 CRC |
| 2 | 温度 CRC | 必须等于 `crc8(frame[0..1])` |
| 3 | 湿度原始值高字节 | 与偏移 4 一起计算 CRC |
| 4 | 湿度原始值低字节 | 与偏移 3 一起计算 CRC |
| 5 | 湿度 CRC | 必须等于 `crc8(frame[3..4])` |

即使湿度不进入公共设备模型，一个有效 DHTC12 周期仍要求温度 CRC 和湿度 CRC 都正确。这样可以证明完整的 6 字节传输有效；任一 CRC 错误均不得发布温度工程值。

### 8.4 CRC-8

参考接口：

```c
uint8_t dhtc12_crc8(const uint8_t *data, size_t length);
```

算法：

```text
crc = 0xFF
for each byte:
    crc ^= byte
    repeat 8 times:
        if crc bit7 is 1: crc = (crc << 1) XOR 0x31
        else:             crc = crc << 1
```

所有运算截断为 8 bit。规范测试向量：

| 数据 | 预期 CRC |
|---|---:|
| `00 00` | `81` |
| `FF FF` | `AC` |
| `12 34` | `37` |
| `F1 00` | `6D` |

### 8.5 定点温度转换

GATE-01 已关闭，正式转换为：

```text
raw_u16 = (msb << 8) | lsb
st = reinterpret_as_int16(raw_u16)
delta_deci = round_to_nearest(st * 10 / 256)
temperature_deci_c = 400 + delta_deci
```

`round_to_nearest` 必须对正负数对称，不得依赖 C 对负数除法的截断方向：

```text
scaled = st * 10
delta_deci = scaled >= 0
    ? (scaled + 128) / 256
    : -((-scaled + 128) / 256)
```

中间计算使用 `int32_t`。转换结果低于 -400 或高于 800 时判为 `ACQ_ERROR_TEMP_OUT_OF_RANGE`，不得钳位后发布。以下边界向量已纳入通过的本机测试：

| 原始值 | `St` | 工程值 |
|---:|---:|---:|
| `0xB000` | -20480 | -400（-40.0 ℃） |
| `0xF100` | -3840 | 250（25.0 ℃） |
| `0x0000` | 0 | 400（40.0 ℃） |
| `0x2800` | 10240 | 800（80.0 ℃） |

## 9. ADC 采集与换算

### 9.1 CubeMX 配置

ADC1 必须满足：

- 通道：ADC1_IN0；
- 分辨率：12 bit；
- 数据对齐：右对齐；
- 软件触发、单次转换；
- ADC 时钟：PCLK2 / 4；
- 采样时间：84 cycles；
- 不启用连续转换和 DMA。

当前 `.ioc` 和生成代码仍为 3 cycles。实现开始时必须先修改 `.ioc` 再重新生成，禁止只手改生成代码。

### 9.2 平均与毫伏换算

```text
sample_count = 16
average_raw = (sum_raw + sample_count / 2) / sample_count
light_mv = average_raw * 3300 / 4095
```

- `sum_raw` 使用 `uint32_t`。
- 每个原始样本必须在 `[0, 4095]`；超范围使整批失败。
- 毫伏换算使用整数除法，结果范围为 `[0, 3300]`。
- 不使用外部参考电压实测值，不宣称校准精度。

规范向量：

| 输入 | 预期结果 |
|---|---:|
| 全部为 0 | 0 mV |
| 全部为 4095 | 3300 mV |
| 全部为 2048 | 1650 mV |
| 8 个 0 与 8 个 4095 | 平均原始值 2048，1650 mV |
| 任一输入为 4096 | 整批失败，`ADC_OUT_OF_RANGE` |

## 10. 模拟环境温度

- Phase 1 使用固定值 250，即 25.0 ℃。
- 初始化后即为有效值，可通过测试注入接口替换，以验证告警逻辑；生产运行时本任务不提供动态配置入口。
- `AMBIENT_SIMULATED` 从初始化开始置位，任何成功/失败路径都不得清除。
- 模拟环境温度参与与真实通道相同的阈值和迟滞计算，但日志必须标记 `source=SIMULATED`。

## 11. 告警规则

每个温度通道独立维护一个告警位。只有 `valid=true` 的新温度或合法阈值变更才能触发重新计算：

```text
若告警未置位且 temperature >= threshold：置位
若告警已置位且 temperature <= threshold - 50：清除
其他情况：保持原状态
```

- 计算 `threshold - 50` 时使用 `int32_t`，再比较，避免边界运算隐患。
- 无效采样不改变最后的告警状态；对应故障位用于表达数据不可用。
- 阈值合法范围沿用寄存器基线 `[-400, 800]`。
- 当阈值为 -400 时，恢复点为 -450，超出有效测温范围；告警一旦触发将无法由有效样本自动清除。这是当前范围和固定迟滞共同导致的已知边界，不得在实现中静默钳位。
- 后续阈值写入应通过 `device_model_set_threshold()` 完成；合法写入后，若当前样本有效，立即重新计算该通道告警。

告警位：A/B/C/环境分别为 bit 0/1/2/3，bit 4～15 固定为 0。

## 12. 故障、有效性与恢复

### 12.1 DHTC12 计数

每个真实温度通道独立维护：

- `consecutive_crc_failures`：仅连续 CRC 错误时递增；遇到非 CRC 结果或成功时清零；
- `consecutive_invalid_cycles`：任何未发布有效工程值的完整周期递增；成功时清零。

CRC 错误同时使两个计数器递增。计数器在 3 饱和。任一计数器达到 3 时置对应 `*_SENSOR_FAULT`。

成功周期必须同时满足：

1. 收到完整 6 字节；
2. 温度 CRC 正确；
3. 湿度 CRC 正确；
4. GATE-01 已关闭；
5. 温度转换结果在有效范围内。

一次成功周期立即：

- 发布新温度和更新时间；
- 置 `valid=true`；
- 清除两个连续计数器；
- 清除对应传感器故障位；
- 更新对应告警。

失败周期保留最后有效工程值和告警状态，置 `valid=false`，更新 `last_error`。启动后前三个无有效数据周期内按任务基线累计，第三个周期结束时才置故障位。

### 12.2 状态位

| 位 | 名称 | Phase 1 行为 |
|---:|---|---|
| 0 | `RUNNING` | `app_init()` 成功并启动调度后置位 |
| 1 | `A_SENSOR_FAULT` | A 通道任一连续计数达到 3 时置位；一次成功清除 |
| 2 | `B_SENSOR_FAULT` | 同上 |
| 3 | `C_SENSOR_FAULT` | 同上 |
| 4 | `LIGHT_ADC_FAULT` | ADC 一批失败时置位；下一批成功时清除 |
| 5 | `AMBIENT_SIMULATED` | 始终置位 |
| 6～15 | `RESERVED` | 始终为 0 |

### 12.3 平台错误映射

`platform` 必须至少区分：成功、传感器未就绪/NACK、超时、总线错误、参数错误。禁止只把所有 HAL 失败映射成布尔值；调试日志和 `last_error` 必须能区分触发失败、等待超时、帧错误、CRC 错误、转换错误和 ADC 错误。

## 13. 运行时间与版本

- `uptime_seconds` 从 `app_init()` 完成时开始累计。
- 使用相邻 `now_ms` 的无符号差值累积毫秒余数，不能直接使用 `HAL_GetTick() / 1000`，避免约 49.7 天时毫秒时基回绕导致运行时间倒退。
- `uptime_seconds` 为 `uint32_t`，自然回绕前不做饱和处理。
- Phase 1 固件版本常量为 major `0`、minor `1`；不得从构建时间或未版本化字符串动态推断。
- 后续寄存器层读取 32 位运行时间时必须生成快照；该寄存器行为不在本任务实现。

## 14. 参考接口

### 14.1 平台接口

```c
typedef enum {
    PLATFORM_OK = 0,
    PLATFORM_I2C_NOT_READY,
    PLATFORM_TIMEOUT,
    PLATFORM_BUS_ERROR,
    PLATFORM_INVALID_ARGUMENT,
    PLATFORM_IO_ERROR
} PlatformStatus;

PlatformStatus platform_dht_write(
    TemperatureChannel channel,
    uint8_t address_7bit,
    const uint8_t *data,
    size_t length,
    uint32_t timeout_ms);

PlatformStatus platform_dht_read(
    TemperatureChannel channel,
    uint8_t address_7bit,
    uint8_t *data,
    size_t length,
    uint32_t timeout_ms);

PlatformStatus platform_adc_start(void);
PlatformStatus platform_adc_read(uint16_t *raw, uint32_t timeout_ms);
PlatformStatus platform_debug_write(const uint8_t *data, size_t length);
uint32_t platform_millis(void);
```

### 14.2 纯 C 业务接口

```c
void device_model_init(DeviceModel *model);
bool device_model_set_threshold(
    DeviceModel *model,
    TemperatureChannel channel,
    int16_t threshold_deci_c);

uint8_t dhtc12_crc8(const uint8_t *data, size_t length);
AcquisitionError dhtc12_decode_frame(
    const uint8_t frame[6],
    int16_t *temperature_deci_c,
    uint16_t *humidity_raw);

void acquisition_init(AcquisitionContext *context, uint32_t now_ms);
void acquisition_service(
    AcquisitionContext *context,
    DeviceModel *model,
    uint32_t now_ms);

bool alarm_update(
    bool current,
    int16_t temperature_deci_c,
    int16_t threshold_deci_c);
```

所有指针参数必须校验。非法参数返回明确错误且不得修改输出对象。

## 15. 调试输出

Phase 1 通过 USART2/ST-LINK VCP 输出 ASCII 行。每个周期至少包含：

```text
seq=<周期序号> ms=<毫秒时刻> ch=<A|B|C|AMBIENT|LIGHT>
raw_t=<hex|NA> raw_h=<hex|NA> crc_t=<OK|FAIL|NA> crc_h=<OK|FAIL|NA>
value=<定点整数|NA> valid=<0|1> error=<结构化错误名>
crc_fail=<0..3> invalid=<0..3> alarm=0xNNNN status=0xNNNN
```

要求：

- 不使用 `%f`；
- 候选 DHTC12 工程值必须附 `UNVERIFIED`；
- 日志序号单调递增并允许自然回绕；
- 不记录 COM 号，COM 号属于 Host 枚举结果；
- `PHASE1_DEBUG_LOG` 仅用于 Phase 1。后续 USART2 发送 Modbus 二进制帧前，必须禁用该日志或迁移到独立发送路径，禁止 ASCII 与 RTU 共用未仲裁 UART。

## 16. 并发、资源与安全

- Phase 1 无中断回调共享业务模型；所有模型更新发生在主循环上下文。
- 如 HAL 中断只更新标志，标志必须使用适当的 `volatile`，业务状态转换仍在主循环完成。
- I2C1/2/3 的 HAL handle 只由 `platform` 映射，业务层不持有 handle。
- 固定长度数组和静态上下文优先；本阶段禁止动态内存分配。
- 日志格式化必须检查缓冲区截断。
- 传感器和光敏模块只使用 3.3 V 与公共 GND，禁止用 5 V 供电后把 AO 直连 MCU。

## 17. 测试规范

### 17.1 本机纯 C 测试

测试放在 `firmware/stm32/tests/`，使用本机 C 编译器、CMake 和 CTest；不链接 STM32 HAL，不修改 Host 生产代码，不引入第三方测试框架。每个失败断言必须返回非零退出码并输出用例名、期望值和实际值。

最低测试集合：

| 类别 | 必测行为 |
|---|---|
| CRC | 第 8.4 节四个向量、单字节损坏、温湿度各自 CRC 错误 |
| 帧解析 | 6 字节顺序、温度 CRC 错误、湿度 CRC 错误、空指针 |
| 温度转换 | GATE-01 关闭后测试第 8.5 节向量、舍入、上下边界和越界 |
| 调度 | 首次偏移、同通道命令间隔 >= 2000 ms、50 ms NACK 轮询、250 ms 超时、毫秒回绕 |
| 初始化恢复 | 三通道独立恢复、`0x30A2` 顺序、恢复/复位失败不进入测量、成功后等待 2000 ms |
| ADC | 0、4095、2048、混合平均、4096、批次中途失败、成功恢复 |
| 告警 | 阈值以下、等于阈值、迟滞区、等于恢复点、阈值 -400 边界、四通道独立性 |
| 故障 | 第 1～3 次 CRC 失败、混合错误打断 CRC 连续性、三周期无有效数据、一次成功恢复 |
| 模型 | 默认值、保留位、模拟环境位、阈值范围、无效采样保留最后值 |
| 运行时间 | 秒累积、余数、毫秒时基回绕 |
| Fake 集成 | 三个通道独立状态、一个通道故障不阻塞另外两个、日志所需字段完整 |

### 17.2 固件构建验证

1. 从干净的独立构建目录配置 ARM GCC 工程；
2. 构建 ELF 成功且无新增编译警告；
3. 确认 `App/` 源文件已进入产物；
4. 确认构建目录未进入 Git；
5. 重新生成 CubeMX 后重复构建，确认用户代码未丢失。

### 17.3 真实硬件验证

真实硬件验证必须记录：

- 开发板 MB1136 修订号、固件版本、构建标识、连接时间和实际 COM 号；
- 三只 DHTC12 的通道/I2C 映射、接线和上拉；
- 至少 10 个连续 2 s 采集周期，记录样本数、持续时间、成功数、失败数及错误分类；
- 每个温度通道的原始帧、两个 CRC、温度工程值和 GATE-01 结论；
- DHTC12 从命令到首次可读响应的观测范围，用于关闭 GATE-03；
- 光敏 AO 在 3.3 V 供电下的明暗方向、原始 ADC 平均值和毫伏值；
- 断开一个传感器后的第 1～3 个失败周期、故障置位和恢复后一次成功清除；
- 串口日志中的周期序号、错误、告警字和状态字。

未实际执行的项目必须写明“未在真实硬件环境验证”，不得填入推测值。

## 18. 验收追踪

| TASK-003 验收项 | 本规范覆盖 | 完成证据 |
|---:|---|---|
| 1 | 第 4、17.2 节 | 干净配置与构建记录、Git 状态 |
| 2 | 第 6、15、17.3 节 | 烧录记录与 VCP 日志 |
| 3 | 第 7、8、17.3 节 | 三通道原始帧和工程值 |
| 4 | 第 17.3 节 | 10 周期统计 |
| 5 | 第 3.2、3.3、8.5 节 | GATE-01 实物证据 |
| 6 | 第 9、17.3 节 | ADC 明暗记录 |
| 7 | 第 10、12.2 节 | 模型测试与状态字日志 |
| 8 | 第 11、17.1 节 | 迟滞边界测试 |
| 9 | 第 12、17.1、17.3 节 | 故障计数与恢复测试 |
| 10 | 第 4、14、17.1 节 | 无 HAL 的 CTest 结果 |
| 11 | 第 3.2、17.3 节 | 明确的硬件验证状态和证据 |

## 19. 实施顺序

1. 关闭 GATE-02，重新生成并复核 CubeMX 工程；
2. 建立纯 C 类型、设备模型、CRC、告警和本机测试；
3. 实现可注入平台接口的调度状态机与 Fake 测试；
4. 实现 STM32 `platform` 适配和 Phase 1 调试日志；
5. 关闭 GATE-04，完成初始化、总线恢复及隔离重连验证；
6. 构建、烧录，以原始帧关闭 GATE-01；
7. GATE-01 关闭后启用正式温度转换和对应测试；
8. 完成 10 周期与故障恢复硬件记录，关闭 GATE-03；
9. 同步硬件基线、TASK-003 测试结果和必要的缺陷记录；
10. 执行最终 Review 后才可将 TASK-003 标记完成。

## 20. Technical Spec 评审记录

### 20.1 评审结论

结论：**通过**。

本规范已经覆盖 TASK-003 要求的输入、输出、模块边界、状态、时间行为、错误、边界条件、恢复、日志、测试和验收追踪。GATE-01～04 均已由本机测试、ARM 构建和真实硬件记录关闭，允许正式发布 DHTC12 温度工程值。

### 20.2 必须修复

1. **DHTC12 有符号解释缺少实物证据。** 已于 2026-09-03 由空调 26 ℃现场参考和三路 25.9～26.2 ℃双 CRC 有效帧关闭。
2. **DHTC12 初始化流程遗漏。** 已加入有界总线恢复、`0x30A2` 软件复位和失败后重新初始化，并通过断线、重连及三路连续采集验证。
3. **ADC 配置与硬件基线不一致。** 该项已于 2026-09-02 修正为 84 cycles 并关闭 GATE-02。

### 20.3 建议修复

1. **B/C 路复接后的交叉验证。** 已于 2026-09-03 完成；三路连续 6 周期均在 50～53 ms 返回双 CRC 有效帧。
2. **后续单独评审阈值下界。** 当前 -40.0 ℃ 最小阈值与 5.0 ℃迟滞会形成不可达恢复点；如需避免锁存，应在 Modbus 公开写入范围确定前把最小阈值改为 -35.0 ℃，该变更不属于本规范自行决定范围。
3. **Phase 2 前关闭日志冲突。** USART2 承载 Modbus RTU 二进制帧时，必须禁用或迁移 Phase 1 ASCII 日志。

### 20.4 可选优化

- 实测噪声较大时，再基于记录评审 ADC 中值滤波、VDDA 校准或 DMA；本阶段不提前加入。
- 如后续需要湿度诊断，可把 `humidity_raw` 留在内部调试快照，但不得擅自增加公共寄存器。

### 20.5 评审范围限制

本次只评审 Technical Spec，未实现生产代码、未执行本机测试、未烧录固件，也未在真实硬件环境验证。
