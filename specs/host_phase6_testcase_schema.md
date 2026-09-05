# Host Phase 6 覆盖模型与测试 Schema v2 技术规范

> 状态：已评审并实现

## 1. 目的与边界

本文定义 TASK-017 的唯一输入契约：在完全保留 v1 行为的前提下，增加 Phase 6 所需的逐元素断言、受限 sequence、确定性预期超时、uint32 组合/单调性以及稳定性配置和汇总断言。

本任务只负责 JSON 到不可变规范化模型及纯数据断言，不执行 Modbus、不访问串口、QWidget 或真实硬件。sequence、expect_timeout、consistency 和 stability 的运行状态机由 TASK-018 实现；正式 20+ 套件和 UI 由 TASK-019 实现。

## 2. 版本与兼容策略

- `schema_version=1` 继续严格按 `test-suite-v1.schema.json`、既有 Loader 默认值和既有组合约束加载。
- `schema_version=2` 只按 `test-suite-v2.schema.json` 和本规范加载；不得把 v2 字段放宽到 v1。
- 其他版本整体拒绝并返回 `UnknownSchemaVersion`。
- 两个版本均要求 UTF-8、无 BOM、顶层对象、`additionalProperties=false`、全有或全无加载。
- Loader 在读取版本后选择显式版本分支；成功后 Engine 只消费 `TestSuite/TestCase` 规范化模型，不解释原始 JSON。
- v2 的公共字符串、标签、metadata、地址、功能码、重试和基础请求继续沿用 v1 上限。

## 3. v2 公共模型

v2 顶层字段与 v1 相同。每个用例除 v1 公共字段外必须声明：

| 字段 | 约束 |
|---|---|
| `environment` | `real_rs485`、`fake` 或 `both`；用于覆盖审计，不在运行时隐式改写后端 |
| `type` | v1 五种类型，或 `sequence`、`expect_timeout`、`consistency`、`stability` |

v2 基础用例继续使用 `request/expected/timeout/retry`。`read_register` 仍规范化为 `ReadRegisters(count=1)`；v2 基础类型的通信语义不在 TASK-017 中改变。

## 4. 逐元素断言

`elements` 用于一次 0x03 返回多个动态寄存器：

```json
{
  "type": "elements",
  "items": [
    {"index": 0, "type": "range", "min": -400, "max": 1250,
     "representation": "int16", "decimal_places": 1, "unit": "℃"}
  ]
}
```

- `items` 数量必须恰好等于 `request.count`；索引唯一且完整覆盖 `0..count-1`。
- 单项只允许 `equals/range/bitmask`。`bitmask` 固定为 `uint16` 且 `decimal_places=0`。
- `representation` 为 `uint16`（默认）或 `int16`；`decimal_places` 为 `0..6`，表示解释值的十进制定点小数位，不使用二进制浮点。
- 断言先保留原始 `quint16`，再进行符号解释；比较使用解释后的定点整数，工程值以 `numerator / 10^decimal_places` 保存和展示。
- 差异包含元素索引、原始值、解释值、定点分母、期望边界和单位。

## 5. sequence

```json
{
  "type": "sequence",
  "environment": "fake",
  "repeat_count": 2,
  "failure_policy": "continue_on_failure",
  "timeout": {"request_ms": 500, "case_ms": 10000},
  "steps": [
    {"id": "bad-address", "type": "expect_exception", "delay_before_ms": 0,
     "request": {"function": 3, "address": 41, "count": 1},
     "expected": {"type": "modbus_exception", "code": 2}}
  ]
}
```

- `steps` 为 `1..64`，step ID 在用例内唯一；`repeat_count` 为 `1..100`，默认 1。
- `delay_before_ms` 为 `0..60000`，默认 0；禁止脚本、表达式和无限循环。
- `failure_policy` 为 `stop_on_failure`（默认）或 `continue_on_failure`。
- sequence 只允许只读、期望异常和确定性预期超时步骤；写入仍使用顶层 `write_and_verify`，避免复合流程绕过安全恢复。
- 每步继承用例的 `request_ms`，可有独立 retry；`case_ms` 为所有重复、延迟、attempt 与清理的硬总预算，范围 `1..3600000 ms`。
- 配置预算可以小于理论最坏执行时间；TASK-018 必须在预算耗尽时停止启动新步骤并产生明确 ERROR，不能延长预算。
- `stop_on_failure` 在首个 FAIL/ERROR 后停止后续业务步骤；`continue_on_failure` 保留失败并继续，但清理错误始终具有更高优先级。

## 6. 确定性预期超时

`expect_timeout` 的 `expected` 固定为：

```json
{"type": "response_timeout", "source": "deterministic_no_response"}
```

并且用例/步骤必须同时满足 `environment="fake"`、`fault="no_response"`、无 retry。只有结构化 `ResponseTimeout` 才能判 PASS；正常响应或远端异常为 FAIL，Cancelled、ClosedByPeer、串口、连接、CRC 或其他错误为 ERROR。该模型不能用于真实链路的偶发无响应，也不能把任意通信故障改判为 PASS。

## 7. uint32 数据一致性

