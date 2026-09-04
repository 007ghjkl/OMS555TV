# Host Phase 4 实时监控 UI 与 RS485 长时验证技术规范

> 状态：已实现并验收
> 版本：1.0
> 日期：2026-09-04
> 对应任务：`TASK-012 Host Phase 4 实时监控 UI 与 RS485 长时验证`

## 1. 目的与范围

本规范定义 TASK-011 监控核心之上的 Qt Widgets 展示层、串口运行时枚举、交互门禁和真实 RS485 长时验证入口。UI 不解析寄存器、不持有串口、不提交 Modbus 请求；所有连接和监控命令只经 `AppStateController`，所有监控数据只经 `MonitorService` 和不可变完整快照。

不实现趋势曲线、参数写入、通信调试、日志归档、自动重连、TestEngine 或 Firmware 修改。

## 2. 组件与依赖

```text
MainWindow（Qt Widgets）
        │ 仅调用/订阅
        ▼
MonitoringViewModel（Qt Core 展示模型）
   ├── AppStateController
   ├── MonitorService
   └── ISerialPortCatalog
              │
              └── QtSerialPortCatalog（QSerialPortInfo，只枚举，不打开）
```

- `MainWindow` 不包含 `QSerialPort`、RTU、CRC、寄存器地址或轮询代码。
- `MonitoringViewModel` 不直接依赖 `IModbusClient`，不复制应用状态机。
- 生产对象由 `main.cpp` 组装；测试注入 Fake Modbus 和 Fake 串口目录。
- 专用长时验证入口复用生产窗口、ViewModel、Controller、Monitor 和串口后端，只增加只读记录与自动结束编排。

## 3. 字段来源、单位与有效性

| UI 字段 | 唯一数据源 | 单位/格式 | 有效条件 | 陈旧行为 |
|---|---|---|---|---|
| A/B/C 相温度 | `DeviceSnapshot.measurements` | `0.1 ℃` | 存在完整成功快照 | 保留值，并由总数据状态明确标记陈旧 |
| 模拟环境温度 | 同上 + `DeviceStatusWord.ambientSimulated` | `0.1 ℃`，固定标注“模拟源” | 存在完整成功快照 | 同上；不得去掉模拟源文字 |
| 光敏模拟电压 | `lightMillivolts` | `mV` | 存在完整成功快照 | 同上 |
| 四路高温告警 | `AlarmStatusWord` | “正常/高温告警” | 存在完整成功快照 | 同上，文字与颜色无关 |
| 传感器/ADC 状态 | `DeviceStatusWord` | 逐项文字 + 原始十六进制状态字 | 存在完整成功快照 | 同上 |
| Firmware 版本 | `FirmwareVersion` | `major.minor` | 存在完整成功快照 | 同上 |
| 运行时间 | `Diagnostics.uptimeSeconds` | 秒及 `HH:mm:ss` | 存在完整成功快照 | 同上，不自行递增旧值 |
| 最后成功时间 | `MonitoringSnapshot.lastSuccessfulUtc` | 本地 ISO 时间 | 存在完整成功快照 | 失败批次不得更新 |
| 应用/连接状态 | `AppStateController` | 中文稳定文本 | 始终有效 | 不适用 |
| 设备健康 | `MonitorService.health` | 未知/在线/退化/离线 | 始终有效 | 停止后显示未知，不把旧快照表述为在线 |
| 请求/成功/失败/超时 | `CommunicationStatistics` | 无符号计数 | 始终有效 | 会话内累计，不因失败清零 |
| 成功率 | `succeeded / requests` | 百分比，2 位小数 | `requests > 0` | 无请求显示 `--` |
| RTT | 统计中的最近/最小/累计/最大 | `ms`，3 位小数 | 对应 RTT 样本存在 | 无样本显示 `--` |
| 实际有效周期 | 最近批次 `effectiveInterval` | `ms`，1 位小数 | 至少开始两个批次 | 首批显示 `--` |
| overrun | 最近批次 `overrun` + 累计次数 | 是/否和累计值 | 至少一个终态批次 | 无批次显示“尚无批次” |

