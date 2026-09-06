# Host Phase 8 报告 UI 与一键导出 Technical Spec

## 1. 文档状态

- 版本：1.0
- 日期：2026-09-06
- 对应任务：TASK-026
- 评审结论：通过，无阻塞未决项

## 2. 目标与边界

本规范把 TASK-024 的 `ReportInput`/`ReportModelBuilder` 与 TASK-025 的
`HtmlReportGenerator` 接入 Qt Widgets 应用。报告页只负责展示最近一次完整运行、收集人工元数据和提交导出，不重新判定测试状态、不解析 TX/RX，也不从 JSONL 重建结果。

输出仅为 UTF-8 自包含 HTML。原生 PDF、历史结果选择器、报告数据库、签名、上传和自动重跑均不在本任务范围。

## 3. 采用方案

新增无 QWidget 依赖的 `ReportExportController`，由 MainWindow 的报告页绑定：

- 控制器监听 `TestAutomationController::workflowFinished`，只接收其当前可见且全部为终态的不可变 `TestSuiteResult`；
- 运行结束时同时保存连接配置、Host 版本、源码标识、显示时区及 SessionLog 身份/路径快照；
- UI 提交 `ReportExportRequest` 后，控制器冻结操作员字段、目标路径和上述快照；
- 后台线程计算已结束 SessionLog 的大小/SHA-256、调用 `ReportModelBuilder`、再调用 `HtmlReportGenerator::write`；
- 完成结果通过 queued invocation 回到控制器所属线程，QWidget 只在应用线程渲染状态。

不把报告职责放入 `TestEngine`，也不让 MainWindow 持有报告生成算法。

## 4. 最近一次完整结果生命周期

1. 应用启动时没有可导出结果，状态为 `NoCompletedResult`。
2. 测试开始后旧的完整结果仍保留，但整个测试工作流非 Idle 时门禁为 `TestRunInProgress`，不能导出旧快照。
3. 工作流结束后，若控制器可见结果具有非零 run ID、有效 suite、有效起止时间，且 suite/case 均为 `PASS/FAIL/ERROR/SKIPPED`，则以共享的只读快照替换上一结果。
4. 工作流失败且没有发布完整终态结果时，不替换上一结果；错误门禁仍按当前工作流状态展示。
5. 加载另一套件不会删除最近一次完整结果；新套件只有完成运行后才替换报告绑定。
6. 页面不提供旧结果选择。一旦新结果完成，旧预览和默认文件名立即失效，避免把旧元数据导出为新结果。
7. 导出已开始后输入独立冻结；其间即使新测试启动或完成，也不改变该作业。作业完成后页面切换到最新结果，成功结果仍保留明确 run ID 和绝对路径。

`ReportPreview` 的状态与计数来自 `ReportModelBuilder` 的文档模型，不由 UI 重算。

## 5. 报告页

页面包含：

- 最近运行：suite ID/name、run ID、Session ID、整体终态、UTC 起止时间、耗时、用例和四终态计数、证据保留说明；
- 固定/来源字段：项目名 `OMS555TV`、测试对象、套件提供的设备型号/台架/环境；
- 人工字段：测试人员（必填，不读取 Windows 用户名）、设备型号/台架/环境补充；套件已提供时补充值不覆盖套件；
- 输出目录和文件名。目录默认 `output/reports/`，文件名使用 TASK-025 建议值；
- 唯一主操作“生成 HTML 报告”，以及成功后的可选“打开报告”。不显示 PDF 按钮。

测试人员为空白时按钮禁用并展示 `MissingRequiredMetadata`；没有结果、运行中和导出中分别展示稳定错误码。输出目录可浏览，文件名可修改以处理重名。

## 6. 导出请求、状态与错误

`ReportExportRequest` 包含 `OperatorReportMetadata`、输出目录和单一文件名。文件名必须是非绝对的单个 `.html` 文件名，不能包含目录跳转；目标转换为绝对路径后交给生成器。

状态为 `NoResult`、`Ready`、`Exporting`、`Succeeded` 或 `Failed`。重复点击在 `Exporting` 时同步拒绝，不启动第二个写任务。

在既有 `ReportErrorCode` 上新增：

- `NoCompletedResult`：没有完整结果；
- `TestRunInProgress`：测试工作流尚未 Idle；
- `MissingRequiredMetadata`：测试人员为空白；
- `ExportInProgress`：已有导出作业；
- `InvalidSessionLogArtifact`：日志身份、状态、读取或哈希不满足工件要求。

模型或文件错误沿用 TASK-024/025 稳定错误码。任何报告失败均不写回 `TestSuiteResult`。成功记录绝对路径、文件大小和 run ID；打开浏览器失败只显示独立提示，不回滚生成成功。

## 7. SessionLog 工件

- 只有结果 Session ID 非空、快照 Session ID 与之相同、会话已经结束、路径仍为同一普通文件时，才计算大小和 SHA-256 并标记 `available=true`。
- 结果没有 Session ID 时，报告明确显示未提供。
- 导出时同一会话仍活动，或已切换至另一会话时，不猜测内容，工件降级为不可用并显示原因；这不改变测试终态。
- 本任务的真实短套件要求先结束会话再导出，因此必须得到可用工件。
- Phase 8 短套件不会触发日志分片；多分片归档保持后续范围。

## 8. 文件与并发

- 默认目录由应用按需创建；用户输入的其他不存在目录不自动创建，返回 `InvalidOutputPath`。
- 不覆盖、不删除、不自动改名。重名返回 `TargetAlreadyExists`，用户修改文件名或目录后再次提交。
- 模型构建、日志读取/哈希、HTML 渲染和 `QSaveFile` 提交全部在全局线程池执行。
- 每个控制器最多一个导出作业。后台不访问 QWidget、QObject 或可变运行状态，只持有值快照。
- 控制器销毁后后台结果通过 `QPointer` 丢弃，不访问失效对象。

## 9. 测试与验收

自动化测试必须覆盖：

1. 无结果、运行中、缺少测试人员、非法目录/文件名和重名门禁；
2. 完整结果发布、加载新套件不替换、下一运行完成后替换、导出期间输入冻结；
3. 后台成功/失败回到应用线程，重复提交不并发，测试终态不变；
4. offscreen 页面字段、来源提示、按钮门禁、成功绝对路径和错误码；
5. 从确定性 `TestSuiteResult` fixture 生成全 PASS、混合终态、stability 有界证据和 guided recovery 四份 HTML，并核对结构、计数、转义及离线资源；
6. 全新配置、构建、全部 CTest 与应用 smoke。

真实 RS485 使用 Phase 5 八用例短套件。开始会话后连接当前串口，执行全部用例，确认写阈值用例已保存、回读并恢复；结束会话后在报告页一键导出。核对 Firmware、串口、Session ID、RequestId、TX/RX/RTT 与不可变结果/日志一致。

人工视觉检查覆盖桌面宽度、窄窗口和浏览器打印预览。真实原始报告留在忽略目录；提交到 `docs/examples/` 的副本只把本机绝对日志路径替换为相对脱敏说明，不改动状态、计数、时间、请求证据、文件大小或日志 SHA-256。

## 10. 评审记录

### 必须修复

无。

### 建议修复

无。采用显式拒绝重名并开放文件名编辑，保持 TASK-025 的不覆盖契约，同时避免隐式递增导致用户误认目标文件。

### 可选优化

后续可增加历史运行选择、多分片日志清单、原生 PDF 和签名；均需另立任务，不影响 Phase 8 验收。
