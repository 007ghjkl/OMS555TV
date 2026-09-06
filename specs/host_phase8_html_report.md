# Host Phase 8 自包含 HTML 报告 Technical Spec

## 1. 文档状态

- 版本：1.0
- 日期：2026-09-06
- 对应任务：TASK-025
- 评审结论：通过，无阻塞未决项

## 2. 目标与边界

本规范定义 `ReportDocumentModel` 到 UTF-8 单文件 HTML 的确定性渲染、安全文本处理、稳定文件名和原子写入行为。生成器不访问 QWidget、测试引擎、串口、SessionLog 内容、网络或本机身份信息，也不重新解释测试结果。

输出用于离线浏览和浏览器“打印为 PDF”。原生 PDF、报告页面、保存对话框和自动打开浏览器属于后续任务。

## 3. 公共接口

新增无 QObject/QWidget 依赖的 `HtmlReportGenerator`：

- 构造函数接收可选 UTC 时钟函数；默认取当前 UTC 时间，测试注入固定时间。
- `generate(const ReportDocumentModel &)` 只接收 TASK-024 冻结模型，返回完整 UTF-8 内容、稳定建议文件名和生成时间。
- `write(const ReportDocumentModel &, const QString &targetFilePath)` 生成后以 `QSaveFile` 原子提交到调用方给出的完整目标路径。
- `suggestFileName(const ReportDocumentModel &)` 不访问文件系统，结果只由 suite ID、运行开始 UTC 和 run ID 决定。

生成与写入均以值结果返回：成功结果不带错误；失败结果不带 HTML/目标路径，并返回 `QVector<ReportError>`。在 TASK-024 错误码基础上新增：

| 错误码 | 条件 |
|---|---|
| `InvalidGenerationTime` | 注入时钟返回无效时间 |
| `InvalidOutputPath` | 空路径、目标为目录、父目录不存在或不是目录 |
| `TargetAlreadyExists` | 目标文件或其他同名条目已存在 |
| `OutputOpenFailed` | `QSaveFile::open` 失败 |
| `OutputWriteFailed` | 未写入完整 UTF-8 内容 |
| `OutputCommitFailed` | 临时文件提交失败 |

错误的 `path` 字段定位到 `generatedAtUtc` 或 `targetFilePath`，诊断使用中文。失败时调用 `cancelWriting()`；不得把临时文件或部分目标文件当作成功报告。

## 4. 时间与确定性

- 时钟值必须有效，转换为 UTC 后按 `yyyy-MM-dd'T'HH:mm:ss.zzz'Z'` 输出。
- 报告显示生成 UTC 时间；运行起止时间直接使用模型中已冻结的 UTC/本地 ISO 文本。
- 数字使用十进制整数或固定整数换算，不使用系统区域设置。
- 相同模型与相同时钟必须逐字节生成相同 HTML；不得写入随机数、当前用户名、主机名或绝对输出路径。

## 5. 文件名与冲突策略

建议文件名格式：

`<safe-suite-id>_<yyyyMMdd-HHmmss-zzzZ>_run-<run-id>.html`

安全片段规则：

1. ASCII 字母转小写；ASCII 数字保留。
2. 连续非 ASCII 字母数字和其他字符统一折叠为单个 `-`。
3. 去除首尾 `-`，最长 48 个字符。
4. 空结果回退为 `suite`。
5. 时间来自模型的运行开始 UTC，不来自生成时钟。

生成器不创建目录，也不静默覆盖或自动递增文件名。目标已存在时返回 `TargetAlreadyExists`；由 UI 决定更换目录、文件名或明确删除旧文件。默认调用方可把建议文件名放入仓库 `output/reports/`，该目录继续由 `.gitignore` 管理。

## 6. HTML 文档结构

固定结构如下：

1. `<!doctype html>`、`<html lang="zh-CN">`、UTF-8、viewport 和限制外部内容的 CSP。
2. `<header>`：标题、suite ID、文字状态、生成时间。
3. `基本信息`：项目、测试对象、设备、Firmware/Host、源码、测试人员、通信参数、环境、套件、Session ID、运行时间。
4. `测试汇总`：total、executed、PASS/FAIL/ERROR/SKIPPED、通过率、总耗时和辅助错误。
5. `用例明细`：suite → case → step/observation → attempt 层级。
6. `SessionLog 与证据边界`：证据保留摘要及日志工件 path/size/SHA-256/可用状态。

每条用例必须显示 ID、名称、类别、类型、前置/环境、描述、tags、终态、耗时、起止时间、请求定义、expected、actual、断言差异、主错误和清理错误。缺少的时间、RTT、RX、元数据和工件均显示明确占位。

专属结构：

