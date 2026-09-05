# OMS555TV 电力监测终端自动化测试验证平台

本项目面向嵌入式产品测试验证场景，计划实现 STM32 被测设备（DUT）与 C++/Qt 上位机，通过 RS485 / Modbus RTU 完成实时监控、参数配置、通信调试、自动化与半自动测试，以及 HTML 测试报告生成。

当前状态：`TASK-001`～`TASK-016`（除编号未使用项外）对应的现有任务均已完成。Firmware Slave 与 Host Phase 3 生产后端已分别通过 ST-LINK VCP/UART 和 USART1/真实 RS485 闭环；Host Phase 4 的集中状态、监控核心、实时 UI、参数配置、通信诊断与会话日志已完成。Host Phase 5 已完成 v1 测试输入契约、TestEngine、自动化测试 UI，以及 8 条真实 RS485 基础套件验收。Host Phase 6 已制定 `TASK-017`～`TASK-020`，当前仅完成任务规划，尚未派发或实施。

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
- [TASK-008 Host VCP/UART 联调记录](docs/test_results/task008_host_vcp_modbus_integration.md)
- [TASK-002 RS485 硬件基线与迁移验收总任务](tasks/TASK-002-rs485-hardware-migration.md)
- [RS485 硬件接口技术规范](specs/rs485_hardware_interface.md)
- [TASK-009 Firmware USART1/RS485 传输迁移](tasks/TASK-009-firmware-usart1-rs485-transport-migration.md)
- [Firmware USART1/RS485 传输迁移技术规范](specs/firmware_rs485_transport_migration.md)
- [TASK-009 Firmware RS485 验证记录](docs/test_results/task009_firmware_rs485_transport_validation.md)
- [TASK-010 Host 真实 RS485 系统联调与迁移验收](tasks/TASK-010-host-rs485-system-integration.md)
- [TASK-010 Host RS485 联调记录](docs/test_results/task010_host_rs485_system_integration.md)
- [TASK-011 Host Phase 4 应用状态与监控核心](tasks/TASK-011-host-phase4-monitoring-core.md)
- [Host Phase 4 应用状态与监控核心技术规范](specs/host_phase4_monitoring_core.md)
- [TASK-011 Host 监控核心验证记录](docs/test_results/task011_host_monitoring_core.md)
- [TASK-012 Host Phase 4 实时监控 UI 与 RS485 长时验证](tasks/TASK-012-host-phase4-monitoring-ui-rs485-validation.md)
- [Host Phase 4 实时监控 UI 技术规范](specs/host_phase4_monitoring_ui.md)
- [TASK-012 实时监控与 30 分钟 RS485 验证记录](docs/test_results/task012_monitoring_rs485_30min.md)
- [TASK-013 Host 参数配置、通信调试与会话日志](tasks/TASK-013-host-configuration-communication-diagnostics.md)
- [Host Phase 4 参数配置、通信诊断与会话日志技术规范](specs/host_phase4_configuration_and_diagnostics.md)
- [TASK-013 参数配置、通信诊断与真实 RS485 验证记录](docs/test_results/task013_configuration_diagnostics_rs485.md)
- [TASK-014 Host Phase 5 测试用例模型、JSON Schema 与断言](tasks/TASK-014-host-phase5-testcase-schema-loader.md)
- [Host Phase 5 测试用例模型、JSON Schema 与断言技术规范](specs/host_phase5_testcase_schema.md)
- [TASK-014 测试用例 Schema、Loader 与断言验证记录](docs/test_results/task014_testcase_schema_loader.md)
- [TASK-015 Host Phase 5 TestEngine 执行核心](tasks/TASK-015-host-phase5-test-engine-core.md)
- [Host Phase 5 TestEngine 执行核心技术规范](specs/host_phase5_test_engine.md)
- [TASK-015 TestEngine 执行核心验证记录](docs/test_results/task015_test_engine_core.md)
- [TASK-016 Host Phase 5 自动化测试 UI 与真实 RS485 验收](tasks/TASK-016-host-phase5-automation-ui-rs485-validation.md)
- [Host Phase 5 自动化测试 UI 技术规范](specs/host_phase5_automation_ui.md)
- [TASK-016 自动化测试 UI 与真实 RS485 验收记录](docs/test_results/task016_phase5_automation_rs485.md)
- [TASK-017 Host Phase 6 覆盖模型与测试 Schema v2](tasks/TASK-017-host-phase6-schema-and-coverage-model.md)
- [TASK-018 Host Phase 6 复合测试、超时与稳定性执行核心](tasks/TASK-018-host-phase6-composite-stability-engine.md)
- [TASK-019 Host Phase 6 完整自动化套件与 UI 集成](tasks/TASK-019-host-phase6-complete-test-suite.md)
- [TASK-020 Host Phase 6 真实 RS485 完整套件验收](tasks/TASK-020-host-phase6-rs485-full-validation.md)

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
- 当前生产协议已通过 ST-LINK VCP/USART2 和真实 RS485/USART1 两条路径验证；RS485 使用自动换向模块、115200 8N1、Slave ID 1

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

