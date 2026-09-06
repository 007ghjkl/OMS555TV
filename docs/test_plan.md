# 测试计划

> 状态：MVP 最终验证基线 3.0（TASK-027～029 与 Phase 9 已完成）

## 1. 目标

验证 DUT 数据、Modbus RTU 行为、Qt 解析与配置、异常恢复、性能和稳定性，并保证每个结论都有日志或报告证据。

## 2. 测试层次

- 纯软件单元测试：CRC、缩放、寄存器映射、告警迟滞、JSON 校验、断言和报告模型。
- Host 集成测试：使用 Fake Modbus Client 验证监控、配置、通信诊断、日志、TestEngine 与自动化测试 UI，无需真实硬件。
- 协议联调测试：第三方 Master 和 Qt 生产后端均已与真实 STM32 通过 VCP/UART 和 RS485 分层验证。
- 系统测试：覆盖人工故障注入、恢复、长时运行和报告输出。

## 3. 测试类别与最低范围

| 类别 | MVP 目标 |
|---|---:|
| 功能 | 温度、阈值、告警、版本读写与回读 |
| 协议 | 正常帧、非法功能/地址/数据、超时、连续请求 |
| 边界 | 最小/最大值、临界值、超范围和地址边界 |
| 一致性 | 固件值、寄存器值、Qt 工程值一致 |
| 恢复 | RS485 断线恢复；设备复位 |
| 性能 | RTT 与请求成功/失败/超时统计 |
| 稳定性 | 可配置持续时间、间隔和允许失败率 |

最终有效用例不少于 20 条，其中至少 1 条为半自动恢复用例。

## 4. 进入条件

- 对应需求、寄存器表和接口规范已评审。
- 两端可独立构建，目标功能已完成单元测试。
- 测试环境、固件版本、Host 版本和通信参数可记录。

## 5. 退出条件

- 计划内用例均有明确状态，不存在无说明的 `NOT_RUN`。
- 失败项包含可定位证据或已登记缺陷。
- 报告汇总与明细一致。
- 性能和稳定性数据只引用真实执行结果。

## 6. 证据要求

每条通信相关用例至少保存用例 ID、请求 ID、开始/结束时间、TX、RX、RTT、实际值、结果和失败原因。人工步骤必须记录提示内容、确认/取消动作和时间戳。

## 7. 环境与风险

