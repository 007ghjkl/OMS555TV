# Host Phase 8 报告契约与元数据策略 Technical Spec

## 1. 文档状态

- 版本：1.0
- 日期：2026-09-06
- 对应任务：TASK-024
- 评审结论：通过，无阻塞未决项

## 2. 目标与边界

本规范冻结 Phase 8 报告输入、元数据来源、汇总算法及纯文档模型。报告映射层只消费已完成的不可变 `TestSuiteResult`、本次连接配置快照和调用方显式提供的元数据，不访问 QWidget、串口、当前应用状态、Windows 用户身份或 JSONL 内容，也不重新执行断言。

本任务只产生未转义的纯文本和结构化数据，不生成 HTML/PDF。TASK-025 必须对所有文本统一转义，不能假设本模型中的任何文本可信。

## 3. 采用方案

新增无 QObject/WIdgets 依赖的 `report` 模块：

- `ReportInput`：一次构建所需的完整值快照；
- `ReportMetadata`：每项值、来源及缺失显示策略；
- `ReportSummary`：终态计数、精确通过率分子/分母和 ppm 展示值；
- `ReportDocumentModel`：套件、用例、步骤、人工动作、观察、事务证据和日志工件；
- `ReportError`：报告映射自身的结构化错误；
- `ReportModelBuilder`：唯一映射入口，成功时返回完整文档，失败时不返回部分文档。

模型使用值语义。构建器接收 `const ReportInput &`，复制所需字段后返回新对象；之后修改 UI、连接或原结果持有者不会改变文档模型。

## 4. 输入契约

### 4.1 `ReportInput`

| 字段 | 类型 | 必填 | 来源 | 缺失策略 | 单位/格式 |
|---|---|---:|---|---|---|
| `suiteResult` | `TestSuiteResult` | 是 | 测试引擎不可变终态 | 缺少 suite 或非终态返回错误 | 原始结构 |
| `connectionConfig` | `optional<ModbusConnectionConfig>` | 否 | 本次连接配置快照 | 显示“未采集” | 结构化串口参数 |
| `applicationVersion` | `QString` | 否 | 应用版本 | 显示“未采集” | 原始纯文本，最长 128 |
| `sourceRevision` | `QString` | 否 | 构建/调用方可得源码标识 | 显示“未采集” | 原始纯文本，最长 128 |
| `operatorMetadata` | `OperatorReportMetadata` | 否 | 操作员生成前显式输入 | 各字段显示“未填写” | 见 5.3 |
| `sessionLogArtifact` | `optional<SessionLogArtifactInput>` | 否 | SessionLog 会话结束后的工件描述 | `available=false`，不访问文件系统补全 | 字节、SHA-256 |
| `displayTimeZone` | `QTimeZone` | 是 | 调用方显式快照 | 无效时返回错误 | IANA/UTC/固定偏移时区 |

`ReportInput` 不包含可执行回调和 UI 引用。Fake 套件可不提供串口快照；真实 RS485 报告应提供，但缺失只会明确标记“未采集”，不会改变测试结果。

### 4.2 终态门禁

- suite 与每条 case 只接受 `PASS/FAIL/ERROR/SKIPPED`；`NOT_RUN/RUNNING` 返回 `NonTerminalStatus`。
- `suiteResult.suite` 必须存在；结果用例数量、顺序和 ID 必须与不可变套件一致。
- UTC 开始/结束时间必须有效，结束不得早于开始；持续时间必须非负。持续时间继续使用引擎保存值，不用墙钟差覆盖。
- `runId` 必须非零；case/session 的关联字段只展示原值，不补造 ID。

## 5. 元数据契约

### 5.1 来源标签

每个 `ReportMetadataValue` 必须保存以下来源之一：

| 枚举 | 稳定名称 | 含义 |
|---|---|---|
| `SystemObserved` | `system-observed` | 由本次系统结果、配置快照或应用构建信息观测 |
| `SuiteConfigured` | `suite-configured` | 来自已执行 `TestSuite` |
| `OperatorEntered` | `operator-entered` | 操作员在生成前显式提供 |
| `Unavailable` | `unavailable` | 未提供或未采集，不得推断 |

不可用字段保存空原值、`available=false` 和明确 `missingDisplay`。来源标签描述数据来源，不代表真实性等级；操作员输入不能冒充系统观测。

### 5.2 固定字段与来源优先级