- `sequence`：按模型顺序显示 repetition、step ID/index、延迟、状态、expected/actual、错误和 attempt sequence。
- `consistency`：显示断言中的全部结构化样本和聚合摘要。
- `stability`：显示总数、成功、失败、超时、有效/缺失 RTT 和最小/平均/最大 RTT，以及有界证据声明。
- `guided_recovery`：显示 final state、终态原因、人工 prompt、允许动作、确认/取消、备注、观察 target/outcome、连续计数、首个/稳定 RequestId、首个/稳定恢复耗时和恢复接线提醒。
- attempt：显示 sequence、purpose、关联 step/repetition、RequestId、请求描述、状态、TX/RX、CRC、RTT、重试和通信错误。

attempt、suite metadata、断言结构化值和人工恢复明细使用原生 `<details>` 折叠；不得使用 JavaScript。折叠只改变初始展示，不改变证据完整/有界语义。

## 7. 状态、占位与格式

- 状态同时显示文字 `PASS`、`FAIL`、`ERROR`、`SKIPPED` 和状态样式；不只依赖颜色。
- 可用元数据显示值与来源稳定名；不可用元数据显示其 `missingDisplay` 和 `unavailable`。
- TX/RX `Missing` 显示“未保留”，`Empty` 显示“空帧（0 字节）”，`Value` 显示模型的 uppercase hex。
- 缺失 RTT、时间、可选数值显示“未采集”；空集合显示“无”。
- 毫秒、纳秒、字节、ppm 均带明确单位。通过率按模型 ppm 以固定四位小数百分比显示；无分母显示 `N/A`。
- SessionLog `available=false` 时显示模型的 `missingDisplay`，不能暗示报告已包含完整日志。

## 8. 文本安全与长字段

所有字符串，无论来源，均按不可信纯文本处理：

1. CRLF 和 CR 规范化为 LF。
2. TAB 转为四个空格。
3. 除 LF 外的 C0 控制字符、DEL 和 C1 控制字符逐个替换为 U+FFFD。
4. 按顺序转义 `&`、`<`、`>`、`"`、`'` 为 `&amp;`、`&lt;`、`&gt;`、`&quot;`、`&#39;`。
5. 文本节点依靠 `white-space: pre-wrap` 保留换行，不把文本拼入标签名、CSS、URL或事件属性。

字段不静默截断；CSS 使用 `overflow-wrap:anywhere`。大型证据仅渲染模型中已保留的内容并置于 `<details>`，不得读取或复制外部 JSONL。

## 9. CSS 与打印

CSS 完全内联，不引用字体、图片、脚本、CDN 或 `@import`。屏幕样式采用系统字体、清晰状态徽标、响应式表格和可换行的 `pre`/代码证据。

`@media print` 必须：

- 使用白底黑字并保留边框和状态文字；
- 隐藏非内容装饰；
- 避免标题、汇总卡片、用例标题和短表格被分页拆开；
- 允许长表格自然分页，重复表头；
- 强制 `<details>` 内容打印展开，确保折叠证据不会从 PDF 消失。

## 10. 安全约束

- 文档不含 `<script>`、事件处理器、表单、iframe、object、外部样式、远程字体或外部 URL。
- CSP 固定为 `default-src 'none'; style-src 'unsafe-inline'; img-src data:; base-uri 'none'; form-action 'none'`。
- 工件路径仅显示为文本，不生成 `file://` 或可点击链接。
- 报告不读取外部工件存在性；`available` 只反映 TASK-024 模型中的调用方快照。

## 11. 测试策略

纯 Qt Test 至少覆盖：

1. 固定模型/时钟 golden 哈希与关键结构，证明确定性；
2. 四种状态文字、中文、基本信息、汇总和所有缺失占位；
3. `<>&"'`、恶意标签、换行、TAB、控制字符及长文本安全；
4. sequence、consistency、stability、guided recovery 和 attempt 层级与关联 ID；
5. 空 RX、缺失 RX、缺失 RTT、通信/断言/清理错误；
6. 有界证据声明和 SessionLog path/size/SHA-256；
7. 无脚本、无外部资源、内联 CSP/CSS 和打印规则；
8. 稳定文件名的清洗、长度、回退与固定时间；
9. 写入成功内容一致、父目录不存在、目标为目录、目标重名、打开失败及提交/写入可覆盖范围内的失败路径；
10. Host 全新配置、构建、全部 CTest 与应用 smoke 回归。

## 12. 评审记录

### 必须修复

无。

### 建议修复

无。TASK-024 没有生成时间字段；采用构造时注入时钟，既保持 `generate()` 只消费不可变文档模型，也满足生产实时与测试确定性。

### 可选优化

TASK-026 可加入保存对话框、建议目录、导出反馈和浏览器人工打印验证。原生 PDF、模板引擎、报告签名和并发多进程文件占位不属于本任务。
