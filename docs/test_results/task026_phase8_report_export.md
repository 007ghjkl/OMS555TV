# TASK-026 Phase 8 报告 UI 与一键导出验证记录

## 1. 结论

TASK-026 于 2026-09-06 完成。报告页、最近完整结果生命周期、必填元数据门禁、后台一键导出、文件冲突处理和示例报告均已实现；全新 Host 构建及 27/27 CTest 通过。真实 COM6/RS485 Phase 5 短套件为 8/8 PASS，一键生成的 HTML、SessionLog 摘要和通信证据一致。桌面、窄窗口及浏览器打印预览人工检查均通过，Phase 8 关闭。

## 2. 软件验证

验证环境为 Windows、Qt 6.8.3 `msvc2022_64`、Visual Studio 2026 Community/MSVC 19.51、CMake 与 Ninja。使用独立目录 `build-host-task026` 完成全新配置、完整构建和测试：

```powershell
cmake -S host/qt -B build-host-task026 -G Ninja `
  -DCMAKE_PREFIX_PATH=D:/Dev/Qt/6.8.3/msvc2022_64 `
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-task026 --parallel
ctest --test-dir build-host-task026 --output-on-failure
```

最终结果为 27/27 CTest 通过，总耗时 10.47 秒；此前两轮完整回归同样为 27/27 通过，`host.application.smoke` 通过。新增验证包括：

- `host.report.ui_export`：无结果、运行中、缺少测试人员、非法路径、重名、重复提交、异步完成、输入冻结、最近运行替换及 offscreen 页面渲染；
- `host.report.acceptance_fixtures`：全 PASS、混合 FAIL/ERROR/SKIPPED、stability 有界证据、guided recovery 人工动作/恢复时间四类确定性报告；
- 同一测试自动核对真实脱敏示例的 8 个用例、11 个 RequestId/TX/RX/RTT、Firmware、通信参数、SessionLog 摘要、CSP、离线资源和固定 SHA-256。

## 3. 真实 RS485 运行

| 项目 | 结果 |
|---|---|
| 套件 | `testcases/functional/phase5-smoke.json`，Schema v1 |
| DUT | NUCLEO-F411RE，Firmware 0.2 |
| 链路 | COM6，115200 8N1，无流控，Slave 1，timeout 500 ms |
| Run / Session | Run 1 / `bfcadd81-2564-4206-90d3-ea2c4be10c7b` |
| 时间 | 2026-09-06T05:36:26.172Z～05:36:27.532Z，1359 ms |
| 用例 | 8 PASS / 0 FAIL / 0 ERROR / 0 SKIPPED，通过率 100% |
| 通信证据 | RequestId 1～11，11 组 TX/RX/RTT；RTT 最小/平均/最大 19.494/35.931/52.611 ms |

`P5-THRESHOLD-RESTORE` 保存地址 9 原值 600，随后依次完成 `read_before_write`、写入 600、独立回读 600 和 `restore_original`；四步 RequestId 8～11 均成功、CRC 有效、无清理错误。由于实机当前基线恰为 600，本轮写入值与原值相同，但保存、回读和恢复请求均实际执行并留有独立证据。

## 4. 工件核对

原始报告位于忽略目录：

- `output/reports/phase5-smoke_20260906-053626-172Z_run-1.html`
- 大小：46,195 字节
- SHA-256：`14566603D68A1BEA69444FBCB0784E0CD8FDA9C383CA216C88CC97C358A8D8F0`

原始 SessionLog：

- `output/logs/2026-09-06/session-20260906-053553-807-bfcadd81-2564-4206-90d3-ea2c4be10c7b.jsonl`
- 45 行，18,306 字节
- SHA-256：`151AB0778C805D1A93BBC222DCD2F03043EEB69F0431B85E437776E1E1558B55`

报告中记录的 Session ID、日志大小及 SHA-256 与磁盘工件一致。报告的 8 个用例终态、11 个 RequestId、TX/RX/RTT、Firmware 0.2 和 COM6 参数均与不可变结果/JSONL 一致。

提交示例为 `docs/examples/task026-phase5-rs485-report.html`，大小 46,222 字节，SHA-256 为 `CA02E7C326901648188A17EB32FC110EB779BAE086EAA66301F7D9844ADF394C`。它直接来自上述真实报告，只把 `D:/Repos/OMS555TV/` 日志绝对路径改为仓库相对路径并增加脱敏说明；状态、计数、时间、Session ID、RequestId、报文、RTT、日志大小和日志 SHA-256 均未改变。自动检查确认示例不含 `C:/`、`D:/`、外部资源或脚本。

## 5. 人工视觉检查

用户于 2026-09-06 确认生成的 HTML 查看无问题，按验收步骤覆盖：

- 桌面宽度：内容完整，状态和分区清晰；
- 约 500 px 窄窗口：文本正常换行，表格可横向查看；
- 浏览器 `Ctrl+P` 打印预览：折叠详情展开，关键内容无异常截断。

本任务没有实现或验证原生 PDF；浏览器打印仅为可选输出方式。

## 6. 最终 Review

- 必须修复：无。
- 建议修复：无。
- 可选优化：历史运行选择、多分片日志清单、原生 PDF、签名和上传仍为后续范围。

结论只适用于当前安全低压、约 20 cm 点对点 RS485 台架，不外推为 USB 自动重连、8/24 小时稳定性、工业长线、隔离或 EMC 验证。