`consistency` 固定读取两个连续寄存器，并显式配置采样：

```json
{
  "type": "consistency",
  "consistency": {"sample_count": 2, "interval_ms": 100},
  "request": {"function": 3, "address": 30, "count": 2},
  "expected": {"type": "uint32", "word_order": "low_word_first",
               "comparison": "non_decreasing", "min": 0, "max": 4294967295,
               "decimal_places": 0, "unit": "s"}
}
```

- 只接受 `low_word_first`：`value = low | (high << 16)`；其他字序在 Loader 阶段拒绝。
- `sample_count=1..16`，`interval_ms=0..60000`；`range` 可单样本，`non_decreasing/strictly_increasing` 至少两个样本。
- 每个组合值都先检查无符号 `min..max`，再检查非递减或严格递增关系。
- `timeout.case_ms` 必须覆盖所有请求超时和采样间隔的理论预算，且不超过 3600000 ms。
- 结果保留每个样本的两个原始寄存器、组合后的 uint32、定点工程值和首个单调性差异。

## 8. stability

```json
{
  "type": "stability",
  "environment": "real_rs485",
  "request": {"function": 3, "address": 0, "count": 5},
  "timeout": {"request_ms": 500, "case_ms": 600500},
  "stability": {
    "duration_ms": 600000,
    "interval_ms": 1000,
    "minimum_successes": 594,
    "allowed_failure_rate_ppm": 10000,
    "evidence_sample_limit": 64
  },
  "expected": {
    "type": "stability_summary",
    "min_total": 594,
    "min_successes": 594,
    "max_failures": 6,
    "max_timeouts": 6,
    "max_failure_rate_ppm": 10000,
    "rtt_ms": {"require_valid_samples": true, "minimum_sample": 0,
               "maximum_average": 500, "maximum_sample": 1000}
  }
}
```

- `duration_ms=600000..86400000`，支持 10 分钟、1/8/24 小时且不能无限运行。
- `interval_ms=10..60000`，定义为相邻请求开始时间的最小间隔；不允许并发。请求超过计划间隔时，下一请求在前一请求完成后尽快开始，不补发积压请求。
- `case_ms` 必须至少为 `duration_ms + request_ms`，最多 86460000 ms；duration 到达后不再启动请求，最后在途请求仍受 request timeout 限制。
- 理论最大启动数为 `ceil(duration_ms / interval_ms)`；`minimum_successes` 和 `expected.min_total/min_successes` 不得超过该值。
- `allowed_failure_rate_ppm=0..1000000`，判定采用 `failed * 1000000 <= total * threshold_ppm`，等于阈值 PASS；计数范围保证 64 位乘法不溢出。
- `evidence_sample_limit=8..256`，默认 64。内存只保留首尾、失败和有界抽样证据；每个 request/attempt 的完整证据写入滚动 JSONL。TASK-018 负责具体保留算法，TASK-017 只固化上限。
- 汇总断言同时检查总数、成功、失败、超时、失败率和 RTT。没有有效 RTT 且 `require_valid_samples=true` 时 FAIL；缺失 RTT 不按 0 参与最小/平均/最大值。

## 9. 默认值、组合约束与错误

- v2 普通请求默认 timeout/retry 与 v1 相同；sequence、consistency、stability 必须显式提供包含 `request_ms/case_ms` 的 timeout。
- unknown field/type/assertion、错误类型、越界、地址末端溢出继续使用既有稳定错误码。
- 重复元素索引或 step ID 使用 `DuplicateId`；非法环境、字序、失败策略、fault/expected 组合、预算和统计冲突使用 `InvalidCombination` 或 `OutOfRange`，并指向具体 JSON Pointer。
- 加载失败时 `suite` 必须为空；错误携带 suite/case ID 和中文诊断。
- v1/v2 模型均不含运行状态，执行过程不得写回输入对象。

## 10. 纯数据断言

`evaluateAssertion` 扩展接受寄存器序列、寄存器样本序列、Modbus 异常或稳定性汇总四种互斥输入。新增稳定差异码覆盖元素数量/索引、uint32 字数/范围/单调性、稳定性计数、失败率和 RTT；不得解析展示文本做判断。

稳定性汇总中：`failures` 包含非 timeout 失败，`timeouts` 单独计数，失败率分子为 `failures + timeouts`，并要求 `total = successes + failures + timeouts`。统计不变量不成立时断言 FAIL 并保留结构化差异。

## 11. 验收与后续边界

1. v1 fixture、中文示例和 Phase 5 套件加载结果不变。
2. v2 fixture 覆盖所有新增模型、默认值、上限和主要非法组合。
3. Schema 常量、Loader 常量、示例和规范化结果由自动测试逐项核对。
4. 逐元素、int16/十进制缩放、uint32 低高字、单调性以及稳定性失败率等于/超过阈值均由纯数据测试验证。
5. TASK-017 不执行长时测试；10 分钟至 24 小时仅作为配置数据验证，因此不触发真实等待。
6. 完整需求—环境—证据路径见 `docs/phase6_coverage_matrix.md`；完成本任务只允许进入 TASK-018，不代表 Phase 6 套件或实机验收完成。
