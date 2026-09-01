# Phase 0 开发环境与技术选型调研

> 状态：Phase 0 构建验证完成；硬件功能待验证
> 日期：2026-09-01

## 1. 已验证的本机环境

通过本机命令和目录检查确认：

| 工具 | 当前状态 |
|---|---|
| Git | 2.55.0.windows.5 |
| CMake | 4.4.3 |
| Ninja | 1.13.2 |
| Qt | 6.8.3 MSVC 2022 x64 已安装 |
| Qt 兼容环境 | 5.15.2 MSVC 2019 x64 已安装 |
| Qt Creator | 已安装 |
| MSVC | Visual Studio 2026 Community，MSVC 19.51，x64 构建已验证 |
| ARM GCC | STM32Cube bundle 14.3.1，固件构建已验证 |
| ARM GDB | STM32Cube bundle 15.2.90，版本已确认 |
| STM32CubeProgrammer | 2.23.0，已只读枚举 NUCLEO-F411RE |

Qt Host 与 STM32 固件均已通过本项目工程完成配置、编译和链接验证。ST-LINK 只完成了探针枚举，未进行擦除、烧录、调试或固件运行验证。

## 2. Qt 方案比较

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| Qt 6.8.3 + MSVC 2022 | 当前版本、长期维护方向、与本机新工具链匹配 | 若需要旧系统兼容，部署成本可能更高 | 推荐作为唯一开发基线 |
| Qt 5.15.2 + MSVC 2019 | 兼容成熟旧项目 | 新项目生命周期较短，后续迁移成本 | 仅在明确兼容约束时采用 |

采用 Qt 6.8.3、C++17、CMake 和 Ninja。Host 已实际链接 Core、Widgets、Test 和 SerialBus，并通过 3 个 CTest 测试；SerialPort 组件在配置阶段已确认可发现。

## 3. 通信实现方案比较

### 方案 A：Qt SerialBus 的 Modbus RTU Client

- 优点：标准 API、开发量较小、容易快速形成监控链路。
- 缺点：原始帧、CRC 故障注入和特殊协议测试的控制能力受限。

### 方案 B：基于 QSerialPort 自建 Modbus RTU Master

- 优点：完全控制 TX/RX、时序、CRC 和故障注入，适合协议验证展示。
- 缺点：协议状态机、帧间隔和异常处理实现成本与缺陷风险更高。

### 验证结论

Qt SerialBus 的公开 API 可以发送和接收原始 Modbus PDU，但不公开完整 RTU ADU 字节，也不能构造错误 CRC 后按原样发送。Phase 0 已保留 `IModbusClient` 抽象；后续如验收要求完整 TX/RX 证据和 CRC 故障注入，应使用基于 QSerialPort 的受控后端或传输层旁路采集。详细证据见 `research/qt_serialbus_raw_frame.md`。

## 4. 线程模型方案比较

### 方案 A：单通信工作线程 + 串行请求队列

- 串口对象和请求状态机归属同一工作线程。
- 监控和 TestEngine 通过统一调度器提交请求。
- 易于保证请求串行化和 UI 响应性。

### 方案 B：UI 线程中的纯事件驱动通信

- 对简单轮询代码量较少。
- 长时测试、取消、模式切换和复杂重试会增加 UI 层状态管理压力。

推荐方案 A，但正式接口须在架构评审后确认。

## 5. STM32 方案

需求允许裸机事件循环和 HAL，适合优先控制工程复杂度。TASK-000 已确认 NUCLEO-F411RE、STM32F411RET6、三路 DHTC12 I2C、PA0/ADC1 光敏输入和 USART2 VCP，可生成唯一的最小 CubeMX 工程。

本机已确认 STM32CubeMX 6.18.1-RC2、STM32CubeF4 v1.28.3 和 STM32Cube bundles。ARM GCC、GDB Server 与 Programmer CLI 不加入系统 `PATH`，通过明确 bundle 路径在项目构建命令中使用。固件已使用 ARM GCC 14.3.1 和 Ninja 1.13.2 完成实际构建。

当前使用 USART2/ST-LINK VCP 验证 Modbus RTU 协议。RS485 模块尚未采购，其电气层迁移由 TASK-002 管理，不阻塞最小工程和传感器采集开发。

## 6. Phase 0 退出条件

1. Qt 最小工程可配置、构建并运行测试。
2. STM32 硬件基线已记录，最小工程可独立构建；构建工具路径有真实验证记录。
3. 通信后端技术验证完成并记录结论。
4. 仓库目录、日志接口骨架和自动化测试入口建立。
5. 构建方法和未验证项在 README 中同步。

以上退出条件已于 2026-09-01 满足。硬件功能验证不属于 Phase 0 构建结论。
