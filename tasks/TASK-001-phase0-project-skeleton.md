# TASK-001 建立 Phase 0 工程骨架

## 目标

建立两端可独立构建、可测试的最小工程，为后续阶段提供稳定入口。

## 背景

需求规定必须分阶段推进，Phase 0 只验证项目结构、工具链、基础 CMake 和日志，不实现完整业务。

## 范围

- 建立 `host/qt` 的 Qt 6/CMake 最小应用与测试目标。
- 建立 Host 模块目录和最小日志接口。
- 在硬件基线确认后建立 `firmware/stm32` 最小可构建工程。
- 建立测试用例与运行输出目录。
- 补充可复现构建命令和环境说明。
- 对 Qt SerialBus 原始报文能力做最小技术验证并记录结论。

## 非范围

- 不实现完整 Modbus Master/Slave。
- 不实现监控 UI、TestEngine 或报告生成。
- 不引入数据库、Web、QML、FreeRTOS 或大型第三方依赖。

## 依赖

- Qt Host 部分无硬件依赖。
- STM32 硬件基线依赖 `TASK-000`，现已满足。
- STM32 构建依赖 ARM GCC、ST-LINK GDB Server 与 Programmer CLI 的实际路径确认；这些工具当前不在系统 `PATH`。
- 真实 RS485 不属于 Phase 0，`TASK-002` 不阻塞本任务。

## 验收标准

1. Host 可使用记录的命令完成配置和构建。
2. Host 至少有一个基础测试可被 CTest 发现并通过。
3. 固件最小工程按硬件基线生成并可独立构建；若工具链缺失，不能将固件子任务标记完成。
4. 构建产物不进入 Git。
5. README 与实际命令一致。
6. 不声称任何未执行的硬件验证结果。

## 测试要求

- 执行 Host 配置、构建和 CTest。
- 确认应用可启动且主线程不含阻塞任务。
- 在硬件工程建立后执行一次干净构建。

## 当前状态

已完成（2026-09-01）。

验证记录：

- Host 使用 Qt 6.8.3、MSVC 19.51、CMake 和 Ninja 配置及构建成功；
- CTest 发现并通过 3 个测试：日志模型、Qt SerialBus 原始 PDU 技术验证、应用 offscreen 启动烟雾测试；
- STM32CubeMX 按 NUCLEO-F411RE、HSI/PLL 100 MHz、ADC1、I2C1/2/3 和 USART2 基线生成工程；
- 固件使用 ARM GCC 14.3.1 构建成功，Flash 使用 10,376 B，RAM 使用 1,976 B；
- STM32CubeProgrammer 2.23.0 已只读枚举 NUCLEO-F411RE 和 ST-LINK V2J38M27；
- 未执行烧录、固件运行、传感器接线或通信功能测试，相关结论仍标记为未在真实硬件环境验证。
