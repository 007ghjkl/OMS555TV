# MVP 最低验收标准证据矩阵

> 对应 `PROJECT_SPEC.md` 第 29 节。状态只由已执行证据支持；TASK-029 已完成最终文档检查和生产 UI 现场彩排。

| # | 最低验收标准 | 状态 | 主要证据 |
|---:|---|---|---|
| 1 | STM32 可稳定采集或模拟 4 路监测数据 | PASS | [硬件基线](hardware_baseline.md)：三路 DHTC12 各 12 个连续有效周期；环境温度明确为模拟源 |
| 2 | STM32 实现 Modbus RTU Slave | PASS | [TASK-005 VCP/UART](test_results/task005_vcp_uart_modbus_validation.md)、[TASK-009 RS485](test_results/task009_firmware_rs485_transport_validation.md) |
| 3 | Qt 实现 Modbus RTU Master | PASS | [TASK-010 Host/RS485 联调](test_results/task010_host_rs485_system_integration.md) |
| 4 | Qt 能实时监控设备 | PASS | [TASK-012 30 分钟监控](test_results/task012_monitoring_rs485_30min.md)、[真实截图](assets/host-monitoring.png) |
| 5 | Qt 能配置阈值 | PASS | [TASK-013 配置验收](test_results/task013_configuration_diagnostics_rs485.md)、[真实截图](assets/host-configuration.png) |
| 6 | Qt 能显示告警 | PASS | [硬件基线](hardware_baseline.md)、[TASK-012 监控 UI](test_results/task012_monitoring_rs485_30min.md)、[真实截图](assets/host-monitoring.png) |
| 7 | Qt 能查看 TX/RX | PASS | [TASK-013 通信诊断](test_results/task013_configuration_diagnostics_rs485.md)、[真实截图](assets/host-diagnostics.png) |
| 8 | Qt 有独立测试模式 | PASS | [TASK-016 自动化 UI](test_results/task016_phase5_automation_rs485.md) |
| 9 | 支持 JSON 测试用例 | PASS | [TASK-014 Schema/Loader](test_results/task014_testcase_schema_loader.md) |
| 10 | 至少 20 条有效测试 | PASS | [测试用例目录](test_cases.md)、[TASK-020 20/20 实机验收](test_results/task020_phase6_rs485_full_validation.md) |
| 11 | 至少包含功能、协议、边界、异常、稳定性测试 | PASS | [Phase 6 覆盖矩阵](phase6_coverage_matrix.md)、[TASK-020 记录](test_results/task020_phase6_rs485_full_validation.md) |
| 12 | 至少实现 1 个半自动故障恢复测试 | PASS | [TASK-023 A/B 物理断线恢复](test_results/task023_phase7_rs485_guided_recovery.md) |
| 13 | 能统计响应时间和通信成功率 | PASS | [TASK-012 监控统计](test_results/task012_monitoring_rs485_30min.md)、[TASK-020 stability](test_results/task020_phase6_rs485_full_validation.md) |
| 14 | 能生成测试报告 | PASS | [TASK-026 报告验收](test_results/task026_phase8_report_export.md)、[真实脱敏 HTML](examples/task026-phase5-rs485-report.html) |
| 15 | 有完整 README 和技术文档 | PASS | [README](../README.md)、[TASK-027 文档审计](test_results/task027_phase9_documentation_audit.md)、[任务索引](task_index.md)、[TASK-029 最终检查](test_results/task029_phase9_final_acceptance.md) |
| 16 | 可进行现场演示 | PASS | [现场演示手册](demo_runbook.md)、[TASK-029 生产 UI 彩排](test_results/task029_phase9_final_acceptance.md) |

## 证据边界

- TASK-019 的 Fake/虚拟时间结果不等于 TASK-020 的实机结果；两者分别证明执行语义与真实 RS485 行为。
- TASK-023 只验证人工 A/B 断线—恢复，不证明 USB 自动重连、STM32 Reset 自动恢复或工业现场可靠性。
- TASK-026/028 的 Phase 5 短套件与展示截图不替代 Phase 6 的 20 条/10 分钟正式验收。
- TASK-029 已完成链接/清洁检查与受控现场彩排，16 项最低验收标准全部 PASS。
