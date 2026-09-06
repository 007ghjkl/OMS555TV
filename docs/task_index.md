# 项目任务历史索引

本页保留从硬件基线到 MVP 最终验收的完整任务追溯入口。当前状态与本次验证结果以各任务文档和 `docs/test_results/` 记录为准。

## Phase 0：基线与工程骨架

- [TASK-000 硬件基线确认](../tasks/TASK-000-confirm-hardware-baseline.md)
- [TASK-001 项目骨架](../tasks/TASK-001-phase0-project-skeleton.md)
- [TASK-002 RS485 硬件迁移总任务](../tasks/TASK-002-rs485-hardware-migration.md)

## Phase 1～2：Firmware

- [TASK-003 基础采集与设备模型](../tasks/TASK-003-firmware-phase1-basic-acquisition.md)
- [TASK-005 Modbus RTU Slave](../tasks/TASK-005-firmware-phase2-modbus-rtu-slave.md)
- [TASK-009 USART1/RS485 传输迁移](../tasks/TASK-009-firmware-usart1-rs485-transport-migration.md)

## Phase 3：Host Modbus 核心与 RS485 联调

- [TASK-004 设备模型与寄存器编解码](../tasks/TASK-004-host-device-model-register-codec.md)
- [TASK-006 Modbus 后端决策与异步通信规范](../tasks/TASK-006-host-phase3-modbus-backend-decision.md)
- [TASK-007 Modbus 核心、通信契约与 Fake](../tasks/TASK-007-host-phase3-modbus-core-and-fake.md)
- [TASK-008 QSerialPort 后端与 VCP 联调](../tasks/TASK-008-host-phase3-qserialport-backend-vcp-integration.md)
- [TASK-010 Host 真实 RS485 系统联调](../tasks/TASK-010-host-rs485-system-integration.md)

## Phase 4：监控、配置与诊断

- [TASK-011 应用状态与监控核心](../tasks/TASK-011-host-phase4-monitoring-core.md)
- [TASK-012 实时监控 UI 与长时 RS485](../tasks/TASK-012-host-phase4-monitoring-ui-rs485-validation.md)
- [TASK-013 参数配置、通信诊断与会话日志](../tasks/TASK-013-host-configuration-communication-diagnostics.md)

## Phase 5：自动化测试基础

- [TASK-014 测试用例模型、Schema 与断言](../tasks/TASK-014-host-phase5-testcase-schema-loader.md)
- [TASK-015 TestEngine 执行核心](../tasks/TASK-015-host-phase5-test-engine-core.md)
- [TASK-016 自动化测试 UI 与实机验收](../tasks/TASK-016-host-phase5-automation-ui-rs485-validation.md)

## Phase 6：完整套件与稳定性

- [TASK-017 Schema v2 与覆盖模型](../tasks/TASK-017-host-phase6-schema-and-coverage-model.md)
- [TASK-018 复合、超时与稳定性执行核心](../tasks/TASK-018-host-phase6-composite-stability-engine.md)
- [TASK-019 完整 20+ 套件与 UI](../tasks/TASK-019-host-phase6-complete-test-suite.md)
- [TASK-020 真实 RS485 完整套件验收](../tasks/TASK-020-host-phase6-rs485-full-validation.md)

## Phase 7：半自动故障恢复

- [TASK-021 引导式模型与 Schema v3](../tasks/TASK-021-host-phase7-guided-test-schema.md)
- [TASK-022 半自动协调器与 UI](../tasks/TASK-022-host-phase7-guided-controller-ui.md)
- [TASK-023 RS485 A/B 断线恢复验收](../tasks/TASK-023-host-phase7-rs485-guided-recovery-validation.md)

## Phase 8：正式测试报告

- [TASK-024 报告契约与元数据](../tasks/TASK-024-host-phase8-report-contract-metadata.md)
- [TASK-025 自包含 HTML 报告生成器](../tasks/TASK-025-host-phase8-html-report-generator.md)
- [TASK-026 报告 UI、一键导出与验收](../tasks/TASK-026-host-phase8-report-ui-export-validation.md)

## Phase 9：工程完善与 MVP 关闭

- [TASK-027 工程文档基线与架构图](../tasks/TASK-027-phase9-documentation-architecture-baseline.md)
- [TASK-028 展示素材、截图与示例报告](../tasks/TASK-028-phase9-demo-assets-example-report.md)
- [TASK-029 最终 README、复现与现场演示验收](../tasks/TASK-029-phase9-final-readme-demo-acceptance.md)

技术规范位于 [`specs/`](../specs/)，逐阶段验证记录位于 [`docs/test_results/`](test_results/)。
