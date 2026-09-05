# TASK-022 Phase 7 半自动测试控制器与 UI 验证记录

> 日期：2026-09-05
>
> 结论：通过；允许进入 TASK-023。

## 1. 验证范围

本记录对应 TASK-022，验证 Technical Spec、无 QWidget 的引导协调器、TestEngine 互斥探测入口、人工动作 token、虚拟时间观察、不可变结果/TEST 日志和 offscreen Qt UI。验证完全使用 Fake 客户端，没有访问串口、开发板或真实 RS485，也没有执行人工拔插。

## 2. 实现资产

- `specs/host_phase7_guided_execution_ui.md`：协调器、控制器、Engine、UI 的所有权与信号方向，以及 token、计时、观察、清理和恢复提醒规则。
- `GuidedTestCoordinator`：人工等待、严格串行观察、连续计数、deadline、恢复耗时、结果发布、日志与 Testing owner 清理。
- `TestEngine::submitGuidedProbe()`：与普通套件互斥、仅限 TESTING/Testing owner 的 0x03 单探测入口。
- `TestAutomationController` v3 路由与动作 API：v1/v2 兼容路径保持不变。
- 自动化页引导区域：提示、说明、安全信息、倒计时、连续样本、RequestId、恢复耗时、确认/取消和接线恢复提醒。
- `host.testing.guided_coordinator` 与扩展后的 `host.testing.automation_ui`。

## 3. 自动验证

最终全新构建目录：`build-host-task022`。

```powershell
$devShell = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShell
Enter-VsDevShell -VsInstallPath 'C:\Program Files\Microsoft Visual Studio\18\Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
cmake -S host/qt -B build-host-task022 -G Ninja -DCMAKE_PREFIX_PATH='D:/Dev/Qt/6.8.3/msvc2022_64' -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-task022 --parallel
ctest --test-dir build-host-task022 --output-on-failure
```

结果：配置成功，145 步全新构建成功；最终 22/22 CTest 通过，完整测试时间 9.78 秒。

核心覆盖：

- 两次人工确认、连续 ResponseTimeout、连续合法业务响应和自动 PASS；
- 人工等待期间零请求，等待/观察期间持续持有 Testing owner，终态释放 owner；
- stale、重复和错误 token 拒绝，不推进步骤或重复记录；
- 中断未检出、恢复超时、业务断言不匹配、致命通信错误、人工取消、人工超时和观察中止；
- deadline 与下一探测使用独立 generation 保护，探测严格串行且不追赶积压；
- 结果与 TEST 日志中的人工步骤、token、RequestId、TX/RX、RTT、恢复耗时和终态一致；
- offscreen UI 显示提示、安全说明、倒计时、连续样本、恢复耗时、PASS/FAIL/SKIPPED 和未恢复接线提醒；
- v1/v2、Phase 5/6、监控、配置、诊断、日志、通信后端和应用 smoke 无回归。

## 4. Review

- 必须修复：无。
- 建议修复：无。
- 可选优化：TASK-023 增加正式 Phase 7 RS485 套件和安全低压台架人工断线—恢复证据；后续报告任务可直接消费本任务结果模型。

TASK-022 的实现、测试、文档同步与 Review 已完成。该结论只允许进入 TASK-023，不能宣称真实 RS485 物理恢复或 Phase 7 已关闭。
