# TASK-014 测试用例 Schema、Loader 与断言验证记录

## 1. 验证范围

本记录验证 TASK-014 的纯软件输入契约与断言核心：

- v1 `TestSuite` / `TestCase` / request / expected / timeout / retry 模型；
- 严格 UTF-8 JSON、未知版本/字段和结构化配置错误；
- `read_register`、`read_registers`、`write_register`、`write_and_verify`、`expect_exception`；
- equals、range、register_sequence、bitmask、modbus_exception 的 PASS/FAIL 与结构化差异；
- Schema、示例、有效/无效 fixture 和 C++ Loader 常量一致性；
- Phase 4 Host 全量回归与应用 smoke test。

本任务不执行 TestEngine，不访问串口、真实 RS485、开发板或 QWidget。中文示例和 fixture 不计入 Phase 6 正式 20 条用例。

## 2. 环境

- 日期：2026-09-04
- 系统：Windows 11 x64
- Qt：6.8.3 `msvc2022_64`
- 编译器：MSVC 19.51.36256.0
- 生成器：Ninja
- 构建目录：`build-host-task014`
- 构建类型：Debug

## 3. 执行结果

全新配置、完整构建成功。随后执行：

```powershell
ctest --test-dir build-host-task014 --output-on-failure
```

最终结果为 17/17 通过，总耗时 6.05 秒。新增测试：

| CTest | 结果 | 覆盖 |
|---|---|---|
| `host.testing.testcase_loader` | PASS | 有效/无效 fixture、默认值、顺序、规范化、错误码/路径、Schema 常量与中文示例 |
| `host.testing.assertions` | PASS | 五类断言 PASS/FAIL、类型/长度差异、期望/实际/单位/原始值保留 |

原有日志、通信、设备编解码、监控、配置、诊断、UI 和应用 smoke 共 15 项全部通过，无 Phase 4 回归。

## 4. 边界与错误证据

- 数量：1 和 125 接受，0 和 126 拒绝。
- 地址：`address=65535,count=1` 与 `address=65411,count=125` 接受；末端超过 65535 返回 `AddressRangeOverflow`。
- 原始值：0/65535 接受，负数和 65536 拒绝；`int16` 断言覆盖 -32768..32767。
- timeout：`request_ms=1..60000`、`case_ms=1..300000`；总预算小于单请求超时拒绝。
- retry：省略即 0 次；显式 1..3 次和五类允许错误；未知或重复错误类型拒绝。
- 配置：非法 UTF-8/BOM、JSON 语法、顶层类型、缺失/错误字段、未知版本/类型、重复 ID/标签和非法组合均使整个加载结果失败，并返回稳定错误码与 JSON 路径。
- 写回读：`write_and_verify` 显式声明预读、独立回读、是否恢复原值和复合 timeout；恢复原值要求预读。

## 5. Review 结论

最终 Review 已检查正确性、Task/Spec/Architecture 一致性、边界、溢出、未知字段、模型与执行状态分离、断言证据完整性和回归范围。

- 必须修复：无。
- 建议修复：无阻塞项。
- 可选优化：后续 TASK-015 可在不改变 v1 JSON 契约的前提下增加处理器注册和不可变执行结果；TASK-016 再接入文件选择与 UI 展示。

结论：TASK-014 验收通过，可以进入 TASK-015。全程没有执行预计超过 5 分钟的验证步骤。
