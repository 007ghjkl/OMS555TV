# Host Phase 7 引导式测试模型与 Schema v3 技术规范

## 1. 文档状态

- 对应任务：`TASK-021 Host Phase 7 引导式测试模型与 Schema v3`
- 状态：已评审，可实施
- 日期：2026-09-05
- 范围：输入 Schema、规范化数据模型、动作校验、结果证据与纯数据测试

本规范不实现运行协调器、按钮、对话框、串口操作或真实硬件测试。TASK-022 必须消费本规范的数据模型，不得在 QWidget 内另建人工流程或直接判定结果。

## 2. 需求结论与方案评审

### 2.1 采用方案

Schema v3 是独立的引导式测试套件，只接受 `guided_recovery` 用例。v1/v2 自动套件继续按原版本加载，语义与正式 Phase 5/6 JSON 均不改变。

每条 `guided_recovery` 固定为四个有界步骤：

1. `operator_prompt` / `disconnect_rs485`；
2. `observe_outage`；
3. `operator_prompt` / `reconnect_rs485`；
4. `observe_recovery`。

固定顺序比任意步骤图更适合当前唯一目标——RS485 物理断线恢复：它能在进入 UI/执行层之前排除漏接线提示、先观察后确认、无界循环和写操作混入等配置错误。

### 2.2 未采用方案

- 在 v2 原文件上扩展：会改变已验收契约，不采用。
- 允许 v3 混合自动与引导用例：控制器所有权和中止语义会变得含糊；自动用例继续使用 v1/v2 即可，不采用。
- 通用脚本或任意动作图：扩大攻击面且无法静态审计，不采用。
- 用诊断文本判断断线/恢复：文本不稳定且无法形成结构化证据，不采用。

评审未发现阻塞性未决项。

## 3. Schema 与加载规则

文件为 `testcases/schema/test-suite-v3.schema.json`，采用 JSON Schema Draft 2020-12。输入必须为无 BOM 的 UTF-8，所有对象均使用 `additionalProperties=false`。Loader 采用全有或全无策略：任一根字段、用例或步骤无效时不返回部分套件。

顶层字段：

| 字段 | 约束 |
|---|---|
| `schema_version` | 固定为 `3` |
| `id` | 1～64 个标识符字符 |
| `name` | 1～128 字符 |
| `description` | 可选，最多 1024 字符 |
| `tags` | 可选，最多 32 个且不重复 |
| `metadata` | 可选，最多 32 项 |
| `cases` | 1～1000 条 `guided_recovery` |

v1、v2、v3 由 Loader 显式分派；其他版本返回 `UnknownSchemaVersion`。v3 不接受 v1/v2 用例类型，避免调用方误把引导套件交给既有自动执行器。

## 4. 引导用例模型

### 4.1 通用字段

`guided_recovery` 包含稳定 ID、中文名称、类别、描述、标签、启用状态、环境、超时和步骤。环境只允许：

- `real_rs485`：正式物理链路；
- `fake`：确定性测试。

`both` 被拒绝，因为人工物理动作不能隐式降级为 Fake。`timeout.request_ms` 为 1～60000 ms，`timeout.case_ms` 为 1～900000 ms，且总预算必须覆盖四步等待/deadline 上限之和。

### 4.2 人工提示步骤

人工步骤包含：

- 唯一 `id`；
- `purpose`：`disconnect_rs485` 或 `reconnect_rs485`；
- `title`、中文 `instruction`、`safety_notice`；
- `allowed_actions`：必须且只能各包含一次 `confirm` 和 `cancel`；
- `wait_timeout_ms`：1000～300000 ms；
- 非空 `cancel_recovery_instruction`。

`confirm` 仅表示操作员声明动作已完成，不是物理状态证据。`cancel` 结束用例，但若链路可能处于断开状态，结果必须设置 `recoveryInstructionRequired=true` 并保留接线恢复指引。

### 4.3 自动观察步骤

观察步骤包含唯一 ID、安全读探测、实际请求启动间隔、deadline、连续命中次数和可选业务断言：

- `probe.function` 固定为 0x03；
- `address` 为 0～65535，`count` 为 1～125，地址区间不得溢出；
- `interval_ms` 为 50～10000；
- `deadline_ms` 为 100～120000，并覆盖请求 timeout；
- `consecutive_matches` 为 1～20，且不得超过 `ceil(deadline/interval)`；
- `observe_recovery.business_assertion` 可选，当前只支持单寄存器 `equals` 或 `range`；
- `observe_outage` 禁止业务断言。

正式 RS485 引导用例中禁止 0x06、写前读、恢复写、fault、脚本、Shell 和其他可执行字段。

## 5. 结构化判定

### 5.1 通信中断

只有结构化 `CommunicationError.code == ResponseTimeout` 才计一次中断命中。连续命中达到配置值后，中断观察成功。以下情况不得作为成功断线证据：

- 用户取消或请求被调用方取消；
- 串口权限错误；
- 端口消失、未连接或串口资源错误；
- CRC、协议或远端异常；
- 任意诊断字符串匹配。

非超时但可继续的响应会把连续计数清零；致命通信错误按第 7 节进入 ERROR。