| 报告字段 | 必填 | 来源规则 | 缺失显示 | 最大长度 |
|---|---:|---|---|---:|
| 项目名称 | 是 | 应用常量 `OMS555TV` | 不允许缺失 | 32 |
| 测试对象 | 是 | 已执行 suite 的 `name` | 空值为输入错误 | 256 |
| suite ID/name/tags/schema | 是 | 已执行 suite | 空值为输入错误 | 沿 Schema 上限 |
| 设备型号 | 否 | suite `device_model`，其次 `hardware`；均无时用操作员补充 | 未填写 | 256 |
| 测试台架 | 否 | suite `test_bench`，其次 `bench`；均无时用操作员补充 | 未填写 | 256 |
| 环境说明 | 否 | suite `environment_description`，其次 `hardware_scope`；均无时用操作员补充 | 未填写 | 512 |
| Firmware 版本 | 否 | 仅从本次通过断言的 Firmware major/minor 实际读取值提取 | 未采集 | 64 |
| Host 版本 | 否 | `applicationVersion` | 未采集 | 128 |
| 源码标识 | 否 | `sourceRevision` | 未采集 | 128 |
| 测试人员 | 否 | 仅 `operatorMetadata.tester` | 未填写 | 128 |
| Session ID | 否 | `suiteResult.sessionId` | 未采集 | 128 |
| 通信参数 | 否 | `connectionConfig` 快照 | 未采集 | 结构化 |

完整 suite metadata 还会按原 key/value 复制为 `suiteMetadata`，统一标记 `suite-configured`，供审计使用。报告层不把任意 metadata key 自动提升为系统观测字段。

### 5.3 操作员字段

`OperatorReportMetadata` 只允许 `tester`、`deviceModel`、`testBench` 和 `environmentDescription`。空白输入视为未提供；非空值保留原始内容，不读取或回退到 Windows 用户名。suite 已配置设备/台架/环境时，操作员值不覆盖，只用于补缺。

任何输入字段超过上表长度，或包含 U+0000，返回 `InvalidMetadata`；构建器不静默截断。

### 5.4 Firmware 版本提取

只有下列条件同时满足，才形成 `system-observed` Firmware 版本：

1. 对应 case/sequence step 为 `PASS`；
2. 存在 `PASS` 的结构化断言结果；
3. 实际结果来自覆盖 PDU 地址 39/40 的合法读请求；
4. major 和 minor 均来自 `actual` 的寄存器值或样本，而不是 expected、用例名称、文件名或日志文本。

多个合格读取必须得到一致的 major/minor；若冲突返回 `InconsistentFirmwareVersion`。只读到 minor、只存在 expected 或未通过断言时显示“未采集”。展示形式固定为十进制 `major.minor`。

## 6. 时间与通信参数

所有审计时间保留 `QDateTime` UTC 值和 ISO 8601 毫秒字符串；本地展示字符串使用输入的 `displayTimeZone` 转换，并包含明确偏移。不得使用构建机器当前区域设置决定格式。

通信快照包含端口、波特率、数据位、校验位、停止位、流控、Slave ID、默认响应超时和最大排队数。枚举使用稳定英文名；时间单位为 ms。

Host 的 RTT 是 API 观测值，不表示物理分析仪测量。文档模型保存可选纳秒值及确定性毫秒文本；缺失 RTT 保持 `optional`，不得显示为 0。

## 7. 汇总算法与状态校验

仅遍历不可变 case 终态计数：

- `total = PASS + FAIL + ERROR + SKIPPED`；
- `executed = PASS + FAIL + ERROR`；
- `passRateNumerator = PASS`；
- `passRateDenominator = PASS + FAIL + ERROR`；
- 分母为 0 时 `passRatePpm` 缺失，展示 N/A；否则使用整数 ppm 四舍五入；
- `duration` 直接复制 suite 运行结果。

所有加法和 ppm 乘法执行溢出检查；不变量失败返回 `SummaryOverflow` 或 `SummaryInvariantViolation`。

整体状态不得由报告覆盖，只复制 `suiteResult.status` 并按现有 Engine 规则校验：ERROR 高于 FAIL，非 `LoggingFailed` auxiliary error 产生 ERROR；否则 v1/v2 即使为空或全 SKIPPED 仍为 PASS；v3 全 SKIPPED 为 SKIPPED，存在 PASS 且无 FAIL/ERROR 时为 PASS。校验不一致返回 `SuiteStatusMismatch`，原结果不被修改。

## 8. 文档模型映射

### 8.1 suite 与 case

`ReportDocumentModel` 保存 run ID、schema、suite ID/name/description/tags、元数据、整体状态、UTC/本地时间、总耗时、汇总、case 明细、auxiliary error 和 SessionLog 工件。

每个 `ReportCase` 保存：

- ID、名称、类别、描述、环境、tags、声明类型和规范化类型；
- 终态、skip reason、开始/结束时间和耗时；
- expected/actual 摘要、断言差异、主错误和清理错误；
- 复合步骤、全部内存保留 attempt、稳定性统计、guided recovery 结构和证据保留摘要。

配置描述来自对应不可变 `TestCase`，执行字段来自同 ID 的 `TestCaseResult`，两者不得按 UI 行号或显示文本关联。

### 8.2 类型映射

