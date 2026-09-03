# OMS555TV 电力监测终端自动化测试验证平台

本项目面向嵌入式产品测试验证场景，计划实现 STM32 被测设备（DUT）与 C++/Qt 上位机，通过 RS485 / Modbus RTU 完成实时监控、参数配置、通信调试、自动化与半自动测试，以及 HTML 测试报告生成。

当前状态：`TASK-001`、`TASK-003`～`TASK-007` 均已完成。Firmware Phase 2 Modbus RTU Slave 已通过 VCP/UART 实机验证；Host 已实现无硬件 Modbus RTU 核心、完整异步通信契约和确定性 Fake，但真实 QSerialPort 工作线程尚未实现。下一步计划为 `TASK-008` QSerialPort 后端与 VCP 联调；`TASK-002` 真实 RS485 硬件迁移继续延期。

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
- [TASK-005 Firmware Phase 2 Modbus RTU Slave](tasks/TASK-005-firmware-phase2-modbus-rtu-slave.md)
- [Firmware Phase 2 Modbus RTU Slave 技术规范](specs/firmware_phase2_modbus_rtu_slave.md)
- [TASK-005 VCP/UART 验证记录](docs/test_results/task005_vcp_uart_modbus_validation.md)
- [TASK-006 Host Modbus 后端决策与异步通信规范](tasks/TASK-006-host-phase3-modbus-backend-decision.md)
- [Host Modbus 后端 ADR](docs/decisions/2026-09-03-Host-Modbus后端.md)
- [Host Phase 3 Modbus 异步通信技术规范](specs/host_phase3_modbus_communication.md)
- [TASK-007 Host Modbus RTU 核心、通信契约与 Fake](tasks/TASK-007-host-phase3-modbus-core-and-fake.md)
- [TASK-008 Host QSerialPort 异步后端与 VCP 联调](tasks/TASK-008-host-phase3-qserialport-backend-vcp-integration.md)

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
- Qt 6.8.3 + MSVC 2022 x64（已完成本机构建验证）
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

2026-09-03 的 TASK-005 最终结果：35/35 纯 C 测试和 ARM 干净构建通过，Flash 使用 29,124 B，RAM 使用 3,272 B；固件已通过 VCP/UART 完成 0x03、0x06、0x01/0x02/0x03、错误 CRC、短帧和阈值恢复验证。连续 500 次 0x03 请求为 500/500 成功，响应时间最小/平均/最大为 6.045/7.868/9.708 ms。该结果仅证明 USART2/ST-LINK VCP 协议行为，不代表 RS485 电气层已经验证。

2026-09-03 的 TASK-007 最终结果：Host 全新配置、构建和 8/8 CTest 通过。新增测试覆盖通信模型、CRC16、0x03/0x06 完整 RTU ADU、正常与异常响应、错误判定优先级、任意分片、确定性 Fake、FIFO、QueueFull、单在途、取消、关闭、所有权交接和恰好一次终态。该结果仅为无串口、无硬件验证，不代表 QSerialPort、VCP/UART 或 RS485 已通过。

## 当前开发门禁

硬件与引脚基线、Firmware Phase 1/2 功能验证以及 Host Modbus 后端决策已经完成。以下事项仍必须在对应阶段完成：

1. 采购 TTL-RS485 模块后完成 `TASK-002`，再验收 RS485 电气层；
2. `TASK-007` 的无硬件通信核心与 Fake 已完成；由 `TASK-008` 实现 QSerialPort 后端并执行 Host VCP 联调；
3. Host Phase 4 监控、TestEngine 和报告必须等待 Phase 3 通信验收，不得提前把技术探针视为生产通信能力；
4. 所有未执行的 RS485 和系统级硬件测试继续标记“未在真实 RS485 环境验证”。

详细未决项见架构与 Phase 0 任务文档。

## 安全说明

本项目仅用于安全低压环境下的教学与测试验证，不接入真实高压、电流或市电设备，不实现工业保护功能。
