# Host Phase 5 测试用例模型、JSON Schema 与断言技术规范

## 1. 目的与边界

本文定义 TASK-014 的唯一输入契约：版本化测试套件、规范化测试用例、严格 JSON 加载错误和无通信依赖的断言结果。后续 `TestEngine` 只能消费成功加载后的模型，不得在运行时重新解释 JSON。

本阶段不执行 Modbus 请求，不管理 Testing owner，不访问串口、QWidget 或 Firmware，也不生成报告。

## 2. 版本与兼容策略

- 顶层 `schema_version` 必须为整数 `1`。
- 未知版本整体拒绝，错误码为 `UnknownSchemaVersion`，不得降级或猜测。
- v1 所有对象默认 `additionalProperties=false`；未知字段整体拒绝。
- 后续兼容字段只能通过新 schema 版本引入。`metadata` 是明确的字符串扩展字典，不属于未知字段绕过通道。
- 文件必须是无损 UTF-8 JSON；UTF-8 BOM、非法 UTF-8、尾随内容和语法错误均拒绝。
- 加载采用全有或全无策略：存在任一配置错误时不返回部分套件。

机器可读契约位于 `testcases/schema/test-suite-v1.schema.json`。标准 JSON Schema 负责结构、类型、枚举和静态范围；地址末端、范围上下界、位掩码子集、跨字段数量等关系由文件中的 `x-oms555tv-semantic-constraints` 和 C++ Loader 共同定义。测试必须同时证明 Schema 常量与 Loader 常量一致，并验证所有示例与 fixture。

## 3. 顶层 TestSuite

| 字段 | 类型 | 必填 | 约束/默认值 |
|---|---|---|---|
| `schema_version` | integer | 是 | 仅 `1` |
| `id` | string | 是 | trim 后 1～64 字符；`[A-Za-z0-9][A-Za-z0-9._-]*` |
| `name` | string | 是 | trim 后 1～128 字符 |
| `description` | string | 否 | 最多 1024 字符，默认空 |
| `tags` | string array | 否 | 最多 32 项；每项 trim 后 1～32 字符；区分大小写且不得重复 |
| `metadata` | object | 否 | 最多 32 项；键满足 ID 规则；值为最多 256 字符的字符串 |
| `cases` | TestCase array | 是 | 0～1000 项，保持 JSON 顺序；用例 ID 在套件内唯一 |

空套件合法，由 TASK-015 决定执行结果。

## 4. TestCase 公共字段

| 字段 | 类型 | 必填 | 约束/默认值 |
|---|---|---|---|
| `id` | string | 是 | 同套件 ID 规则，套件内唯一 |
| `name` | string | 是 | trim 后 1～128 字符 |
| `category` | string | 是 | trim 后 1～64 字符，不决定执行顺序 |
| `description` | string | 否 | 最多 1024 字符，默认空 |
| `type` | enum | 是 | 见第 5 节 |
| `enabled` | boolean | 否 | 默认 `true` |
| `tags` | string array | 否 | 规则同套件标签 |
| `request` | object | 是 | 必须与 `type` 匹配 |
| `expected` | object | 是 | 必须与 `type`、数量匹配 |
| `timeout` | object | 条件 | 见第 7 节 |
| `retry` | object | 否 | 省略即不重试，见第 8 节 |

加载模型同时保存 `declaredType` 和 `type`。`read_register` 的 `declaredType` 为 `ReadRegister`，但规范化后的可执行 `type` 为 `ReadRegisters` 且 `count=1`。其余类型两个值相同。运行状态不写回 `TestCase`。

## 5. 用例类型与请求

所有地址均为 Modbus PDU 0-based，范围 `0..65535`，不得使用 40001 风格编号。

### 5.1 read_register

```json
{"function": 3, "address": 0, "count": 1}
```

- 只允许字段 `function/address/count`。
- `function` 必须为 3，`count` 必须显式为 1。
- `expected` 可为 `equals`、`range`、`register_sequence` 或 `bitmask`；序列长度必须为 1。

### 5.2 read_registers

请求字段同上，`count` 为 `1..125`。使用 32 位扩展宽度校验 `address + count - 1 <= 65535`。

- `count=1` 时允许四种寄存器断言。
- `count>1` 只允许 `register_sequence`，且期望序列长度必须等于 `count`。

### 5.3 write_register

```json
{"function": 6, "address": 9, "value": 600}
```

- `value` 是原始寄存器值，范围 `0..65535`。
- 只允许 `equals` 断言、`uint16` 表示，且期望值必须等于写入值；断言对象用于验证 0x06 回显语义。

### 5.4 write_and_verify

```json
{
  "function": 6,
  "verify_function": 3,
  "address": 9,
  "value": 600,
  "read_before_write": true,
  "restore_original": true
}
```

- 所有六个字段都必须显式提供，不得由引擎猜测。
- `function=6`，`verify_function=3`；独立回读数量固定为 1。
- `read_before_write=true` 表示写入前独立读取原值。
- `restore_original=true` 时 `read_before_write` 必须为 true；主断言结束后独立写回原值。恢复操作不替代主回读。
- 只允许 `equals`、`uint16`，且期望值必须等于写入值。
- 必须显式提供复合 timeout。

