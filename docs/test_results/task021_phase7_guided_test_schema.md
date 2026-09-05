# TASK-021 Phase 7 引导式模型与 Schema v3 验证记录

> 日期：2026-09-05
>
> 结论：通过；允许进入 TASK-022。

## 1. 验证范围

本记录对应 TASK-021，验证 Technical Spec、独立 Schema v3、严格 Loader、引导式纯数据模型、人工动作校验、恢复计时和不可变结果证据。验证不访问 QWidget、串口、开发板或真实 RS485，也不执行人工拔插和半自动 UI 流程。

## 2. 实现资产

- `specs/host_phase7_guided_test_schema.md`：状态、终态优先级、提示/确认/取消、观察、计时、安全与 Phase 8 交接契约。
- `testcases/schema/test-suite-v3.schema.json`：独立 v3 Schema，保持 v1/v2 原语义不变。
- `TestCaseLoaderV3`：无 BOM UTF-8、未知字段拒绝、全有或全无加载和跨字段语义校验。
- `GuidedTestModel`：run/case/step/token 动作校验、固定终态优先级和无溢出双口径恢复计时。
- 有效/边界/七类无效 fixture 与中文示例。
- `GuidedRecoveryCaseResult`：人工记录、观察进度、RequestId/事务 attempt、恢复时间和未恢复指引。

## 3. 自动验证

最终全新构建目录：`build-host-task021-final`。

```powershell
$devShell = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShell
Enter-VsDevShell -VsInstallPath 'C:\Program Files\Microsoft Visual Studio\18\Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
cmake -S host/qt -B build-host-task021-final -G Ninja -DCMAKE_PREFIX_PATH='D:/Dev/Qt/6.8.3/msvc2022_64' -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-task021-final --parallel
ctest --test-dir build-host-task021-final --output-on-failure
```

结果：配置成功，140 步全新构建成功；最终 21/21 CTest 通过，总测试时间 9.97 秒。

新增 `host.testing.guided_model` 覆盖：

- 有效 v3 的断线提示、连续超时观察、重连提示、连续合法响应和可选业务断言规范化；
- 人工等待 300000 ms、观察 deadline 120000 ms、请求 timeout 60000 ms 和用例预算 900000 ms 边界；
- 未知步骤、未知动作、缺失取消恢复指引、非法 interval/deadline、重复 step ID、0x06 写探测结构化拒绝；
- 只有结构化 `ResponseTimeout` 能命中中断条件；取消、权限错误和合法响应均不会误判为断线，恢复还会执行可选业务断言；
- stale、重复、跨运行、跨用例、跨步骤和错误 token 拒绝，校验不修改上下文；
- 首个成功/稳定恢复两个时长、零边界、逆序、缺失与 `qint64` 溢出拒绝；
- 多候选终态固定优先级；
- 人工动作、观察结果、RequestId 和恢复时间经 `TestResultManager` 复制发布后保持不可变。

## 4. 兼容性与回归

v1/v2 有效 fixture、Phase 5 正式套件、Phase 6 主套件和 Fake 边界套件均保持成功加载；既有 TestEngine、通信、监控、配置、日志、offscreen UI 和应用 smoke 测试全部通过。

首次全量回归发现旧 `unknown-version.json` 使用版本 3；在 v3 成为合法版本后该 fixture 不再表达“未知版本”。fixture 已改为版本 4，Loader 的未知版本行为与测试断言未放宽。

## 5. Review

- 必须修复：无。
- 建议修复：无。
- 可选优化：TASK-022 实现协调器、注入调度器和 offscreen UI；TASK-023 再执行真实 RS485 人工断线—重连验收。

TASK-021 的实现、测试、文档同步与 Review 已完成。该结论只允许进入 TASK-022，不能宣称半自动 UI、真实物理恢复或 Phase 7 已完成。
