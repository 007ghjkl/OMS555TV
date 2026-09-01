# OMS555TV 电力监测终端自动化测试验证平台

本项目面向嵌入式产品测试验证场景，计划实现 STM32 被测设备（DUT）与 C++/Qt 上位机，通过 RS485 / Modbus RTU 完成实时监控、参数配置、通信调试、自动化与半自动测试，以及 HTML 测试报告生成。

当前状态：`TASK-001` Phase 0 工程骨架已完成。Qt Host 与 STM32 固件均已在本机实际构建；Host 的 3 个 CTest 测试已通过。尚未烧录固件，也未进行传感器或通信功能的真实硬件验证。

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

2026-09-01 的实际构建结果：`oms555tv_firmware.elf` 链接成功，Flash 使用 10,376 B，RAM 使用 1,976 B。该结果只证明代码可交叉编译和链接，不代表固件已烧录或硬件功能已通过。

## 当前开发门禁

硬件与引脚基线已经确认，可以开始 TASK-001 的最小 STM32CubeMX 工程。以下事项仍必须在对应阶段完成：

1. 首次传感器接线前记录 MB1136 板修订号；
2. 用实物确认 DHTC12 温度原始值的有符号解释；
3. 采购 TTL-RS485 模块后完成 `TASK-002`，再验收 RS485 电气层；
4. 所有未执行的硬件测试继续标记“未在真实硬件环境验证”。

详细未决项见架构与 Phase 0 任务文档。

## 安全说明

本项目仅用于安全低压环境下的教学与测试验证，不接入真实高压、电流或市电设备，不实现工业保护功能。