上述默认目标是 `USART2_VCP`。用于当前真实 RS485 台架的镜像必须使用独立构建目录并显式选择端点，且烧录前核对配置输出包含 `Modbus transport: USART1_RS485`：

```powershell
& "$bundleRoot\cmake\4.3.1+st.1\bin\cmake.exe" -S "$projectRoot\firmware\stm32" -B "$projectRoot\build-firmware-rs485" -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$projectRoot\firmware\stm32\cmake\gcc-arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Debug -DOMS555TV_MODBUS_TRANSPORT=USART1_RS485
& "$bundleRoot\cmake\4.3.1+st.1\bin\cmake.exe" --build "$projectRoot\build-firmware-rs485" --parallel
```

2026-09-03 的 TASK-003 最终结果：`oms555tv_firmware.elf` 链接成功，Flash 使用 27,260 B，RAM 使用 2,624 B；22 个本机测试通过，固件已通过 ST-LINK 烧录、校验并运行。GATE-01～04 均已关闭，三路 DHTC12 正式固件各完成 12 个连续有效周期，温度为 25.9～26.2 ℃，36 帧双 CRC 全部通过；光敏 AO 已确认由强光 130～132 mV 上升至完全遮光 2883～2898 mV；B 路断线三周期置位与运行中重连恢复均已验证。实物板标识为 `MB1136-F411RE-C04`、贴纸编号 `A232203276`，TASK-003 已完成。

2026-09-03 的 TASK-005 最终结果：35/35 纯 C 测试和 ARM 干净构建通过，Flash 使用 29,124 B，RAM 使用 3,272 B；固件已通过 VCP/UART 完成 0x03、0x06、0x01/0x02/0x03、错误 CRC、短帧和阈值恢复验证。连续 500 次 0x03 请求为 500/500 成功，响应时间最小/平均/最大为 6.045/7.868/9.708 ms。该结果仅证明 USART2/ST-LINK VCP 协议行为，不代表 RS485 电气层已经验证。

2026-09-03 的 TASK-007 最终结果：Host 全新配置、构建和 8/8 CTest 通过。新增测试覆盖通信模型、CRC16、0x03/0x06 完整 RTU ADU、正常与异常响应、错误判定优先级、任意分片、确定性 Fake、FIFO、QueueFull、单在途、取消、关闭、所有权交接和恰好一次终态。该结果仅为无串口、无硬件验证，不代表 QSerialPort、VCP/UART 或 RS485 已通过。

2026-09-03 的 TASK-008 最终结果：Host 全新配置、构建和 9/9 CTest 通过；生产 `QSerialPortModbusClient` 使用单通信线程、有界 FIFO、异步完成、受控取消/关闭/交接和完整 TX/RX/CRC/RTT 证据。运行时枚举 ST-LINK VCP 为 COM3，全部合法读块、TASK-004 快照解码、四路阈值写入/回读/恢复、远端 0x02/0x03 和错误 Slave ID 超时后的关闭/重开恢复均通过。500 次连续 0x03 为 500/500 成功，RTT 最小/平均/最大 3.348/5.674/6.941 ms，Firmware 通信错误计数 0→0。该结论仅限 VCP/UART；RS485 电气层仍未验证。

2026-09-04 的 TASK-009 最终结果：USART2 VCP 与 USART1 RS485 两种 ARM 构建、35/35 纯 C 测试和 RS485 烧录均通过。第三方 Master 经 COM6 完成全量读写、0x01/0x02/0x03、坏 CRC、短帧、阈值恢复和采集并行验证；500 次连续 0x03 为 500/500 成功，RTT 最小/平均/最大 13.312/16.036/18.446 ms。

2026-09-04 的 TASK-010 最终结果：Host 全新构建和 9/9 CTest 通过，生产 `QSerialPortModbusClient` 经真实 RS485 完成全量闭环；500 次连续 0x03 为 500/500 成功，Host API RTT 最小/平均/最大 26.311/32.477/46.087 ms。断开/重连 T/R+ 与保持/释放设备 RESET 的失败和显式恢复均已实测通过。该结论仅限当前约 20 cm 安全低压点对点台架。

2026-09-04 的 TASK-011 最终结果：Host 全新配置、构建和 10/10 CTest 通过。新增 `AppStateController`、`MonitorService` 和可注入调度器，覆盖连接与所有权状态、五个合法块严格串行轮询、完整快照、Online/Degraded/Offline、可审计通信统计、周期超期不积压，以及无在途/queued/in-flight 停止。测试完全使用 Fake 和手动虚拟时间，不访问 QWidget、串口或真实硬件。

2026-09-04 的 TASK-012 最终结果：新增运行时串口枚举、`MonitoringViewModel` 和完整 Qt Widgets 实时监控界面；全新 Host 构建和 11/11 CTest 通过，真实后端与 UI 测试各重复 10 轮通过。COM6/真实 RS485 连续运行 1,800,007 ms，3,524/3,524 批次、17,620/17,620 请求成功，零失败、零超时，UI 最大心跳迟到 244 ms。MCU 复位时 UI 明确显示离线和陈旧快照，显式断开/重连后恢复 Online，最终稳定复检 50/50 请求成功。结论仅限当前约 20 cm 安全低压点对点台架。

