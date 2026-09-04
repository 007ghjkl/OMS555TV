# TASK-015 TestEngine 执行核心测试记录

## 1. 结论

TASK-015 已完成。Technical Spec、执行状态机、处理器注册边界、四类基础处理器、不可变结果快照、超时/重试、中止、复合恢复、TEST 日志和 Testing owner 清理均通过确定性测试。最终 Review 无必须修复项，可以进入 TASK-016。

本记录只证明 Fake Modbus Client 下的 Host 执行核心，不代表自动化测试 UI 或真实 RS485 测试套件已通过。

## 2. 环境

- 日期：2026-09-05
- 系统：Windows
- 编译器：MSVC 19.51.36256 x64
- Qt：6.8.3 `msvc2022_64`
- 构建系统：CMake + Ninja，Debug
- 全新构建目录：`build-host-task015`
- 硬件：未使用
- 时间控制：`ManualScheduler` 虚拟时间，无 `sleep()`

## 3. 实施范围

- `specs/host_phase5_test_engine.md` 固化 Engine/Suite/Case/Attempt 状态、错误优先级及 owner 责任边界。
- `TestHandlerRegistry` 按规范化类型创建独立处理器，中心循环不包含类型分支。
- `TestEngine` 严格要求 AppState=`TESTING` 且 owner=`Testing`，串行执行并在结束后等待 `stopTesting()` 完成。
- `TestResultManager` 通过复制替换发布 `shared_ptr<const TestSuiteResult>`，保留终态历史。
- 每个已接受 RequestId 生成一条独立 attempt，保存完整 `ModbusRequestResult` 与 TX/RX/RTT/结构化错误。
- `write_and_verify` 覆盖写前读取、写、独立回读、原值恢复以及恢复失败提升 ERROR。
- TEST 日志引用 run/suite/case/RequestId/status，不重复写入通信 TX/RX。

## 4. 自动测试覆盖

- 状态与所有权：非法状态拒绝、空套件、重复开始、正常释放并返回 `CONNECTED_IDLE`。
- 顺序与选择：单条、批量、禁用、未选择、显式跳过及 JSON 顺序。
- 四类处理器：`read_registers`、`write_register`、`expect_exception`、`write_and_verify` 各自覆盖 PASS、FAIL、ERROR。
- timeout/retry：允许错误重试、零重试、不同 RequestId、独立 attempt、用例总预算截止。
- 中止：当前 pending 请求取消、剩余用例 SKIPPED、复合用例中止后继续恢复原值。
- 清理：回读不一致、恢复成功、恢复失败提升 ERROR、owner 释放。
- 结果与日志：不可变运行中快照、终态历史、断言差异、完整事务证据和 TEST 日志关联一致。
- 日志错误：作为附加错误保存，不改变测试断言结果。

## 5. 构建与回归结果

全新配置和完整 Host 构建成功。随后运行全部 CTest：

```text
100% tests passed, 0 tests failed out of 18
Total Test time (real) = 4.69 sec
```

新增测试 `host.testing.engine` 通过；既有通信、监控、配置、诊断、日志、Loader、断言、offscreen UI 和应用 smoke test 全部通过。

## 6. Review

### 必须修复

无。

### 建议修复

无阻塞项。TASK-016 组装 UI 时应继续由 UI 负责“先停止监控并进入 TESTING”，不要绕过 `AppStateController`。

### 可选优化

后续报告阶段可在不改变当前不可变结果契约的前提下增加汇总视图和序列化器。

## 7. 未验证项

- 未连接真实开发板和 RS485。
- 未实现 TASK-016 自动化测试页面和 Phase 5 至少 5 条真实套件。
- 未实现 Phase 6 完整 20 条套件、半自动人工步骤或 HTML/PDF 报告。
- 未执行任何超过 5 分钟的测试。
