# OMS555TV 电力监测终端自动化测试验证平台

本项目面向嵌入式产品测试验证场景，计划实现 STM32 被测设备（DUT）与 C++/Qt 上位机，通过 RS485 / Modbus RTU 完成实时监控、参数配置、通信调试、自动化与半自动测试，以及 HTML 测试报告生成。

当前状态：`TASK-001` Phase 0 工程骨架、`TASK-003` Firmware Phase 1 基础采集与设备模型、`TASK-004` Host 设备模型与寄存器编解码均已完成。Qt Host 与 STM32 固件均已在本机实际构建；STM32 固件已烧录并完成三路 DHTC12、光敏明暗方向及传感器断线恢复验证。

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
- [硬件与通信基线](docs/hardware_baseline.md)
- [Modbus 寄存器表草案](docs/modbus_register_map.md)
- [测试计划](docs/test_plan.md)
- [测试用例目录](docs/test_cases.md)
- [Phase 0 调研记录](research/phase0_development_environment.md)
- [Qt SerialBus 原始帧能力验证](research/qt_serialbus_raw_frame.md)
- [Phase 0 任务](tasks/TASK-001-phase0-project-skeleton.md)
- [Phase 0 技术规范](specs/phase0_project_skeleton.md)
- [TASK-003 Firmware Phase 1 基础采集与设备模型](tasks/TASK-003-firmware-phase1-basic-acquisition.md)
- [Firmware Phase 1 基础采集技术规范](specs/firmware_phase1_acquisition.md)
- [Host 设备模型与寄存器编解码任务](tasks/TASK-004-host-device-model-register-codec.md)
- [Host 设备模型与寄存器编解码技术规范](specs/host_device_model_register_codec.md)

## 仓库结构

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
- NUCLEO-F411RE / STM32F411RET6
- STM32CubeMX 6.18.1-RC2 + STM32CubeF4 v1.28.3 + HAL
- 三只 DHTC12（I2C1/2/3）与一路 PA0/ADC1 光敏模拟量
- 当前通过 ST-LINK VCP/USART2、115200 8N1 开发 Modbus RTU；真实 RS485 待硬件迁移任务

## 构建 Host

在仓库根目录打开 PowerShell 7：

```powershell
$projectRoot = (Get-Location).Path
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
& cmd.exe /d /s /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && cmake -S $projectRoot\host\qt -B $projectRoot\build-host -G Ninja -DCMAKE_PREFIX_PATH=D:\Dev\Qt\6.8.3\msvc2022_64 -DCMAKE_BUILD_TYPE=Debug"
& cmd.exe /d /s /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && cmake --build $projectRoot\build-host --parallel"
ctest --test-dir "$projectRoot\build-host" --output-on-failure
```

当前机器实际使用 Visual Studio 2026 Community 的 MSVC 19.51 编译器；它已通过 Qt `msvc2022_64` 包的配置、链接和运行测试。应用启动烟雾测试使用 Qt 的 offscreen 平台，不访问串口或硬件。

## 构建 STM32 固件

固件由 STM32CubeMX 6.18.1-RC2 生成，构建工具来自 STM32Cube bundles。无需修改系统 `PATH`：

```powershell
$projectRoot = (Get-Location).Path
$bundleRoot = 'C:\Users\rainbow\AppData\Local\stm32cube\bundles'
$env:Path = "$bundleRoot\gnu-tools-for-stm32\14.3.1+st.2\bin;$bundleRoot\ninja\1.13.2+st.1\bin;$env:Path"
& "$bundleRoot\cmake\4.3.1+st.1\bin\cmake.exe" -S "$projectRoot\firmware\stm32" -B "$projectRoot\build-firmware" -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$projectRoot\firmware\stm32\cmake\gcc-arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Debug
& "$bundleRoot\cmake\4.3.1+st.1\bin\cmake.exe" --build "$projectRoot\build-firmware" --parallel
```

2026-09-03 的 TASK-003 最终结果：`oms555tv_firmware.elf` 链接成功，Flash 使用 27,260 B，RAM 使用 2,624 B；22 个本机测试通过，固件已通过 ST-LINK 烧录、校验并运行。GATE-01～04 均已关闭，三路 DHTC12 正式固件各完成 12 个连续有效周期，温度为 25.9～26.2 ℃，36 帧双 CRC 全部通过；光敏 AO 已确认由强光 130～132 mV 上升至完全遮光 2883～2898 mV；B 路断线三周期置位与运行中重连恢复均已验证。实物板标识为 `MB1136-F411RE-C04`、贴纸编号 `A232203276`，TASK-003 已完成。

## 当前开发门禁

硬件与引脚基线以及 TASK-003 功能验证已经完成。以下事项仍必须在对应阶段完成：

1. 采购 TTL-RS485 模块后完成 `TASK-002`，再验收 RS485 电气层；
2. Phase 2 启用 Modbus RTU 二进制帧前，关闭或迁移 USART2 ASCII 调试日志；
3. 所有未执行的硬件测试继续标记“未在真实硬件环境验证”。

详细未决项见架构与 Phase 0 任务文档。

## 安全说明

本项目仅用于安全低压环境下的教学与测试验证，不接入真实高压、电流或市电设备，不实现工业保护功能。