- 基础请求：case → attempt 事务证据；写入验证/恢复按 attempt purpose 保留。
- `sequence`：保持 repetition、step ID/index、状态、expected/actual、attempt sequence 引用。
- `consistency`：保留聚合 actual samples，并保持每个采样 attempt。
- `stability`：保存统计、expected/actual、总/成功/失败/超时、RTT 统计和有界证据摘要。
- `guided_recovery`：保存原始人工动作、一次性 token、提示/动作时间、确认/取消、备注、观察 target/outcome、连续数、首个/稳定 RequestId、恢复时间和接线恢复提醒。人工 `confirm` 只是动作记录，不转换为 PASS 证据。

## 9. 事务和错误证据

每个内存保留 attempt 映射为 `ReportTransactionEvidence`：

- attempt sequence、purpose、step/repetition/retry、RequestId；
- 请求状态、开始/结束/耗时；
- TX/RX、CRC 状态、RTT；
- communication error category/code/exception/diagnostic；
- 关联的 case/step 由层级和 ID 明确保存。

TX/RX 格式固定为大写十六进制、字节间一个空格。事务存在但 `rxAdu` 长度为 0 时状态为 `Empty`；没有事务证据时状态为 `Missing`。两者不能共用空字符串语义。

断言差异逐项保存稳定 code、index、expected/actual、原始值、缩放分母、单位和原因。`TestError`、communication error 与 cleanup error 分开映射；清理失败不得被普通失败原因覆盖。

## 10. 证据保留与 SessionLog 工件

`ReportEvidenceRetention` 原样保存策略、总 attempt、内存保留/丢弃、总失败/保留失败、配置上限和说明，并验证：

- `retainedAttempts + droppedAttempts == totalAttempts`；
- `retainedAttempts == case.attempts.size()`；
- `retainedFailures <= totalFailures <= totalAttempts`。

稳定性 `BoundedRepresentative` 只表示 HTML 可展示内存保留证据，不能声称包含全部 attempt。完整事务若存在，只通过 `ReportSessionLogArtifact` 引用：session ID、原始路径、非负大小、大写 64 位 SHA-256 和 `available`。构建器不读取 JSONL、不计算哈希、不从日志重建结果。

`available=true` 时 path、session ID、size 和 SHA-256 必须完整有效；否则返回 `InvalidArtifact`。工件 session ID 必须与结果 session ID 一致。

## 11. `ReportError`

稳定错误码至少包含：

- `MissingSuite`
- `InvalidRunId`
- `NonTerminalStatus`
- `SuiteResultCountMismatch`
- `CaseIdentityMismatch`
- `SuiteStatusMismatch`
- `InvalidTimestamp`
- `InvalidDuration`
- `InvalidTimeZone`
- `InvalidMetadata`
- `SummaryOverflow`
- `SummaryInvariantViolation`
- `EvidenceInvariantViolation`
- `InvalidArtifact`
- `InconsistentFirmwareVersion`

错误包含 code、可定位 field path 和中文 diagnostic。报告错误是独立失败域，不写回或改变 `TestSuiteResult`。

## 12. 安全、兼容与性能

- v1/v2/v3 共享现有 `TestSuite`/`TestSuiteResult`，按 schema 和 case type 映射，不修改 Loader/Engine。
- 文档模型不保存 HTML，不提供“已转义”标志；TASK-025 必须统一转义 `<>&\"'`、换行和控制字符。
- 不读取环境用户名、主机名、任意文件内容或远程资源。
- 构建复杂度为 O(case + retained step/attempt/probe)，不扩展 stability 已丢弃证据。
- 本任务不新增第三方依赖。

## 13. 测试策略

纯数据测试至少覆盖：

1. PASS/FAIL/ERROR/SKIPPED 混合汇总、通过率和状态优先级；
2. 空套件、全 SKIPPED、分母为 0、非终态、suite/case 数量与 ID 错误；
3. UTC/指定时区、时间缺失/倒序、持续时间非法；
4. 四类元数据来源、suite 优先、operator 补缺、长度和 U+0000；
5. Firmware 合法提取、缺失、expected-only 和冲突；
6. 基础、sequence、consistency、stability、guided recovery 完整层级；
7. TX/RX 大写格式、空 RX、RTT 缺失、错误码、断言差异和清理错误；
8. 有界保留不变量和 SessionLog 工件字段/哈希/session 关联；
9. v1/v2/v3 兼容及全部既有 Host CTest、应用 smoke。

## 14. 评审记录

### 必须修复

无。

### 建议修复

无。报告模型不应依赖当前全局 `QCoreApplication`，否则离线/golden 测试不可复现；本规范将应用版本和源码标识改为显式输入快照。

### 可选优化

TASK-025 可在不改变本契约的前提下增加 HTML 排版、原子写入和安全文件名；原生 PDF、报告签名及上传仍不在 Phase 8 MVP 范围。