### 5.2 通信恢复

一次恢复命中必须同时满足：

1. 请求成功并包含合法 0x03 响应及完整事务证据；
2. 若配置了业务断言，结构化寄存器值满足该断言。

失败、超时或业务断言不匹配会把连续成功数清零；达到连续次数后才确认稳定恢复。人工“已重连”确认本身不能产生 PASS。

## 6. 状态机与动作令牌

唯一正常路径：

```text
IDLE
  -> WAITING_FOR_DISCONNECT_CONFIRMATION
  -> OBSERVING_OUTAGE
  -> WAITING_FOR_RECONNECT_CONFIRMATION
  -> OBSERVING_RECOVERY
  -> FINISHED
```

任一步骤只能转入下一个规定状态或 `FINISHED`。从开始运行至终态持续持有 Testing owner；人工等待期间不发送请求。每个提示生成不可复用的一次性 token，动作命令必须同时匹配：

- run ID；
- case ID；
- step ID；
- token；
- 当前允许动作。

非等待状态、跨运行、跨用例、跨步骤、空/错误 token、已消费 token、未允许动作均被结构化拒绝，且校验函数不修改上下文。TASK-022 只有在校验成功后才能原子消费 token 并转换状态。

## 7. 唯一终态、状态与优先级

终态原因与用例状态映射：

| 终态原因 | 状态 | 含义 |
|---|---|---|
| `pass` | PASS | 中断与稳定恢复均有自动证据 |
| `outage_not_detected` | FAIL | 中断观察 deadline 内未满足连续超时 |
| `recovery_timeout` | FAIL | 恢复观察 deadline 内未满足连续合法响应 |
| `operator_cancelled` | SKIPPED | 操作员在人工提示处取消 |
| `operator_timeout` | ERROR | 人工等待超过上限 |
| `fatal_communication_error` | ERROR | 权限、端口、串口、协议状态等致命错误 |
| `user_aborted` | ERROR | 整套运行被显式中止 |
| `internal_error` | ERROR | 不变量、调度或内部错误 |

同一事件批次出现多个候选终态时，优先级从高到低固定为：

`internal_error > user_aborted > operator_cancelled > fatal_communication_error > operator_timeout > outage_not_detected > recovery_timeout > pass`。

应用线程按该规则选择一次终态；进入 `FINISHED` 后不得被迟到响应或迟到命令覆盖。

## 8. 时间语义

- UTC：只记录提示展示、动作确认/取消、观察开始/结束和请求审计时间；
- 单调时钟：计算人工等待、观察 deadline、请求间隔、用例总预算和恢复时间；
- 调度串行执行且不追赶积压。

恢复时间起点是重连提示的有效 `confirm` 单调时刻。保存两个不可混用口径：

- `toFirstSuccessfulResponse`：到首个满足合法响应与可选业务断言的请求；
- `toStableRecovery`：到连续成功次数全部满足。

首个成功早于确认、稳定成功早于首个成功、缺失时间点或差值超过 `qint64` 毫秒范围均拒绝，不生成截断、负值或溢出的时长。

## 9. 不可变结果与 Phase 8 交接

结果通过既有 `TestResultManager` 的复制发布方式形成不可变快照。`GuidedRecoveryCaseResult` 直接保存：

- 最终状态与结构化终态原因；
- 每个人工步骤的 run/case/step/token、展示 UTC、动作、动作 UTC、单调等待时长和备注；
- 每个观察步骤的目标、结果、UTC/单调时长、连续进度；
- 首个与稳定命中的 RequestId；
- 每次探测的 RequestId、TX、RX、RTT、结构化结果与错误；
- 两种恢复耗时；
- 物理链路是否已由软件确认恢复；
- 取消后是否仍需恢复接线及明确指引。

Phase 8 只能消费这些结构化字段，不得重新解释 UI 文本或日志字符串。完整事务仍写入 JSONL；内存结果的保留策略由 TASK-022 在不丢失关键命中/失败证据的前提下确定。

## 10. 错误与取消清理

人工取消、人工超时、中断未检测、恢复超时、致命通信错误、整套中止与内部错误必须分别保留。终态后协调器取消在途请求、忽略迟到回调并释放 Testing owner；不得自动 close/open 串口，不得隐藏执行自动重连。

如果取消时恢复尚未被自动观察确认，`physicalLinkRestored=false`，并显示/记录对应人工步骤的 `cancel_recovery_instruction`。释放 owner 不等于物理链路安全恢复。

## 11. 测试与验收

TASK-021 的自动测试只使用 JSON、纯数据模型和确定性单调时间，不访问 QWidget、串口或开发板。至少覆盖：

- v1/v2 全部既有 fixture 与 Phase 5/6 正式套件兼容；
- v3 有效样例、上限边界及 Schema 常量一致性；
- 未知步骤/动作、缺失恢复指引、非法 deadline、重复 step ID、不安全写探测；
- run/case/step/token 的 stale、重复与跨运行拒绝；
- 两种恢复耗时、零时长、逆序和溢出；
- 固定终态优先级；
- 全新 Host 配置、构建、全部 CTest 和应用 smoke test。

真实 RS485 拔插与长时间测试均不属于本任务。
