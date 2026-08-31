# 硬件与通信基线

> 状态：基线 1.0，TASK-000 评审结论
>
> 日期：2026-08-31
>
> 验证范围：已核对用户提供的器件资料与本机安装目录；尚未完成接线、固件构建或真实硬件运行验证。

## 1. 基线结论

当前硬件足以生成唯一的 STM32CubeMX 工程并开展 Phase 0/Phase 1：使用 NUCLEO-F411RE、STM32F411RET6、板载 ST-LINK/V2-1、三路独立 I2C 的 DHTC12，以及一路 PA0/ADC1 光敏模拟量。

当前没有 TTL-RS485 模块。开发阶段先通过板载 ST-LINK 虚拟串口（USART2）传输 Modbus RTU 帧；RS485 物理层迁移单独由 `TASK-002` 管理，不把尚未购买的收发器型号或接线写成已确认事实。

## 2. 硬件清单

| 类别 | 基线 |
|---|---|
| 开发板 | NUCLEO-F411RE，参考板 MB1136 |
| MCU | STM32F411RET6，LQFP64，512 KiB Flash，128 KiB SRAM |
| 调试器 | 板载 ST-LINK/V2-1，SWD 调试与虚拟串口 |
| 温湿度传感器 | DHTC12 ×3，固定 7 位 I2C 地址 `0x44` |
| 模拟传感器 | 4 线制光敏电阻模块，使用 AO，DO 暂不连接 |
| 供电 | NUCLEO 由 ST-LINK USB 供电；所有外接模块使用板载 3.3 V 与公共 GND |
| RS485 | 当前无实物；后续选择 3.3 V 逻辑兼容的半双工模块 |

必须在首次接线前记录开发板底部 MB1136 的 `C-xx` 修订号。该信息不阻塞当前工程，因为时钟基线不依赖板载 HSE 焊桥状态。

## 3. 开发工具基线

| 项目 | 已确认值 |
|---|---|
| STM32CubeMX | 6.18.1-RC2，`D:\Dev\STM32CubeMX` |
| STM32CubeF4 | v1.28.3，`C:\Users\rainbow\STM32Cube\Repository\STM32Cube_FW_F4_V1.28.3` |
| VS Code STM32 扩展 | `stmicroelectronics.stm32-vscode-extension-3.10.0` 及配套 1.4.0 组件 |
| 工程模型 | STM32CubeMX + HAL，裸机事件循环，第一阶段不使用 FreeRTOS |

`arm-none-eabi-gcc`、ST-LINK GDB Server 和 STM32CubeProgrammer CLI 当前不在系统 `PATH`。这不阻塞 CubeMX 工程生成，但 TASK-001 在声称“固件可构建”前必须通过 VS Code Bundle Manager 或明确安装路径确认实际编译/调试工具链。

## 4. 时钟与基础工程配置

为消除 MB1136 C-01/C-02 等修订版对 ST-LINK MCO/HSE 默认焊桥的差异，基线采用内部 HSI：

| 配置 | 值 |
|---|---|
| 时钟源 | HSI 16 MHz |
| PLL | M=16，N=200，P=2，Q=4；SYSCLK=100 MHz |
| AHB | 100 MHz |
| APB1 | 50 MHz |
| APB2 | 100 MHz |
| HSE/LSE | 不使用 |
| 调试接口 | Serial Wire，保留 PA13/PA14 |
| 时基 | SysTick 1 ms |

PLLQ 对应的 48 MHz 域在当前阶段不使用，由 CubeMX 按未启用 USB/SDIO 的工程约束生成。电压缩放和 Flash 等待周期由 CubeMX 根据 100 MHz 配置生成，不在用户代码中重复硬编码。

## 5. 引脚分配

### 5.1 已确认引脚

| 功能 | STM32 引脚 | 外部连接 | CubeMX 模式 | 参数 |
|---|---|---|---|---|
| USART2_TX | PA2 | ST-LINK VCP，默认 SB13/SB14 ON | USART2 Asynchronous | 115200 8N1 |
| USART2_RX | PA3 | ST-LINK VCP，默认 SB13/SB14 ON | USART2 Asynchronous | 无流控 |
| A 相 DHTC12 SCK | PB8 | Arduino D15 / CN10-3 | I2C1_SCL AF4 | 100 kHz |
| A 相 DHTC12 SDA | PB9 | Arduino D14 / CN10-5 | I2C1_SDA AF4 | 100 kHz |
| B 相 DHTC12 SCK | PB10 | Arduino D6 / CN10-25 | I2C2_SCL AF4 | 100 kHz |
| B 相 DHTC12 SDA | PB3 | Arduino D3 / CN10-31 | I2C2_SDA AF9 | 100 kHz |
| C 相 DHTC12 SCK | PA8 | Arduino D7 / CN10-23 | I2C3_SCL AF4 | 100 kHz |
| C 相 DHTC12 SDA | PC9 | ST morpho CN10-1 | I2C3_SDA AF4 | 100 kHz |
| 光敏 AO | PA0 | Arduino A0 / CN8-1 | ADC1_IN0 | 12 bit |

三只 DHTC12 地址均固定为 `0x44`，因此不能并联在同一 I2C 总线上。上述分配使每只传感器独占一个硬件 I2C 控制器。

### 5.2 预留的 RS485 迁移资源

建议后续优先评估 USART1 的 PA9/PA10，并预留 PC8 作为 DE/RE 控制，使 USART2 继续承担调试日志和 VCP。该方案在 TTL-RS485 模块型号与接线评审前仅是候选，不属于当前已实现基线。

## 6. 传感器电气与采集约束

