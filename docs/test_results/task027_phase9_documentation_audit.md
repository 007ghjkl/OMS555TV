# TASK-027 Phase 9 工程文档与架构基线审计记录

> 日期：2026-09-06
>
> 结论：通过；TASK-027 完成，可以进入 TASK-028。
>
> 性质：只读代码/证据核对与文档整改，不产生新的功能、构建或实机测试结论。

## 1. 审计范围

本次以主分支 `6031f66` 之后的工作区为输入，核对以下事实源：

- 需求与入口：`../../PROJECT_SPEC.md`、`../../README.md`、`../prd.md`；
- 设计与硬件：`../architecture.md`、`../hardware_baseline.md`、`../modbus_register_map.md`；
- 测试与缺陷：`../test_plan.md`、`../test_cases.md`、`../bug_records.md`、`../phase6_coverage_matrix.md`；
- 实现：Firmware `register_map`/`device_model`/`modbus_slave`，Host `RegisterMap`/`DeviceTypes`/`RegisterCodec` 和 `host/qt/src/CMakeLists.txt`；
- 测试资产：Phase 5 Schema v1 正式套件、Phase 6 Schema v2 主套件与 Fake 边界套件、Phase 7 Schema v3 正式套件；
- 证据：`docs/test_results/` 中 TASK-005、TASK-008～TASK-026 共 20 份已提交验证记录。

## 2. 文档审计清单

| 文档 | 核对来源 | 发现项 | 处理结果 | 剩余边界 |
|---|---|---|---|---|
| `README.md` | PROJECT_SPEC、任务状态、验证记录 | Phase 9 仍写为全部待实施，平台口径未链接 ADR | 更新 TASK-027 状态、导航、平台和门禁 | 最终首页结构、截图和演示入口属于 TASK-028/029 |
| `docs/prd.md` | PROJECT_SPEC、Phase 8 Spec、平台证据 | Windows/Linux 为唯一未决项 | 采用 Windows 已验证、Linux 未验证口径并关闭 MVP 未决项 | 后续能力仍须另立任务 |
| `docs/architecture.md` | Host/Firmware 源码、各阶段 Spec | 标题仍为草案，仅写确认到 Phase 6，缺少可渲染图 | 更新为 MVP 实现基线，补齐四幅 Mermaid 图和 Phase 7/8 现状 | 图不表示工业部署或未实现能力 |
| `docs/hardware_baseline.md` | 硬件记录、TASK-003/009/010/012 | 保留 Phase 1 前待确认措辞、本机绝对路径和唯一设备标识 | 更新完成态措辞，路径参数化，唯一标识不入公开基线 | 只适用于当前安全低压短线台架 |
| `docs/modbus_register_map.md` | Firmware、Host、正式 JSON | 状态仍写“固件实现前”，限制仍写 RS485/符号未验证 | 更新实现状态、验证边界并增加一致性矩阵 | 0x04/0x10/Raw Frame 未实现 |
| `docs/test_plan.md` | 20 份验证记录、正式套件 | Phase 9 状态待更新，部分证据仅写任务名 | 更新 TASK-027 状态与关键证据链接 | TASK-028/029 尚未执行 |
| `docs/test_cases.md` | Schema v1/v2/v3 JSON、TASK-019/020/023 | 标题未覆盖 Phase 7，正式套件链接不完整 | 更新 MVP 状态和 Phase 9 审计结论，补充证据链接 | 4 条 Fake-only 仍不代表实机 |
| `docs/bug_records.md` | TASK-005 记录、提交历史、当前源码 | 修复提交写成相对描述 | 固化提交 `0150121`、证据与当前实现路径 | 不虚构第二个 Bug |
| Phase 0～8 验证记录 | Git 历史、README 汇总、测试计划 | 关键数字一致，未发现互相冲突的终态 | 保持原记录不变，只从核心文档建立链接 | 历史环境与端口只代表当次执行 |

## 3. 寄存器一致性核对

使用 PowerShell 正则分别提取 Firmware 枚举、Host `HoldingRegister` 和 Markdown 表格中的 PDU 地址，再与预期集合比较。三方结果均为：

```text
0,1,2,3,4,9,10,11,12,19,20,29,30,31,39,40
```