2026-09-04 的 TASK-013 最终结果：新增四路阈值配置服务、通信诊断模型、会话 JSONL 日志和对应 Qt Widgets 页面；全新 Host 构建和 15/15 CTest 通过。COM6/真实 RS485 测试前值为 60.0/60.0/60.0/40.0 ℃，测试值 60.1/60.1/60.1/40.1 ℃ 的逐路写回读通过，结束后恢复并最终读取为原值。会话内 20/20 请求成功且均保留 TX/RX，最终 Review 无必须修复项。

2026-09-04 的 TASK-014 最终结果：新增 v1 JSON Schema、严格 UTF-8 Loader、版本化套件/用例/timeout/retry 模型和无通信依赖的五类断言。有效/无效 fixture、中文示例、边界和结构化差异测试均通过；全新 Host 配置、构建和 17/17 CTest 通过。本任务未访问串口、真实硬件或 QWidget，示例不计入正式测试用例。

2026-09-05 的 TASK-015 最终结果：新增无 QWidget 的 `TestEngine`、可注册基础处理器和不可变 `TestResultManager` 快照，支持单条/批量、禁用/选择跳过、timeout、显式 retry、中止、完整 attempt/RequestId/TX/RX 证据及 TEST 日志。`write_and_verify` 执行写前读取、独立写/回读，并在失败或中止后受控恢复原值；全新 Host 构建和 18/18 CTest 通过。验证完全使用 Fake 与虚拟时间，未访问真实 RS485；自动化 UI 与至少 5 条实机套件留给 TASK-016。

2026-09-05 的 TASK-016 最终结果：新增异步 JSON 加载与测试模式编排层、Qt 自动化测试页面、8 条 Phase 5 基础套件和真实 RS485 验收工具。页面支持选中/全部执行、跳过、中止、可选恢复监控，并展示 PASS/FAIL/ERROR/SKIPPED、当前步骤、RequestId、TX/RX、RTT、实际值和结构化错误。全新 Host 构建和 19/19 CTest 通过；COM6 实测 8/8 用例 PASS，11 个测试请求在结果/诊断/TEST 日志中一致，四路阈值最终恢复为 60.0/60.0/60.0/40.0 ℃。本结果只关闭 Phase 5，不代表 Phase 6 的完整 20 条套件、Phase 7 或 Phase 8 已完成。

实板联调工具会运行时枚举 ST-LINK，不写死 COM 号：

```powershell
$env:Path = "D:\Dev\Qt\6.8.3\msvc2022_64\bin;$env:Path"
& .\build-host\task008_vcp_integration.exe --stress-count 500
```

如存在多个候选串口，可根据工具打印的枚举结果显式增加 `--port COMx`。工具会在阈值测试前保存基线，并在结束时逐路恢复和整块回读；退出码为 0 且输出 `RESULT=PASS` 才表示全部联调步骤通过。

## 当前开发门禁

硬件与引脚基线、Firmware Phase 1/2、Host Modbus 后端以及 RS485 迁移总验收已经完成。后续阶段必须保持以下门禁：

1. `TASK-002/009/010` 已关闭；改变模块、供电、方向方式、线长、终端或偏置时必须重新评审硬件接口和对应测试范围；
2. TASK-011/012/013 已关闭；配置和调试日志复用状态/监控核心与 `IModbusClient`，后续功能仍不得在 QWidget 直接操作 QSerialPort；
3. Phase 4 的实时监控、30 分钟台架、参数配置、通信调试和会话日志已通过；Phase 5 的输入、执行核心、自动化 UI 与 8 条真实 RS485 基础套件也已通过；
4. Phase 5 的 `TASK-014/015/016` 已关闭；后续仍须复用 TestAutomationController、TestEngine、AppStateController 和唯一 `IModbusClient` 路径，不得在 QWidget 临时实现协议或执行循环；
5. Phase 6 按 `TASK-017` 覆盖模型/Schema v2、`TASK-018` 复合与稳定性执行核心、`TASK-019` 完整 20+ 套件/UI、`TASK-020` 真实 RS485 全量验收的顺序推进；前置任务未验收时不得跨层临时实现；
6. `TASK-017`～`TASK-020` 当前仅已制定、尚未派发；Phase 7 人工恢复和 Phase 8 HTML 报告继续留待后续拆分；
7. TASK-014 的中文示例与 fixture 不属于正式用例，TASK-016 的 8 条基础用例也不得表述为 Phase 6 已完成；
8. 8/24 小时稳定性、工业长线、隔离和 EMC 仍未验证，不得从当前短距离台架结果外推。

详细未决项见架构与 Phase 0 任务文档。

## 安全说明

本项目仅用于安全低压环境下的教学与测试验证，不接入真实高压、电流或市电设备，不实现工业保护功能。
