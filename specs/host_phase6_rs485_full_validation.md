# Host Phase 6 真实 RS485 完整套件验收技术规范

> 状态：已实现并通过真实 RS485 完整验收
>
> 日期：2026-09-05
>
> 适用任务：TASK-020

## 1. 目标与边界

本规范定义 TASK-019 正式主套件在当前安全低压 RS485 台架上的唯一关闭流程。验收复用生产 `QSerialPortModbusClient`、`AppStateController`、`TestAutomationController`、`TestEngine`、`TestResultManager`、`ConfigurationService`、`CommunicationDiagnosticsModel` 和 `SessionLogService`，不创建第二套协议实现，不修改 Firmware、寄存器表或正式 JSON。

本任务不执行物理拔插、设备复位、传感器断开、Raw Frame、8/24 小时稳定性或报告生成。Fake-only 的 TC-P006、TC-B001、TC-B002、TC-D001 不进入真实主套件结果。

## 2. 专用验收工具

新增 `task020_rs485_validation`，使用 `QApplication` 实例化生产 `MainWindow` 并保持窗口可见。命令必须显式提供运行时枚举所得端口、正式套件路径和可追溯源码修订标识，不在代码中写死 COM 号。

工具提供三个彼此独立、分别创建会话日志的模式：

| 模式 | 用途 | 执行集合 | 成功条件 |
|---|---|---|---|
| `preflight` | 五分钟内的安全预检 | TC-F001、TC-F005、TC-P004、TC-R-AUTO-001 | 四条 PASS、Firmware 0.2、owner 释放、阈值未变 |
| `abort` | 五分钟内的 UI/中止专项 | 只运行 TC-S001，并在配置时刻自动中止 | 当前用例为 Aborted/ERROR、其余不计入正式结果、owner 释放、阈值未变 |
| `full` | 正式 Phase 6 关闭运行 | 原样执行 20 条主套件 | 20/20 PASS、10 分钟稳定性合格、证据与阈值全部一致 |

`preflight` 和 `abort` 不是正式 PASS 套件，不得与 `full` 合并计数。`full` 的 watchdog 为 15 分钟，仅用于防止状态机失去终态，不缩短 TC-S001 的 600000 ms 配置。

## 3. 开始门禁

1. 加载后严格确认套件 ID 为 `phase6-rs485-full`、Schema v2、20 条全部启用、无 Fake 环境，并满足功能 8、协议 4、边界 4、数据一致性 2、自动恢复 1、稳定性 1。
2. 输出套件、当前验收程序和源码修订标识；分别计算 SHA-256。程序文件哈希用于精确关联未提交或后续提交的构建产物。
3. 运行时枚举串口并打印端口、描述、厂商、序列号和 VID/PID。正式基线要求所选设备仍为 VID `345F`、PID `3020`、序列号 `A02001JS`；身份不符立即停止，不允许用参数绕过。
4. 连接参数固定为 115200 8N1、无流控、Slave ID 1，请求超时默认 500 ms。
5. 进入 Testing 前通过 `ConfigurationService` 一次读取四路阈值并保存基线；读取失败时不得启动套件。

## 4. 执行与 UI 响应

- 运行始终由 `TestAutomationController` 获取唯一 Testing owner；MainWindow 仅观察控制器和不可变结果。
- 使用 100 ms GUI 心跳记录 tick 数与最大迟到。正式模式要求运行期间存在心跳且最大迟到不超过 1000 ms；该值只证明当前应用事件循环响应，不代表实时系统保证。
- `QtMonitorScheduler` 的真实等待使用 `Qt::PreciseTimer`；稳定性仍从每次实际请求开始时刻计算下一周期，不追赶已经错过的周期，也不修改正式 1000 ms 间隔和 600000 ms 时长。
- `abort` 模式在首个稳定性请求运行后由单次定时器调用控制器中止，必须沿既有状态机取消请求或等待并返回 `CONNECTED_IDLE`。
- `full` 模式禁止筛选或跳过用例；任何 FAIL、ERROR、SKIPPED、NOT_RUN 或 auxiliary error 均使正式结果失败。

## 5. 阈值安全

- 每个成功写用例继续由 Engine 完成写前读、0x06、独立 0x03 回读和恢复原值。
- 正式或中止流程结束后，再通过 `ConfigurationService` 整块读取四路阈值；必须与测试前基线逐项一致。
- 最终读取失败或不一致时整体验收失败，即使套件结果为 PASS。

## 6. 结果与证据审计

正式模式逐用例输出 ID、类别、状态、持续时间、预期/实际摘要、复合步骤和稳定性统计。计数只使用本次 20 条主套件结果。

证据核对规则：

1. `sum(evidenceRetention.totalAttempts)` 等于 Testing owner 的诊断 RequestId 数和 TEST `request_attempt` RequestId 数。
2. 诊断与 TEST 日志的全量 RequestId 集合完全一致且无重复；内存中保留的 attempt RequestId 必须是该全集的子集。
3. 正式模式所有保留 attempt 均有非空 TX/RX 和 RTT；远端 0x02/0x03 也必须保留 RX。
4. TC-P007 的 15 个复合步骤和 TC-R-AUTO-001 的 2 个步骤均为 PASS；自动恢复两个 RequestId 不同，后续合法步骤成功并保存耗时。
5. TC-S001 的 `total = success + failure + timeout`，持续时间不少于 600000 ms，最低 594 成功，失败率不超过 10000 ppm，RTT 样本及 min/avg/max 完整。
6. 会话结束后输出 JSONL 路径、字节数、行数和 SHA-256；日志位于忽略目录，不提交原始文件。
7. `validation_complete` 必须在 JSONL 中保存套件/稳定性持续时间与计数、RTT 汇总、UI tick/最大迟到和测试前后阈值，确保终端输出丢失后仍可离线完成最终审计。

## 7. 终态与错误处理

无论成功、断言失败、通信错误、watchdog 或中止，都必须先等待自动化工作流释放 Testing owner，再执行最终阈值读取和显式断开。最终成功要求应用为 `DISCONNECTED`、owner 为 `None`、会话日志正常结束且没有未处理错误。

工具使用非零退出码表示失败，并保留原始会话文件。不得自动重跑失败用例、修改期望范围或隐藏第一次失败。

## 8. 验证顺序

1. 全新配置、构建并运行全部 Host CTest。
2. 执行 `preflight`，确认硬件身份、Firmware 和只读协议路径。
3. 执行独立 `abort` 专项，确认可见 UI、事件循环、受控中止、owner 和阈值恢复。
4. 用户确认台架稳定后手动执行 `full`；该步骤真实持续至少 10 分钟。
5. 保存三次会话摘要与哈希，更新 TASK、README、测试计划、用例目录和架构状态，完成最终 Review。

## 9. 评审结论

方案不改变既有架构或公开协议；正式套件原样执行，短时中止与正式 PASS 会话隔离。稳定性内存抽样与 JSONL 全量证据使用不同集合规则，避免沿用 TASK-016 的集合相等逻辑导致误判。阈值前后双重门禁、硬件身份检查、可见 UI 心跳和 15 分钟 watchdog 已覆盖 TASK-020 的安全与可验证性要求。最终真实 RS485 主套件 20/20 PASS，600001 ms 稳定性完成 599/599 次成功请求，最终 Review 无必须修复项，本规范已完成验证。