### 5.5 expect_exception

支持两种请求形态：

- 读：`function=3`、`address`、`count=1..125`，并校验地址末端。
- 写：`function=6`、`address`、`value=0..65535`。

只允许 `modbus_exception` 断言。异常码范围为 `1..255`；本地超时、串口错误或正常响应均不是期望异常。

## 6. 断言模型

### 6.1 equals

字段：`type="equals"`、`value`、可选 `representation`、可选 `unit`。

### 6.2 range

字段：`type="range"`、`min`、`max`、可选 `representation`、可选 `unit`；必须满足 `min <= max`。

### 6.3 register_sequence

字段：`type="register_sequence"`、`values`、可选 `representation`、可选 `unit`。数组 1～125 项并与读取数量相同。

### 6.4 bitmask

字段：`type="bitmask"`、`mask`、`value`、可选 `unit`。两者范围均为 `0..65535`，并要求 `(value & ~mask) == 0`。判定公式为 `(actual & mask) == value`。

### 6.5 modbus_exception

字段：`type="modbus_exception"`、`code=1..255`。

`representation` 取值：

- `uint16`（默认）：断言数值范围 `0..65535`；
- `int16`：把实际寄存器按二补码解释，断言数值范围 `-32768..32767`。

`unit` 仅用于诊断展示，trim 后最多 16 字符，不参与数值换算。Schema 中的数值始终为整数，写入原始值不接受负数或小数。

断言输入 `ActualResult` 只能是寄存器序列或 Modbus 异常之一。输出 `AssertionResult` 包含 PASS/FAIL、期望/实际摘要、期望值、解释后的实际值、原始 `quint16`、单位以及结构化差异。实际类型不匹配、长度不匹配、数值不等、越界、掩码不匹配和异常码不匹配分别使用稳定差异码，不用诊断文本驱动程序分支。

## 7. Timeout

普通用例可省略 `timeout`：规范化为单请求 `request_ms=500`，用例总预算 `case_ms=500`。若提供，允许：

```json
{"request_ms": 500, "case_ms": 1000}
```

- `request_ms`：每次 Modbus 请求的响应超时，`1..60000 ms`。
- `case_ms`：整个用例（含所有 attempt 和步骤）的预算，`1..300000 ms`，且不得小于 `request_ms`。
- 普通用例省略 `case_ms` 时规范化为 `request_ms`。
- `write_and_verify` 必须同时显式提供两个字段，分别记录单次请求和复合用例总预算。

TASK-014 只校验和保存两个口径，不启动计时器。

## 8. Retry

省略 `retry` 表示 `max_retries=0` 且不重试。若提供，必须包含：

```json
{"max_retries": 2, "on_errors": ["timeout", "crc"]}
```

- `max_retries` 范围 `1..3`，表示首次 attempt 之外最多增加的 attempt 数。
- `on_errors` 非空、唯一，可选 `timeout/connection/serial/crc/protocol`。
- 重试作用于失败的单个通信步骤；断言 FAIL、配置错误、远端 Modbus 异常和中止不重试。
- `expect_exception` 收到非期望异常属于断言 FAIL，不按 retry 处理。
- 用例总预算始终高于 retry；预算耗尽后不得再开始 attempt。

## 9. 状态与错误边界

稳定状态枚举为 `NOT_RUN/RUNNING/PASS/FAIL/SKIPPED/ERROR`。

- `FAIL`：通信产生了可供目标断言判断的结果，但断言不满足。
- `ERROR`：配置非法、无处理器、通信失败、超时、中止清理异常或引擎内部错误。Loader 失败发生在执行前，后续由引擎映射为配置 ERROR。
- `SKIPPED`：禁用或用户明确跳过。
- `NOT_RUN/RUNNING` 仅为过程状态，不是报告最终成功状态。

配置错误至少包含：稳定 `ConfigErrorCode`、JSON Pointer 风格路径、可得的 suite/case ID 和中文诊断。Loader 可返回多个错误，但任何错误都会使 `suite` 为空。错误文本仅展示，逻辑只依赖枚举和路径。

稳定错误码包括：`InvalidUtf8`、`JsonSyntax`、`RootNotObject`、`UnknownSchemaVersion`、`MissingField`、`UnknownField`、`WrongType`、`EmptyString`、`InvalidIdentifier`、`OutOfRange`、`UnknownCaseType`、`UnknownAssertionType`、`DuplicateId`、`DuplicateTag`、`AddressRangeOverflow`、`InvalidCombination`。

## 10. 不变量与验收映射

1. Loader 成功结果中的字符串均已 trim，默认值均已填充，顺序不变。
2. Loader 成功后无需再次检查未知字段、地址溢出、类型组合或默认值。
3. 模型不含可变执行状态；执行结果使用独立类型。
4. JSON Schema、示例、fixture 和 Loader 使用同一版本、枚举和边界常量。
5. 单元测试覆盖五种输入类型、五种断言的 PASS/FAIL、1/125 数量、65535 末端、溢出、0/65535 原始值、timeout/retry 上下限以及所有主要错误路径。
6. 本任务所有测试均为确定性纯软件测试，不访问串口、真实硬件或 QWidget，不包含超过 5 分钟的步骤。
