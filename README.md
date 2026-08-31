# OMS555TV 电力监测终端自动化测试验证平台

本项目面向嵌入式产品测试验证场景，计划实现 STM32 被测设备（DUT）与 C++/Qt 上位机，通过 RS485 / Modbus RTU 完成实时监控、参数配置、通信调试、自动化与半自动测试，以及 HTML 测试报告生成。

当前状态：开发前准备阶段。需求基线已建立，架构和 Phase 0 技术规范为草案；尚未开始生产代码，也未在真实硬件环境验证。

## 项目目标

- STM32 采集或模拟 4 路温度数据并提供告警与设备状态。
- STM32 作为 Modbus RTU Slave，上位机作为 Master。
- Qt 上位机统一管理监控与测试模式，避免串口并发访问。
- 通过 JSON 配置测试用例，记录原始报文、耗时和结果。
- 支持功能、协议、边界、异常恢复、稳定性和半自动测试。
- 输出可追溯的日志与正式 HTML 测试报告。

## 文档导航

- [项目需求基线](PROJECT_SPEC.md)
- [产品需求文档](docs/prd.md)
- [架构设计草案](docs/architecture.md)
- [Modbus 寄存器表草案](docs/modbus_register_map.md)
- [测试计划](docs/test_plan.md)
- [测试用例目录](docs/test_cases.md)
- [Phase 0 调研记录](research/phase0_development_environment.md)
- [Phase 0 任务](tasks/TASK-001-phase0-project-skeleton.md)
- [Phase 0 技术规范](specs/phase0_project_skeleton.md)

## 计划中的仓库结构

```text
firmware/stm32/   STM32 固件
host/qt/          C++ / Qt 上位机
testcases/        JSON 测试用例
docs/             产品、架构、协议与测试文档
research/         调研记录
specs/            技术规范
tasks/            可独立验收的任务
output/           本地日志与报告（内容不提交）
```

## 当前开发基线

- Windows 10/11
- C++17
- Qt 6.8.3 + MSVC 2022 x64（推荐，待 Phase 0 构建验证）
- CMake + Ninja
- STM32CubeMX / STM32CubeIDE + HAL（具体芯片和工程参数待硬件确认）

## 开始编码前的门禁

以下信息确认前，不创建绑定具体芯片的 STM32 工程：

1. STM32 开发板与 MCU 精确型号；
2. 可用 UART、RS485 收发使能引脚及电气连接；
3. ADC/传感器类型与引脚；
4. 调试器、CubeIDE/CubeMX 版本和固件包；
5. 默认通信参数最终值。

详细未决项见架构与 Phase 0 任务文档。

## 安全说明

本项目仅用于安全低压环境下的教学与测试验证，不接入真实高压、电流或市电设备，不实现工业保护功能。