数据状态规则：没有快照时为“无有效快照”；仅当应用处于 `Monitoring` 且健康为 `Online` 时为“实时”；`Degraded`、`Offline`、监控刚启动尚未成功、已停止或已断开时一律明确显示“陈旧”及原因。失败批次不覆盖任何成功快照字段。

## 4. 配置与交互门禁

- 串口、Slave ID 和响应超时仅在 `Disconnected`、无控制命令进行时可编辑。
- 串口刷新只调用目录枚举，不打开设备；保留仍存在的当前选择，只有唯一端口且无选择时才自动选择。
- 串口固定展示 115200、8 数据位、无校验、1 停止位、无流控。
- 目标周期提供 100、500、1000、2000 ms；在 `Disconnected` 或 `ConnectedIdle` 且无命令时可编辑，监控期间锁定。
- 连接仅在断开、配置有效且未执行命令时允许；断开允许于 `ConnectedIdle` 或 `Monitoring`。
- 开始监控仅在 `ConnectedIdle`；停止仅在 `Monitoring`；`Stopping` 期间全部控制按钮禁用。
- `Error` 状态只允许“恢复”；恢复到 `Disconnected` 后由用户显式重新连接。
- 所有按钮状态每次从 Controller 状态派生，不由槽函数维护独立布尔状态。

## 5. 错误与状态展示

- 命令同步拒绝、异步失败和监控批次错误均转换为中文稳定类别与诊断文本。
- 普通请求超时只改变批次、健康、统计和陈旧状态，不强制应用进入 `Error`。
- 后端 Faulted、意外断开或不变量错误沿用 Controller 行为进入 `Error`。
- 告警、故障、离线、退化和陈旧必须有文字，不得只依赖颜色。

## 6. 长时验证记录

专用验证入口接受运行时端口、周期、超时、时长和日志路径。它显示真实 `MainWindow`，用 ViewModel 发起连接/监控/停止/断开，并记录：

1. Qt/Host 版本、UTC 起止、串口枚举和明确选择、115200 8N1、Slave ID、超时和目标周期；
2. 每个终态批次的时间、批次 ID、完整性、健康、实际周期、耗时、overrun、请求/失败/超时、RTT、运行时间和最后成功时间；
3. 每秒 UI 事件循环心跳的最大迟到量；
4. 最终请求/成功/失败/超时/批次/overrun/RTT 汇总和最终设备状态；
5. 连接、监控、停止、断开及错误事件。

默认正式验收时长为 1800 秒。记录器不重试请求、不写寄存器、不自动重连；异常时以非零退出码结束并保留已有证据。受控恢复模式仅用于实机验收：观察到 Offline 后通过 ViewModel 依次提交显式断开、重连和开始监控命令，并要求恢复后连续成功；该编排不进入产品逻辑。

## 7. 测试要求

- offscreen UI 测试覆盖字段格式、单位、模拟源、告警/故障文字和无快照占位。
- 覆盖连接、开始、停止、断开、参数锁定、连接失败及恢复门禁。
- 覆盖成功快照后连续三批失败的退化/离线/陈旧，以及下一完整批次恢复在线。
- 覆盖实际周期与 overrun 展示，证明 UI 不虚报目标周期。
- 全新 MSVC/Qt 配置、构建和全部 Host CTest 通过。
- 真实 RS485 运行至少 30 分钟；最终报告引用原始记录，不把短距离台架结果外推到工业长线、隔离或 EMC。

## 8. 评审结论

本规范复用已批准的应用状态机、监控周期语义、完整快照和生产串口后端，未修改公开通信 API、协议或线程模型。实现已通过全新 Host 构建、11/11 CTest、30 分钟真实 RS485 和设备复位显式恢复验收；结果见 `docs/test_results/task012_monitoring_rs485_30min.md`，最终评审无必须修复项。
