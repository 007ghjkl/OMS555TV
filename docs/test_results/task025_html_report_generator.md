# TASK-025 自包含 HTML 报告生成器验证记录

> 日期：2026-09-06
>
> 结论：通过；TASK-025 完成，可以进入 TASK-026。

## 1. 实现范围

- 新增并评审 `specs/host_phase8_html_report.md`，结构、转义、文件、打印和错误策略无阻塞未决项。
- 新增无 QObject/QWidget 依赖的 `HtmlReportGenerator`；`generate()` 只消费不可变 `ReportDocumentModel`，生成时间通过构造时 UTC 时钟注入。
- 输出为 UTF-8 单文件 HTML，包含 CSP、内联 CSS 和无 JavaScript 的 `<details>`；不包含外部脚本、样式、字体、图片、链接或 SessionLog 内容。
- 完整渲染基本信息及来源、汇总、用例、步骤、断言、sequence、consistency、stability、guided recovery、RequestId、TX/RX、RTT、主错误、清理错误、证据保留和 SessionLog 工件摘要。
- 所有文本统一规范化 CR/LF、TAB、C0/DEL/C1 控制字符，并转义 `<>&"'`；长字段不截断，通过 CSS 换行并把大型证据放入折叠结构。
- 状态同时显示 PASS/FAIL/ERROR/SKIPPED 文字与样式；打印 CSS 使用黑白可读状态、重复表头并展开 `<details>` 内容。
- 建议文件名稳定包含安全 suite 片段、运行开始 UTC 和 run ID；目标路径写入使用禁用直接回退的 `QSaveFile`，拒绝重名且返回结构化路径/打开/写入/提交错误。

## 2. 自动测试

使用全新目录 `build-host-task025`：

```powershell
& cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake -S host/qt -B build-host-task025 -G Ninja -DCMAKE_PREFIX_PATH=D:/Dev/Qt/6.8.3/msvc2022_64 -DCMAKE_BUILD_TYPE=Debug'
& cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build build-host-task025 && ctest --test-dir build-host-task025 --output-on-failure'
```

结果：全新配置、完整构建成功；最终 25/25 CTest 通过，总测试时间 10.77 秒。新增 `host.report.html` 的 6 组测试覆盖：

- 固定模型、固定生成时间和 SHA-256 golden，重复生成逐字节一致；
- 中文、恶意标签、`<>&"'`、换行、TAB、控制字符和 2048 字符长字段；
- PASS/FAIL/ERROR/SKIPPED、全部元数据来源、零分母和缺失占位；
- sequence 步骤、consistency 样本、stability 统计/有界证据和 guided prompt/action/observation/恢复时间；
- RequestId、TX、空 RX、缺失 RX、缺失 RTT、通信错误、断言差异和清理错误；
- CSP、无脚本/外部资源、打印 CSS 和打印时展开折叠内容；
- 安全文件名的清洗、长度、回退和固定时间；
- 原子写入内容一致、非法路径、目录目标、父目录缺失、目标重名和 Windows 非法文件名打开失败。

完整回归还包括 Phase 5～7 Loader、TestEngine、guided 协调器、正式套件目录、offscreen UI 和应用 smoke，均通过。

## 3. Review

- 必须修复：无。
- 建议修复：无。
- 可选优化：TASK-026 接入最近一次终态结果、异步 UI、一键导出、真实短套件报告和浏览器桌面/窄窗口/打印预览人工检查。

本任务只使用确定性纯数据 fixture 和临时目录，没有访问开发板、串口或真实 SessionLog，也没有实现 UI 或原生 PDF。因此不能把本记录视为真实运行报告或 Phase 8 关闭证据。
