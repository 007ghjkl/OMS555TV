# TASK-024 报告契约与元数据策略验证记录

> 日期：2026-09-06
>
> 结论：通过；TASK-024 完成，可以进入 TASK-025。

## 1. 实现范围

- 新增 `specs/host_phase8_report_contract.md`，评审通过且无阻塞未决项。
- 新增纯数据 `ReportInput`、四类来源标签的 `ReportMetadata`、`ReportSummary`、`ReportDocumentModel`、`ReportError` 和 `ReportModelBuilder`。
- 映射只消费不可变 `TestSuiteResult` 和显式输入快照，不访问 QWidget、串口、Windows 用户身份、文件内容或 JSONL。
- Firmware 版本只从本次 `PASS` 且断言通过的 major/minor 实际读取值提取；缺失显示“未采集”，冲突返回结构化错误。
- 保留基础请求、sequence、consistency、stability 和 guided recovery 的层级、expected/actual、实际样本、步骤、人工提示/动作、观察、恢复时间、RequestId、TX/RX、RTT、错误和清理错误。
- 稳定性明确区分总 attempt、内存保留/丢弃和外部 SessionLog 工件；构建器不读取日志重算结果。

## 2. 自动测试

使用全新目录 `build-host-task024`：

```powershell
& cmd.exe /d /s /c "call `"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul && cmake -S D:\Repos\OMS555TV\host\qt -B D:\Repos\OMS555TV\build-host-task024 -G Ninja -DCMAKE_PREFIX_PATH=D:\Dev\Qt\6.8.3\msvc2022_64 -DCMAKE_BUILD_TYPE=Debug"
& cmd.exe /d /s /c "call `"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul && cmake --build D:\Repos\OMS555TV\build-host-task024 --parallel"
ctest --test-dir build-host-task024 --output-on-failure
```

结果：全新配置和完整构建成功；最终 24/24 CTest 通过，总测试时间 10.64 秒。新增 `host.report.model` 纯数据测试覆盖：

- PASS/FAIL/ERROR/SKIPPED 混合计数、通过率、空套件、全 SKIPPED 和 Engine 状态优先级；
- 非终态、时间缺失、case ID/数量、suite 状态、证据保留和日志工件不变量错误；
- `system-observed`、`suite-configured`、`operator-entered`、`unavailable` 四类来源、优先级、缺失显示和长度；
- Firmware 实际读取提取、expected-only 缺失和冲突；
- 基础、sequence、consistency、stability、guided recovery 完整映射；
- 大写 TX/RX、空 RX、RTT 缺失、断言差异、通信错误、清理错误、人工提示和恢复时间；
- Schema v1/v2/v3 终态结果兼容。

完整回归还包括 Phase 5～7 Loader、TestEngine、guided 协调器、正式套件目录、offscreen UI 和应用 smoke，均通过。

## 3. Review

- 必须修复：无。
- 建议修复：无。
- 可选优化：TASK-025 基于该模型实现安全转义、自包含 HTML、打印样式与原子写入；TASK-026 再实现 UI 和一键导出。

本任务不生成 HTML/PDF、不执行测试套件、不访问开发板或 RS485，因此没有实机结论，也不改变 TASK-020/023 的原始硬件结果。
