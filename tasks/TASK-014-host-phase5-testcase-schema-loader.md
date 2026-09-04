# TASK-014 Host Phase 5 测试用例模型、JSON Schema 与断言

## 目标

建立版本化、可校验、可扩展的自动化测试用例与测试套件模型，实现严格 JSON 加载和无通信依赖的断言核心，使新增基础测试可以通过数据文件完成，而不修改 TestEngine 核心逻辑。

## 背景

PRD 要求测试步骤与预期结果配置化，并至少支持 NOT_RUN、RUNNING、PASS、FAIL、SKIPPED 和 ERROR。当前 `testcases/` 只有分类目录与 `.gitkeep`，`docs/test_cases.md` 中的 24 条记录仍是候选目录，仓库尚无正式 JSON Schema、加载器、规范化用例模型或断言实现。

Phase 5 不应直接从 UI 读取任意 JSON 并边执行边猜语义。本任务先固化输入契约和纯数据判断，后续 TASK-015 只消费已经校验和规范化的对象。

## 范围

- 实现前创建并评审 `specs/host_phase5_testcase_schema.md`。
- 定义版本化 `TestSuite`、`TestCase`、请求、预期、重试、超时、标签和元数据模型。
- 定义稳定测试状态：NOT_RUN、RUNNING、PASS、FAIL、SKIPPED、ERROR。
- 定义 Phase 5 支持的基础用例类型：
  - `read_register` 与 `read_registers`；
  - `write_register`；
  - `write_and_verify`；
  - `expect_exception`。
- 定义基础断言类型：相等、数值范围、寄存器序列、位掩码和期望 Modbus 异常。
- 明确所有 Modbus 地址均为 PDU 0-based，数量、原始值和异常码的合法范围。
- 输出机器可读的版本化 JSON Schema，例如 `testcases/schema/test-suite-v1.schema.json`。
- 实现 `TestCaseLoader`，把严格 JSON 转为规范化模型并返回结构化配置错误。
- 校验 schema 版本、必填字段、枚举、类型、范围、唯一 ID、用例类型与 request/expected 的组合。
- 对未知 schema 版本、未知用例/断言类型和不支持字段给出稳定错误，不静默忽略。
- 实现不依赖 `IModbusClient` 的断言核心，输入实际结果数据，输出 PASS/FAIL 和结构化差异。
- 建立有效/无效 JSON fixture，覆盖边界、错误路径和向前兼容拒绝行为。
- 提供中文示例套件，但不将其计入 Phase 6 的 20 条正式用例。

## 非范围

- 不实现 TestEngine 调度、通信请求、超时计时、重试执行或中止。
- 不实现 QWidget 自动化测试页面、真实 RS485 或 Firmware 修改。
- 不实现半自动人工步骤、稳定性长循环、Raw Frame、错误 CRC 注入。
- 不实现 HTML/PDF 报告、正式 20 条套件或报告元数据输入。
- 不引入第三方 JSON/Schema 框架；优先使用 Qt Core JSON 和项目内明确校验。

## 依赖

- `PROJECT_SPEC.md` 第 15～17 节和 Phase 5 要求。
- `docs/test_cases.md` 候选用例目录。
- TASK-004 的 PDU 地址、寄存器类型和阈值编码边界。
- TASK-007 的通信请求/错误类型可作为执行结果语义参考，但本任务不访问客户端。

## 实现门禁

1. Spec 必须先定义顶层 suite 与 case 的完整字段、默认值、禁止组合和版本升级策略。
2. `write_and_verify` 必须定义写前读取、写入、独立回读和可选恢复原值的声明方式，不得由引擎猜测。
3. 重试必须是显式配置，默认不重试；次数和适用错误需在 Schema 中有界。
4. timeout 必须定义为单次请求还是用例整体预算；复合用例需要分别记录两个口径。
5. FAIL（断言不满足）与 ERROR（配置、通信或引擎错误）的边界必须在模型中固化。
6. Schema 和 C++ 校验规则必须有一致性测试，不能出现文件声称合法但加载器拒绝的分叉。

## 实现要求

1. 严格使用 UTF-8 JSON；重复 ID、空 ID/名称、非法类型和数值溢出必须拒绝整个加载结果或按 Spec 返回明确策略。
2. 配置错误至少包含稳定错误码、JSON 路径、套件/用例 ID（若可得）和中文可展示诊断。
3. 地址范围计算使用扩展宽度，防止 `start + count - 1` 溢出。
4. 断言比较保留期望、实际、单位/原始值和失败原因，不只返回布尔值。
5. 测试用例模型与执行结果模型分离；加载后输入对象不可被引擎运行状态污染。
6. 用例顺序按 JSON 数组保持稳定；标签和 category 不决定执行顺序。
7. 未知 schema 版本必须拒绝，不得尝试按当前版本运行。
8. 错误文本用于展示，程序分支只依赖枚举和结构化字段。
9. `read_register` 作为需求示例和现有用例目录中的合法输入类型，必须被显式校验并规范化为 count=1 的读取语义；不得作为未声明别名静默兼容。

## 验收标准

1. Technical Spec 和 JSON Schema 已评审通过，字段、默认值和禁止组合无未决项。
2. Host 全新配置、构建和全部 CTest 通过，无 Phase 4 回归。
3. 有效 fixture 可稳定加载，并保持用例顺序、ID、类型、请求、预期、timeout 和 retry。
4. 缺失字段、错误类型、未知版本、重复 ID、非法地址/数量/值/异常码均返回结构化错误和 JSON 路径。
5. 各用例类型只接受匹配的 request/expected 组合；无关字段不被静默接受。
6. 相等、范围、序列、位掩码和异常断言均覆盖 PASS 与 FAIL，并保存结构化差异。
7. 边界测试覆盖寄存器数量 1/125、地址末端、地址溢出、阈值原始值和超时/重试上下限。
8. Schema 示例和 C++ Loader 的一致性由自动测试验证。
9. 测试不访问串口、真实硬件、QWidget 或文件系统外部状态。
10. 最终 Review 无必须修复项，允许进入 TASK-015。

## 测试要求

- 有效加载：最小套件、全部基础类型、可选字段、中文名称和标签。
- 无效加载：语法、版本、字段、枚举、重复 ID、范围、溢出和组合冲突。
- 断言：相等、范围、序列、位掩码、异常码及详细差异。
- Schema 一致性：示例和 fixture 与加载器采用相同约束。
- 回归：全部 Host CTest 和应用 smoke test。

## 当前状态

已完成（2026-09-04）。Technical Spec、v1 JSON Schema、严格 Loader、规范化模型、纯断言核心、中文示例和有效/无效 fixture 均已实现；全新 Host 配置、构建和 17/17 CTest 通过。测试未访问串口、真实硬件或 QWidget，最终 Review 无必须修复项，TASK-015 可进入实施。详见 `docs/test_results/task014_testcase_schema_loader.md`。