- 当前硬件与引脚基线已确认；Firmware Phase 1/2 已完成构建、烧录和 VCP/UART 实机验证，证据见 [TASK-005 VCP/UART 验证记录](test_results/task005_vcp_uart_modbus_validation.md) 及 TASK-003 记录。
- TASK-005/008 已证明 USART2 VCP 下的 Firmware 与 Host Modbus RTU 闭环。
- TASK-009/010 已证明当前 USART1、自动换向模块、约 20 cm 两线 RS485 和当时枚举为 COM6 的转换器下的双向 Modbus、500 次连续请求、物理断线恢复与设备复位恢复；端口号不作为固定配置。证据见 [Firmware RS485 验证记录](test_results/task009_firmware_rs485_transport_validation.md) 与 [Host RS485 系统联调记录](test_results/task010_host_rs485_system_integration.md)。
- TASK-011 的应用状态与监控核心、TASK-012 的实时 UI、TASK-013 的参数配置/通信诊断/会话日志均已通过自动化测试；TASK-012 另完成 30 分钟真实 RS485、设备复位、陈旧状态、UI 响应和显式重连验收；TASK-013 完成四路阈值真实写回读、原值恢复和 20/20 请求会话证据核对。证据见 [TASK-012 记录](test_results/task012_monitoring_rs485_30min.md) 与 [TASK-013 记录](test_results/task013_configuration_diagnostics_rs485.md)。
- Host Phase 5 的 TASK-014 测试模型/JSON Schema/断言、TASK-015 TestEngine 和 TASK-016 自动化测试 UI/真实 RS485 基础套件均已完成。TASK-016 全新构建和 19/19 CTest 通过，COM6 上 8/8 基础用例 PASS，11 个测试 RequestId 与诊断/TEST 日志一致，阈值前后独立读取一致。证据见 [TASK-016 记录](test_results/task016_phase5_automation_rs485.md)。
- Host Phase 6 的 TASK-017 覆盖模型/Schema v2、TASK-018 复合/超时/稳定性执行核心、TASK-019 完整 20+ 套件/UI 集成和 TASK-020 真实 RS485 全量验收均已完成。TASK-020 最终 20/20 CTest 与 COM6 实机预检/中止专项通过；正式主套件 20/20 PASS，真实稳定性持续 600001 ms、599/599 请求成功、0 失败、0 超时，647 个 Testing RequestId 的跨层证据一致且阈值最终恢复。证据见 [TASK-020 记录](test_results/task020_phase6_rs485_full_validation.md)。
- Host Phase 7 的 TASK-021 引导式模型/Schema v3、TASK-022 半自动控制器/UI、TASK-023 RS485 A/B 物理断线恢复实机验收均已完成。TASK-023 全新构建和 23/23 CTest 通过；COM6 正式 `TC-R001` 观察到 3 次连续超时与 3 次连续合法响应，首次/稳定恢复为 51/716 ms，6 个 Testing RequestId 跨层一致，最终链路在线。证据见 [TASK-023 记录](test_results/task023_phase7_rs485_guided_recovery.md)。
- Host Phase 8 的 TASK-024/025/026 已完成：报告契约、自包含 HTML、最近完整结果页面、必填元数据门禁和后台一键导出均有自动化覆盖。四类确定性 fixture 覆盖全 PASS、混合终态、stability 有界证据和 guided recovery；真实脱敏示例自动核对 8 个用例、11 个 RequestId/TX/RX/RTT、Firmware、通信参数、日志摘要、离线资源和 SHA-256。全新 Host 构建和 27/27 CTest 通过；COM6 Phase 5 短套件 8/8 PASS，桌面、窄窗口和打印预览通过。证据见 [TASK-026 记录](test_results/task026_phase8_report_export.md)。
- Phase 9 的 TASK-027 已完成文档、寄存器、测试证据和缺陷记录审计及平台支持 ADR；TASK-028 已完成真实硬件/生产 Host 六张脱敏素材和示例 HTML 复核；TASK-029 已完成最终 README、独立构建/回归、实机短套件和生产 UI 现场彩排。MVP 16 项最低验收标准全部 PASS。证据见 [TASK-027 审计记录](test_results/task027_phase9_documentation_audit.md)、[TASK-028 验收记录](test_results/task028_phase9_demo_assets.md)、[TASK-029 最终验收](test_results/task029_phase9_final_acceptance.md) 与 [MVP 证据矩阵](mvp_acceptance_matrix.md)。
- 标准 Modbus Client 未必能发送 CRC 错误帧，可能需要后续 Raw Frame 接口。
- 8/24 小时稳定性测试只能在具备持续硬件环境后执行。

## 8. 结果真实性

未实际运行的测试不得标记通过。TASK-003、TASK-005、TASK-008～TASK-026 只能按各自记录标记通过；TASK-027 只完成文档与证据一致性审计，不产生新的功能或实机通过结论。TASK-028 的 Phase 5 smoke 只证明短时素材采集与唯一阈值写入恢复成功；TASK-029 的新短套件和现场彩排也只关闭最终复现/演示门禁，二者都不能替代 TASK-020 的正式 20 条/10 分钟实机证据或 TASK-026 的既有示例基线。TASK-024 只验证纯数据报告契约，TASK-025 只验证确定性 fixture HTML 与文件行为。TASK-022 的 Fake/虚拟时间结果与 TASK-023 的真实人工断线—恢复结果保持独立口径，TASK-019 的 Fake/虚拟时间结果与 TASK-020 的真实 RS485 结果也保持独立口径。Phase 9 截图和演示记录不改变这些证据口径；原生 PDF 仍为可选未实现能力。当前短距离 RS485 结果不得外推为 USB 自动重连、8/24 小时稳定性、工业长线、隔离或 EMC 验证。