### 6.1 DHTC12

- VDD 接 3.3 V，GND 接公共地。
- 每只传感器的 SDA 和 SCK 分别使用 4.7 kΩ 上拉到 3.3 V。
- 每只传感器 VDD/GND 近端放置 100 nF 去耦电容。
- I2C 时钟为 100 kHz；7 位地址为 `0x44`。
- 使用 `0x2C10` 同时测量温湿度；每个 16 位结果后校验 CRC-8。
- CRC-8 多项式为 `0x31`，初值为 `0xFF`，不反转。
- 产品手册给出的采样周期为 2 s，固件对同一只传感器的主动测量间隔不得小于 2 s。
- 温度工作范围为 -40.0～80.0 ℃，寄存器工程值使用 `int16`、缩放 ×0.1 ℃。

手册给出的温度换算式为 `T = 40 + St / 256`，但没有明确说明 `St` 的有符号性。Phase 1 必须用室温实测原始数据确认符号解释后再固化转换单元测试；确认前不得声称测量值已经校准。湿度数据可用于传感器诊断，暂不进入 MVP 公共寄存器表。

### 6.2 光敏电阻模块

- VCC 接 3.3 V，GND 接公共地，AO 接 PA0；DO 悬空不使用。
- 模块原理图显示 AO 来自 10 kΩ 与光敏电阻的分压节点。3.3 V 供电可保证 ADC 输入不超过 VDDA；禁止在 AO 直连 MCU 时使用 5 V 供电。
- 光照增强时光敏电阻阻值下降，AO 电压预计下降；该方向需在首次实测时验证。
- ADC1 使用 12 位单次软件触发，基线采样时间 84 cycles；应用层使用多次采样平均抑制噪声。
- 对外工程值为 0～3300 mV，理想换算为 `raw * 3300 / 4095`。该结果是模拟电压，不宣称为校准照度。

### 6.3 环境温度

三只真实 DHTC12 分别用于 A/B/C 相温度。原需求中的环境温度在当前硬件阶段使用明确标记的软件模拟值，以保持四路温度数据链路和测试能力；不得把光敏电压伪装成环境温度。后续若增加第四只同地址传感器，需要新增 I2C 复用器、软件 I2C 或更换可寻址传感器，并单独评审。

## 7. 通信基线

| 参数 | 值 |
|---|---|
| 当前物理通道 | ST-LINK USB Virtual COM Port + USART2 TTL |
| 上层帧格式 | Modbus RTU |
| Slave ID | 1 |
| Baud rate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| 响应超时初始值 | 500 ms，由 Host 配置 |

当前链路可验证 Modbus 帧格式、CRC、寄存器映射和测试引擎，但不能代表 RS485 电气层已经验证。所有报告必须区分“VCP/UART 协议验证”和“RS485 实物验证”。

## 8. 阈值、迟滞与故障策略

| 项目 | 默认值 | 有效范围 |
|---|---:|---:|
| A/B/C 相高温阈值 | 60.0 ℃ | -40.0～80.0 ℃ |
| 环境高温阈值 | 40.0 ℃ | -40.0～80.0 ℃ |
| 温度告警迟滞 | 5.0 ℃ | 固件常量，MVP 不开放写入 |
| 光敏模拟电压 | 无告警阈值 | 0～3300 mV |

高温告警在 `temperature >= threshold` 时置位，在 `temperature <= threshold - 5.0 ℃` 时清除。传感器 CRC 连续 3 次失败或连续 3 个采样周期无有效数据时置相应故障位；单次成功采样后清除故障计数并恢复有效状态。具体时间行为在 Phase 1 Spec 中固化并测试。

## 9. 已确认的数据表示

- 温度：`int16`，单位 0.1 ℃。
- 光敏模拟电压：`uint16`，单位 mV。
- 运行时间：`uint32`，单位秒；低字放在低地址，高字放在高地址。
- Modbus 单寄存器字节序遵循协议，高字节先发送。
- 通信错误计数：`uint16` 饱和计数，不回绕。
- 设备状态：位定义见 `docs/modbus_register_map.md`。

## 10. 资料来源与核验记录

| 资料 | 使用内容 |
|---|---|
| `hardware_info/MCU/DS10314.pdf`，DS10314 Rev 8 | STM32F411RET6 外设、引脚和复用功能 |
| `hardware_info/开发板/um1724-...pdf`，UM1724 Rev 17 | NUCLEO-F411RE、VCP、时钟修订差异和连接器引脚 |
| `hardware_info/开发板/NUCLEO板原理图.pdf` | ST-LINK、USART 与扩展连接器连接 |
| `hardware_info/温湿度传感器/DHTC12.pdf` | 供电、I2C 地址、时序、命令、CRC 与测量范围 |
| `hardware_info/光敏传感器/光敏电阻传感器模块使用说明书4线制.pdf` | 3.3～5 V 供电、AO/DO 定义和端子顺序 |
| `hardware_info/光敏传感器/光敏电阻传感器模块电路图.pdf` | AO 分压与 LM393 DO 电路 |

## 11. 尚未进行的验证

- 未记录实物 MB1136 `C-xx` 修订号。
- 未核对 ST-LINK 当前固件版本和 Windows COM 端口号。
- 未进行三路 I2C 接线、上拉阻值实测或地址应答测试。
- 未确认 DHTC12 温度原始值的有符号解释。
- 未测量 ADC 零点、满量程或光照-电压关系。
- 未在真实硬件环境构建、烧录或运行固件。
- 未采购或验证 RS485 收发器、终端电阻、偏置和 DE/RE 时序。
