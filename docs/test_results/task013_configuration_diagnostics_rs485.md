# TASK-013 参数配置、通信诊断与真实 RS485 验证记录

> 日期：2026-09-04
>
> 结论：通过
>
> 对应规范：`specs/host_phase4_configuration_and_diagnostics.md`

## 1. 验证范围

本次验证覆盖四路告警阈值读取、0x06 写入、独立 0x03 回读、部分失败与取消清理，基于不可变 `requestCompleted` 结果的通信诊断，以及有界内存与 JSON Lines 会话日志。真实台架验证在监控停止、应用处于 `ConnectedIdle` 时独占 `ManualDebug` owner，结束后恢复测试前阈值并再次整块读取。

不包含 8/24 小时稳定性、工业长线、隔离、终端/偏置组合、EMC、错误帧注入、TestEngine 或正式报告生成。

## 2. 环境

- Host：Windows，Qt 6.8.3 `msvc2022_64`，MSVC 19.51，CMake + Ninja，Debug。
- DUT：NUCLEO-F411RE / STM32F411RET6，USART1，115200 8N1，Slave ID 1。
- 物理链路：COM6 USB-RS485、自动换向 TTL-RS485 模块、约 20 cm 安全低压点对点台架。
- 构建目录：`build-host-task013`，由全新配置生成。

## 3. 自动化验证

全新配置和构建成功，全部 Host CTest 为 **15/15 通过**，总耗时 6.62 秒。TASK-013 新增覆盖如下：

| 范围 | 结果与关键断言 |
|---|---|
| 配置服务 | 四路读取与写回读、0.1 ℃ 精度本地拒绝、0x03 远端异常、回读超时、回读不一致、部分失败继续、取消、获取/释放 owner、Monitoring 门禁 |
| 通信诊断 | 成功、远端异常 0x02、超时、取消、CRC 错误、TX/RX 十六进制展示、组合筛选、清空、1000 条有界保留 |
| 会话日志 | 开始/结束、重复开始、JSONL 重新读取、可选字段、2000 条有界内存、目录失败结构化错误、文件失败不影响内存记录 |
| offscreen UI | Disconnected/ConnectedIdle/Monitoring 门禁、完整配置结果、诊断列表与筛选、会话文件创建 |
| 回归 | Phase 3 通信核心/生产后端、TASK-011 监控核心、TASK-012 监控 UI 和应用 smoke 均通过 |

非法输入在申请 owner 和发送请求前被拒绝，因此不产生 RequestId、TX 或设备写入。MainWindow 静态检查未发现 `QSerialPort`、RTU Codec、CRC、寄存器映射或直接 0x03/0x06 调用；协议与地址仍由服务层及设备模型负责。

## 4. 真实 RS485 写入、回读与恢复

执行命令：

```powershell
build-host-task013\task013_rs485_validation.exe --port COM6 --timeout-ms 500
```

执行结果：

| 阶段 | A 相 | B 相 | C 相 | 环境 | 结果 |
|---|---:|---:|---:|---:|---|
| 测试前读取 | 60.0 ℃ | 60.0 ℃ | 60.0 ℃ | 40.0 ℃ | 通过 |
| 测试值独立回读 | 60.1 ℃ | 60.1 ℃ | 60.1 ℃ | 40.1 ℃ | 通过 |
| 恢复后最终读取 | 60.0 ℃ | 60.0 ℃ | 60.0 ℃ | 40.0 ℃ | 通过 |

全过程共完成 20 个请求：显式测试前整块读取 1 个，测试写操作自身的写前整块读取 1 个及四路写/回读 8 个，恢复写操作自身的写前整块读取 1 个及四路写/回读 8 个，最终整块读取 1 个。工具最终报告 **20/20 成功**、`RESTORE=PASS`、`FINAL_READ=PASS`、`RESULT=PASS`。所有阈值已恢复为测试前值，未遗留 `ManualDebug` owner 或在途请求。

## 5. 会话日志证据

- 本地文件：`output/logs/2026-09-04/session-20260904-113511-551-2f5f87b9-6b3f-408c-81f4-d2fb8aaa767f.jsonl`
- 文件大小：8135 字节。
- JSON 行数：23；其中 `request_completed` 20 条、成功 20 条、带 TX 20 条、带 RX 20 条。
- RequestId：1～20，均可由诊断记录定位对应 TX/RX 和结构化元数据。
- SHA-256：`ED69B711096F58805C70A83F8384F1FEB917043E2ADF7F9C6870317FB03932A4`。
- 日志位于 `.gitignore` 覆盖的 `output/logs/`，未进入版本控制。

## 6. 最终 Review

### 必须修复

无。

### 已在 Review 中修复

- 修复取消发生在 owner 获取阶段时的终态清理，确保成功获取后释放，获取失败且 owner 为 `None` 时也能结束操作。
- 修复活动会话内重复“开始”造成后续正常“结束”错误返回的问题。
- 内部不变量错误改为先走 owner 清理路径，避免意外遗留 `ManualDebug`。

### 可选后续验证

- 8/24 小时运行、工业长线、隔离与 EMC 仍需独立任务和对应硬件条件，不能由本次短距离台架结果外推。

## 7. 结论

TASK-013 的 Technical Spec、实现、自动化测试、真实 RS485 四路阈值写回读、原值恢复、会话日志重新核对和最终 Review 均完成。参数配置、通信调试与运行日志 MVP 验收通过。
