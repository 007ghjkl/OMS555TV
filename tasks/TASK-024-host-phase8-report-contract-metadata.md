# TASK-024 Host Phase 8 报告契约与元数据策略

## 目标

定义稳定、不可变、可验证的报告输入与文档模型，解决项目、设备、版本、测试人员、通信参数和环境信息的来源问题，并统一自动、复合、稳定性及半自动结果的汇总与明细口径。

## 背景

Phase 5～7 已形成 `TestSuiteResult`、用例/步骤/attempt、稳定性统计、人工动作、恢复时间和 Session ID，但尚无 `ReportGenerator` 所需的完整输入契约。`docs/prd.md` 仍把报告元数据来源列为未决问题；若直接从当前 UI、日志文本或运行环境临时拼装，会导致报告不可复现、来源不明或与测试结果不一致。

架构要求 `ReportGenerator` 只消费结果模型，不重新解释通信数据。本任务先冻结报告数据边界，后续 HTML 生成器不得自行推断测试状态。

## 范围

- 实现前创建并评审 `specs/host_phase8_report_contract.md`。
- 定义不可变 `ReportInput`、`ReportMetadata`、`ReportDocumentModel`、`ReportSummary`、用例/步骤/证据模型和结构化 `ReportError`。
- 明确元数据字段及来源标签：system-observed、suite-configured、operator-entered、unavailable。
- 采用以下来源策略并在 Spec 中固化：
  - 项目名称默认使用应用常量 `OMS555TV`；
  - 测试对象、套件名称/ID 和套件标签来自已执行的不可变 `TestSuite`；
  - 设备型号、台架和环境说明优先使用套件 metadata，允许操作员在生成前显式补充；
  - Firmware 版本优先使用本次结果中经过断言的版本读取，缺失时明确显示“未采集”，不得从文件名猜测；
  - Host 版本来自应用版本和可得的源码标识；
  - 通信参数来自本次连接配置快照；
  - 测试人员由操作员输入，不读取 Windows 用户名；
  - 测试开始/结束时间、总耗时和 Session ID 来自不可变运行结果。
- 定义元数据必填项、长度、字符、安全转义前原始值和缺失显示规则。
- 定义汇总算法：总数、已执行数、PASS/FAIL/ERROR/SKIPPED、通过率和总耗时；整体状态直接沿用不可变 `TestSuiteResult` 并校验其与计数一致。
- 通过率固定为 `PASS / (PASS + FAIL + ERROR)`；SKIPPED 不进入分母，分母为 0 时显示 N/A。
- 定义每类用例的报告映射：基础请求、sequence、consistency、stability 和 guided_recovery。
- 定义证据映射：RequestId、TX/RX、RTT、错误码、断言差异、清理错误、人工动作、恢复时间和证据保留摘要。
- 定义 SessionLog 工件描述：session ID、日志路径、大小、SHA-256 和是否可用；报告只引用描述，不解析日志来重算结果。
- 使用纯数据测试验证汇总、状态优先级、缺失字段和所有结果类型。

## 非范围

- 不生成 HTML/PDF 文件，不实现 CSS、模板或 QWidget 页面。
- 不修改 TestEngine 的判定，不从 JSONL 重建或修正测试结果。
- 不持久化测试人员偏好，不引入账户、数据库或 Windows 用户身份读取。
- 不重新执行测试或访问真实 RS485。
- 不实现报告签名、加密、上传或云端存储。

## 依赖

- TASK-015/018 的不可变自动测试结果、复合步骤和稳定性统计。
- TASK-021/022 的人工动作、观察和 guided recovery 结果。
- TASK-013 SessionLogService 的会话工件。
- `PROJECT_SPEC.md` 第 21 节和 Phase 8 验收要求。
- `docs/prd.md` 的报告元数据未决项。

## 实现门禁

1. Spec 必须先列出每个报告字段、类型、必填性、来源、缺失策略和展示单位。
2. 报告汇总只能从不可变用例终态计算，不得读取 UI 文本或日志事件猜测 PASS/FAIL。
3. Firmware、Host、串口和测试环境信息必须保存来源标签，用户输入不得冒充系统观测。
4. 稳定性有界内存证据与完整 JSONL 工件的边界必须清楚，不能声称 HTML 内含被丢弃的全部 attempt。
5. guided 人工确认必须按原始记录展示，并明确用户确认不是自动 PASS 证据。
6. 报告错误不得改变原测试结果；生成失败使用独立 `ReportError`。

## 实现要求

1. 所有汇总计数使用溢出安全整数；必须满足总数等于四种终态之和。
2. 报告不得重新判定整体状态；只能沿用 TestSuiteResult 的终态并按 Engine 既有规则校验。全 SKIPPED/空套件的通过率显示 N/A，套件状态仍按原结果展示。
3. 时间戳统一保留 UTC，并可生成带明确时区的本地展示值；持续时间使用运行结果值。
4. TX/RX 使用大写十六进制、字节间空格；空 RX 必须与“字段缺失”区分。
5. RTT 明确单位和缺失状态，不把缺失值显示为 0。
6. 序列、稳定性与 guided 结果保留原有层级和 step ID，不压平成不可追溯文本。
7. 文档模型只含已转义前的纯文本和结构化数据，不允许注入原始 HTML。
8. v1/v2/v3 套件及现有结果结构必须保持兼容。

## 验收标准

1. Technical Spec 已评审，元数据来源和报告字段无未决项。
2. `docs/prd.md` 中报告元数据来源未决项已按确认策略同步关闭。
3. Host 全新配置、构建和全部 CTest 通过，无 Phase 5～7 回归。
4. PASS/FAIL/ERROR/SKIPPED 混合结果的总数、已执行数、通过率和整体状态计算正确。
5. 空套件、全 SKIPPED、非终态输入、时间缺失、RTT 缺失和计数/套件状态不变量错误均得到确定结果或结构化错误。
6. 基础、sequence、consistency、stability 和 guided_recovery 均可完整转换为 ReportDocumentModel。
7. 自动与人工结果保留 expected/actual、失败原因、时间、步骤和证据关联。
8. 稳定性报告明确展示总 attempt、内存保留/丢弃和外部 SessionLog 工件。
9. 所有元数据都能追溯来源，不自动读取用户身份或猜测 Firmware 版本。
10. 最终 Review 无必须修复项，允许进入 TASK-025。

## 测试要求

- 汇总：全部终态组合、通过率、空分母、总耗时和套件状态一致性。
- 映射：基础、复合、稳定性和半自动结果的完整字段。
- 元数据：四类来源、必填/缺失、长度和不可用值。
- 证据：TX/RX、RTT、错误码、清理错误、保留摘要和日志工件。
- 回归：全部 Host CTest 和应用 smoke test。

## 当前状态

已完成（2026-09-06）。Technical Spec 已评审；纯数据报告模型、元数据来源、汇总/状态校验、五类结果映射、证据与 SessionLog 工件契约均已实现。全新 Host 配置和构建成功，24/24 CTest（含应用 smoke）通过；最终 Review 无必须修复项，可以进入 TASK-025。本任务未生成 HTML/PDF、未访问串口或真实硬件。
