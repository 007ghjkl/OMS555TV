# 系统架构设计草案

> 状态：评审草案 0.3，Host 后端与 RS485 物理接口已确认
> 日期：2026-09-04

## 1. 架构目标

系统需要把 DUT 数据采集、Modbus RTU、上位机监控和自动化测试组合为可验证闭环。核心约束是通信通道单一所有权、UI 非阻塞、协议逻辑与界面分离，以及测试证据可追溯。

## 2. 系统边界

```text
安全低压传感器/模拟输入
          │
          ▼
 STM32 DUT（采集、告警、寄存器、Modbus Slave）
          │ RS485 / Modbus RTU
          ▼
 Qt Host（通信、监控、测试、日志、报告）
          │
          ├── JSON 测试用例
          └── 本地日志与 HTML 报告
```

数据库、Web 后台、云服务、IEC 61850 和真实高压系统不在 MVP 边界内。

## 3. 固件模块

| 模块 | 职责 |
|---|---|
| `platform` | HAL 适配、时钟、GPIO、UART、ADC |
| `acquisition` | 原始值采集、缩放、范围约束和异常识别 |
| `device_model` | 温度、阈值、告警、状态、统计和版本信息 |
| `alarm` | 阈值与迟滞状态转换 |
| `register_map` | 业务模型与 0-based Modbus 地址映射 |
| `modbus_slave` | RTU 帧解析、CRC、0x03/0x06 和异常响应 |

硬件相关代码与可纯 C 测试的协议/业务逻辑应分离。

### 3.1 硬件实例

- 目标板为 NUCLEO-F411RE / STM32F411RET6，使用 HSI/PLL 100 MHz 和 HAL 裸机事件循环。
- A/B/C 相 DHTC12 分别使用 I2C1（PB8/PB9）、I2C2（PB10/PB3）和 I2C3（PA8/PC9）。
- 光敏模块 AO 使用 PA0/ADC1_IN0，3.3 V 供电；数据表示为毫伏，不表示校准照度。
- 环境温度当前为带状态位的软件模拟源，不能与真实传感器值混淆。
- 生产实测链路使用 USART1 PA9/PA10 和 MAX13487EESA 系列自动换向 TTL-RS485 模块，不使用 DE/RE GPIO；USART2/ST-LINK VCP 保留为编译期可选回归通道。单个 Firmware 镜像只启用一个 Modbus 端点。TASK-002/009/010 的真实 RS485 闭环与恢复验收已通过。

详细接线、电气约束与待验证项以 `docs/hardware_baseline.md` 为准。

## 4. 上位机模块

| 模块 | 职责 |
|---|---|
| `app` | 生命周期、依赖组装和全局状态机 |
| `communication` | 串口配置、请求队列、Modbus 后端、错误和原始帧 |
| `device` | 寄存器到领域数据的解析、缩放和写入校验 |
| `monitor` | 周期轮询、在线状态和通信统计 |
| `testing` | 用例加载、执行、断言、重试、中止和结果模型 |
| `logging` | 结构化日志、会话归档和 UI 日志模型 |
| `report` | HTML 报告模型与渲染 |
| `ui` | 展示和用户输入，不实现协议业务 |

`IModbusClient` 是监控与测试共同依赖的边界；Fake 实现用于无硬件单元测试。

## 5. 通信并发模型

已确认采用“QSerialPort 受控 RTU 后端 + 单通信工作线程 + 串行请求队列”：

1. 串口及通信状态机只在通信线程访问。
2. 应用状态机决定 `MONITORING` 或 `TESTING` 的请求来源。
3. 模式切换先停止新请求，再等待在途请求结束或受控取消。
4. 结果通过 queued signal 返回，不从工作线程操作 QWidget。
5. 每个请求分配 ID，并携带时间戳、超时、TX/RX、RTT 和结构化错误。

QSerialPort 后端负责完整 RTU ADU、CRC、分帧、超时和证据；`device` 模块继续负责寄存器领域解释。Qt SerialBus 仅保留技术探针，不作为生产后端，因为公开 API 不能提供包含 Slave 地址和 CRC 的完整 RTU TX/RX 证据。

备选的 Qt SerialBus 主后端和 UI 线程纯事件驱动方案均不采用。完整决策、风险与回退条件见 `docs/decisions/2026-09-03-Host-Modbus后端.md`。

## 6. 应用状态机

```text
DISCONNECTED → CONNECTED_IDLE → MONITORING
                      │              │
                      └──────────────┘
                      │
                      ▼
                   TESTING → STOPPING → CONNECTED_IDLE

任意活动状态发生不可恢复错误 → ERROR → DISCONNECTED/CONNECTED_IDLE
```

状态迁移必须集中管理；未连接禁止监控和测试，测试与监控互斥。

## 7. 数据与错误模型

- 传输数据以 `quint16` 寄存器为边界，温度使用有符号定点数 ×0.1℃。
- 32 位数据采用低字在低地址、高字在高地址；单寄存器仍按 Modbus 规范高字节先传输。
- 光敏模拟电压使用 `uint16` 毫伏值，范围 0～3300。
- 错误分类至少包括串口、超时、CRC、协议异常、参数、断言、配置和报告错误。
- 底层返回错误代码、上下文和原始证据；UI 决定展示方式。

## 8. 测试架构

`TestCaseLoader` 负责 JSON 模式校验，`TestEngine` 只执行规范化用例，处理器按测试类型扩展。`TestResultManager` 保存不可变执行结果，`ReportGenerator` 只消费结果模型，不重新解释通信数据。

## 9. 部署与构建

- Host：Windows x64，Qt 6.8.3、MSVC 2022、CMake、Ninja。
- Firmware：NUCLEO-F411RE、STM32CubeMX 6.18.1-RC2、STM32CubeF4 v1.28.3、HAL；ARM 编译/调试工具实际路径须在 TASK-001 验证。
- 两端独立构建，仓库根目录提供统一说明，不混用构建目录。

## 10. 安全、可观测性与恢复

- 所有硬件操作限制在安全低压环境。
- 配置写入执行范围校验与回读验证。
- 日志避免记录密钥或无关敏感信息。
- 通信断开需进入明确离线状态；自动重连不是 MVP 强制项，但恢复测试必须可判定。

## 11. 已确认的架构决策

1. Host Modbus 生产后端采用 QSerialPort 受控 RTU；标准业务和未来经批准的 Raw/错误注入必须复用同一工作线程、串行队列、串口和证据模型。确认日期：2026-09-03。
2. Firmware Modbus 物理端点采用编译期互斥选择；真实 RS485 使用 USART1 PA9/PA10 和模块自动换向，不使用 PC8/DE/RE。确认日期：2026-09-04。

## 12. 待确认的架构决策

1. 是否只支持 Windows，Linux 仅保留可移植边界。
