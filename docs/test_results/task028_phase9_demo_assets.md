# TASK-028 Phase 9 展示素材与示例报告验收记录

## 1. 结论

TASK-028 已完成。仓库新增六张真实、脱敏、可在 GitHub Markdown 直接显示的硬件与生产 Host 素材，并复核 TASK-026 的真实 RS485 示例 HTML。图片、示例报告、来源边界、像素尺寸、SHA-256 和中文替代文本均已记录；最终 Review 无必须修复项，可以进入 TASK-029。

本任务没有修改 Host、Firmware、协议、测试引擎或报告模板，没有重跑 Phase 6 的 10 分钟稳定性测试，也没有执行 Phase 7 的物理断线流程。

## 2. 来源与环境

- 采集日期：2026-09-06。
- 产品源码提交：`6a027c3`（TASK-026 生产 Host）；采集时文档基线：`6a7aae0`（TASK-027）。
- Host：`build-host-task026/src/oms555tv_host.exe`，Windows x64，Qt 6.8.3 MSVC 2022。
- 设备：NUCLEO-F411RE，Firmware 0.2，USART1-RS485，Slave 1，115200 baud、8N1、无流控、500 ms 超时。
- 台架：约 20 cm、安全低压、非隔离实验接线；不代表工业长线、隔离、EMC 或量产环境。
- 串口：本次运行时枚举为 COM6。该值仅属于本机本次采集，不是通用配置。

## 3. 资产验收

| 资产 | 真实性 | 观察结果 | 验收 |
|---|---|---|---|
| `hardware-rs485-bench.jpg` | 真实实机 | NUCLEO、TTL-RS485、USB-RS485 及 A/B/GND 关系可辨认；纸签只标识既有接线 | PASS |
| `host-monitoring.png` | 真实实机 UI | 在线、完整实时快照、Firmware 0.2；当前进程会话 315 请求、314 成功、0 超时、1 失败、14 次周期超限 | PASS，明确不是稳定性证明 |
| `host-configuration.png` | 真实实机 UI | 只读获得四路基线 60.0/60.0/60.0/40.0 ℃，操作成功 | PASS |
| `host-diagnostics.png` | 真实实机 UI | 可见 FC03、RequestId、RTT、CRC Valid 及代表性 TX/RX | PASS |
| `host-automation-results.png` | 真实实机 UI | 相对套件路径；Phase 5 smoke 8/8 PASS；阈值用例保留 4/4 attempt，失败 0，可见代表性 RequestId/TX/RX | PASS |
| `host-report.png` | 真实结果脱敏 | 可见基本信息、8/8/100% 汇总和用例明细入口 | PASS |

监控截图如实保留当前进程中的一次失败和周期超限，没有把它编辑成全成功状态；该画面只用于展示可观测性。自动化截图来自另一次短时 Phase 5 smoke，不能与 TASK-026 示例报告中的温度、RTT、时间或 Session ID 混作同一次运行。

## 4. 写入与恢复

采集前在参数配置页只读确认四路阈值为 60.0/60.0/60.0/40.0 ℃。随后执行 `testcases/functional/phase5-smoke.json`：套件只写地址 9 的 A 相阈值，B/C/环境阈值未被写入；`P5-THRESHOLD-RESTORE` 的写前读取、写入、独立回读和恢复共 4 个 attempt 全部保留，失败数为 0，用例终态 PASS。因此本次唯一写入已恢复，没有发现清理错误。

该短套件结果仅证明本次采集流程成功，不替代 TASK-020 的 Phase 6 正式套件，也不新增长期稳定性结论。

## 5. 示例报告复核

复核文件为 `docs/examples/task026-phase5-rs485-report.html`：

- SHA-256 为 `CA02E7C326901648188A17EB32FC110EB779BAE086EAA66301F7D9844ADF394C`，与 TASK-026 记录一致；
- 8/8 PASS、100.0000%、Firmware 0.2、COM6 运行快照、RequestId/TX/RX/RTT 和 Session 摘要未被改写；
- 无 HTTP/HTTPS URL、外部 `src`/`href`、脚本、Windows 绝对路径或 `rainbow` 用户名；
- COM6 只作为 2026-09-06 那次真实运行的通信快照，不作为通用端口；
- 报告离线、自包含，可用于 README 链接和浏览器演示。

报告回归命令及结果：

```powershell
$env:Path = "D:\Dev\Qt\6.8.3\msvc2022_64\bin;$env:Path"
ctest --test-dir build-host-task026 -R "report|Report" --output-on-failure
```

`host.report.model`、`host.report.html`、`host.report.ui_export`、`host.report.acceptance_fixtures` 共 4/4 通过；最终复核总耗时 1.17 秒。

## 6. 脱敏、链接与视觉检查

- 六张图片均以原始分辨率检查，文字、状态、表格与接线标识可辨认；图片格式为 GitHub 可直接显示的 PNG/JPEG。
- 未发现 Windows 用户目录、设备序列号、个人通知、无关应用或 SessionLog 绝对路径。
- 自动化截图首次采集暴露本机绝对路径，验收前已由操作者重新采集为仓库相对路径；最终文件不保留首次版本。
- 硬件照片使用纸签解释 A/B/GND，不移动接线、不修改测试结果；画面没有宣称工业等级。
- 硬件照片原文件含 GPS、相机和拍摄时间 EXIF；验收阶段已清除这些隐私字段并重编码为 4096×3072 JPEG。重编码前后人工视觉核对一致，最终文件只保留无个人信息的色彩配置/编码属性。
- 素材清单中的六个图片链接、示例报告链接和验收记录链接均以仓库相对路径维护。

## 7. Review

### 必须修复

无。

### 建议修复

无。

### 可选优化

- TASK-029 可按 README 信息层级选择其中三至六张图片展示，避免首页过长。
- 后续若 README 加载体积成为问题，可在不改变画面含义的前提下等比例缩小硬件照片；当前约 2 MB，已兼顾接线辨识度与仓库体积。

## 8. 验收结论

TASK-028 的五类必需展示范围、来源追溯、写入恢复、示例报告复核、脱敏、完整性、中文替代文本和最终 Review 均已完成。素材不扩大既有测试结论，允许进入 TASK-029。