核对结果：

- 16 个已实现地址三方完全一致；五个连续读块均为 0～4、9～12、19～20、29～31、39～40；
- Firmware 与 Host 均按二补码解释温度，范围 -400～800（单位 0.1 ℃），阈值只允许 PDU 9～12 写入；
- 告警位 mask 为 `0x000F`，设备状态位 mask 为 `0x003F`；
- 运行时间均按低字 PDU 30、高字 PDU 31 组合，单寄存器保持 Modbus 高字节先传输；
- Firmware 对不支持功能、非法地址和非法值返回 0x01、0x02、0x03，正式用例对应相同语义；
- Phase 5/6/7 正式套件分别为 Schema v1 的 8 条、Schema v2 的 20 条和 Schema v3 的 1 条；Phase 6 另有 4 条 Fake-only 边界用例。

正式套件中的 PDU 50000 和 PDU 40/count 2 是显式异常边界，分别期望 0x02，不是地址表冲突。未发现需要修改实现、协议或测试预期的差异。

## 4. 测试证据口径核对

| 场景 | 已确认事实 | 证据 |
|---|---|---|
| VCP 协议回归 | BUG-001 修复后 500/500 成功，通信错误 0→0 | [TASK-005](task005_vcp_uart_modbus_validation.md) |
| 实时监控 | 1,800,007 ms，17,620/17,620 请求成功 | [TASK-012](task012_monitoring_rs485_30min.md) |
| Phase 5 短套件 | 8/8 PASS，11 个 Testing RequestId 一致 | [TASK-016](task016_phase5_automation_rs485.md) |
| Phase 6 正式套件 | 20/20 PASS；TC-S001 600001 ms、599/599；总计 647 个 Testing RequestId | [TASK-020](task020_phase6_rs485_full_validation.md) |
| Phase 7 半自动恢复 | 连续 3 次超时与 3 次合法响应；稳定恢复 716 ms；6 个 RequestId | [TASK-023](task023_phase7_rs485_guided_recovery.md) |
| Phase 8 报告 | 27/27 CTest；真实短套件报告 8/8 PASS、11 个 RequestId | [TASK-026](task026_phase8_report_export.md) |

以上数字均直接来自既有记录。本任务没有重新运行 Host/Firmware 构建、串口、长稳态或人工断线测试，因此不增加新的性能或实机结论。

## 5. 架构图核对

`docs/architecture.md` 当前包含四幅 GitHub Mermaid 图：

1. 系统与硬件边界；
2. Host 模块依赖、通信 owner 与线程边界；
3. 应用状态机；
4. 测试、不可变证据、日志与 HTML 报告数据流。

节点名称来自当前源码目录和类型；图中没有加入 Linux、Raw Frame、自动重连、PDF、数据库或云服务。Mermaid 使用 GitHub 支持的 `flowchart` 与 `stateDiagram-v2` 基础语法，不依赖外部图片或插件。

## 6. 平台决策

已新增 [MVP 平台支持基线 ADR](../decisions/2026-09-06-MVP平台支持基线.md)：

- MVP 已验证 Host 平台：Windows x64；
- 当前完整证据组合：Qt 6.8.3、MSVC 2022、CMake、Ninja；
- Linux：保留 Qt/CMake 可移植边界，但未完成构建、测试和串口实机验证，不列为已支持平台；
- 未来增加 Linux 支持必须另立任务并获得独立证据。

PRD 与架构中的平台未决项已按该 ADR 关闭。

## 7. 验证与 Review

- 任务结构、89 份 Markdown 文档的本地链接、尾随空白和 Git diff 范围检查通过；
- 寄存器地址集合自动比较通过，正式 JSON 文件可解析且用例数量符合目录；
- 四个 Mermaid 代码块的围栏、图类型和 GitHub Mermaid 基础语法检查通过，其中 3 个 `flowchart`、1 个 `stateDiagram-v2`；
- 修改范围仅为文档，没有生产代码、测试资产或历史结果变更；
- 必须修复：无；
- 建议修复：无；
- 可选优化：TASK-028 采集真实脱敏截图与素材清单，TASK-029 重构最终 README 并执行干净构建和现场演示彩排。
