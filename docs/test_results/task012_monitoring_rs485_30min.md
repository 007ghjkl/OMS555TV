# TASK-012 实时监控 UI 与 RS485 长时验证记录

> 结论：通过
>
> 日期：2026-09-04
>
> 对应任务：`TASK-012 Host Phase 4 实时监控 UI 与 RS485 长时验证`

## 1. 实现范围

本任务在 TASK-011 的 `AppStateController` 与 `MonitorService` 之上完成 Qt Widgets 实时监控界面。新增 `MonitoringViewModel` 统一派生按钮门禁、中文状态、字段格式、模拟源和陈旧标识；`QtSerialPortCatalog` 只负责运行时枚举；`MainWindow` 只绑定展示模型，不访问串口、RTU、CRC 或寄存器地址。

界面支持：

- 运行时串口枚举、端口选择、Slave ID、超时和固定 115200 8N1 参数展示；
- 连接、断开、恢复、开始和停止监控；
- 100、500、1000、2000 ms 目标周期，以及实际周期和 overrun；
- A/B/C 相温度、模拟环境温度、光敏电压、四路告警、设备状态、Firmware 和 uptime；
- 请求、成功、失败、超时、成功率和 RTT；
- 无快照、实时、退化、离线、停止和断开时的明确数据有效性/陈旧状态。

实现中发现生产 `QSerialPortModbusClient` 的所有权完成事件只发送了 `ownershipChanged`，而 Controller 契约还要求对应的 `controlCompleted`。真实后端因此会停在 `ConnectedIdle`，Fake 测试没有暴露该差异。修复后，真实后端从 `OwnershipResult` 生成与 Fake 一致的 Acquire/Release `controlCompleted`，并补充回归断言；未修改公开 API、通信线程模型或 Modbus 协议。

## 2. 构建与自动化验证

全新目录 `build-host-task012-final` 使用 Qt 6.8.3、MSVC 19.51、CMake/Ninja 完成配置和清洁构建。CTest 11/11 通过：

1. LogEntry；
2. Qt SerialBus 探针；
3. QSerialPort RTU API；
4. RegisterCodec；
5. 通信类型；
6. RTU Codec；
7. Fake 契约；
8. QSerialPort 后端；
9. 监控核心；
10. offscreen 监控 UI；
11. 应用 smoke test。

`host.communication.qserialport_backend` 和 `host.monitoring.ui` 另执行 `until-fail:10`，两项均连续 10 轮通过。

offscreen UI 测试覆盖运行时枚举、多端口不隐式选择、固定串口参数、所有按钮/字段门禁、完整快照与单位、模拟环境源、告警和状态字、停止后的陈旧数据、连续三批超时后的 Degraded/Offline、下一完整批次恢复、实际周期/overrun、打开失败和显式恢复。

## 3. 长时测试环境

| 项目 | 实际值 |
|---|---|
| Host/Qt | 验证工具 0.1.0；Qt 6.8.3 |
| DUT | NUCLEO-F411RE；Firmware 0.2；USART1_RS485 构建 |
| 端口 | 运行时显式选择 COM6；MacroSilicon `345F:3020`；序列号 `A02001JS` |
| 其他枚举端口 | COM3；ST-LINK VCP `0483:374B`；序列号 `0671FF555185754867222928` |
| 串口 | 115200 8N1，无流控，Slave ID 1，500 ms 超时，无隐式重试 |
| 周期/时长 | 500 ms；请求 1800 s |
| RS485 台架 | 约 20 cm 安全低压点对点；自动换向 TTL-RS485；DTECH USB-RS485 |

验证入口显示真实 `MainWindow`，复用生产 ViewModel、Controller、Monitor 和 `QSerialPortModbusClient`。每个请求保存 RequestId、状态、RTT、TX/RX ADU 和错误；每个批次保存实际周期、耗时、健康、统计和 uptime；100 ms UI 心跳记录事件循环最大迟到量。

## 4. 30 分钟结果

| 指标 | 结果 |
|---|---:|
| 实际单调时钟时长 | 1,800,007 ms |
| 批次 | 3,524/3,524 成功 |
| 请求 | 17,620/17,620 成功 |
| 失败/超时 | 0/0 |
| Degraded/Offline | 0/0 次 |
| RTT 最小/平均/最大 | 14.348/31.016/67.783 ms |
| 实际周期最小/平均/最大 | 490.2/510.847/567.5 ms |
| 批次耗时最小/平均/最大 | 144.4/180.082/540.5 ms |
| overrun | 1 次（3,524 批次的 0.028%） |
| UI 心跳最大迟到 | 244 ms，低于 1,000 ms 门限 |
| uptime | 5008→6831 s |
| 最终状态/告警 | `0x21`/`0x00` |
| 最终温度 | A/B/C/环境 28.2/28.0/28.2/25.0 ℃ |
| 最终光敏 | 1419 mV |

唯一一次 overrun 没有产生重叠请求、队列增长、失败、超时或健康降级；下一批继续成功，因此判定为单次调度超限，不是通信异常。完整运行中所有 17,620 条 TXN 都是 `Succeeded`，所有 3,524 条 BATCH 都完整、Online，原始审计没有发现不一致。

原始文件为 `output/logs/task012_rs485_ui_30min.log`（本地证据，不进入 Git），大小 1,727,700 B，22,835 行，SHA-256：`9BD9E31893B5B5B0BDB44B8A4A6DEEE759BDEC819DC33C7DDB96A88EFB397898`。

## 5. 设备复位与显式恢复

使用 STM32CubeProgrammer 2.23.0 对当前 `USART1_RS485` 目标执行写入、校验和 MCU 复位。UI 实机恢复记录得到：

- 健康状态 Online→Degraded→Offline；
- 离线时 UI 明确显示“陈旧（设备离线，保留最后成功快照）”和响应超时，不覆盖最后成功值；
- UI 心跳最大迟到 46 ms，没有冻结；
- 验证编排只调用现有 ViewModel 命令，执行显式断开、显式重连和重新开始监控；未在产品代码增加自动重连；
- 重连后 Offline→Online，连续 10 个批次成功，恢复流程 `PASS`；
- 随后的 5 秒稳定复检为 10/10 批次、50/50 请求成功，零失败/超时，uptime 33→37 s，最终 A/B/C/环境温度 28.2/27.9/28.0/25.0 ℃，状态 `0x21`、告警 `0x00`。

恢复原始文件 `output/logs/task012_rs485_ui_recovery.log` 的 SHA-256 为 `26AAC524BFC76EC760FFB38104D38CEAAB9B9CA5C7886CA9A2A32B231402C8AF`；复检文件 `output/logs/task012_rs485_ui_post_recovery.log` 的 SHA-256 为 `3DF3A35DBDA019C5C774F12219495829D70E113FDEDA815704D6F8E8B8B411F0`。

验证期间曾误选早于 TASK-009 的默认 `USART2_VCP` 旧 ELF，导致一次无响应的中间运行。该结果已排除，不用于验收；随后从当前源码在独立 `build/RS485` 目录显式配置 `OMS555TV_MODBUS_TRANSPORT=USART1_RS485`，全量构建并经 Programmer 校验后恢复。此事件说明 RS485 烧录不得依赖未核对目标类型和时间戳的缓存产物，README 已补充显式 RS485 构建要求。

## 6. 最终评审

- 必须修复：无；真实后端所有权完成契约差异已修复并有回归测试。
- 建议修复：无阻塞项；后续可把 UI 心跳从“每 10 次定时回调记录”改为按单调时钟整秒采样，以便日志行数更接近墙钟秒数，但不影响本次最大迟到判定。
- 可选优化：趋势曲线、阈值配置和通信调试属于 TASK-013 或 PRD 可选范围，不纳入本任务。

结论：TASK-012 的功能、自动化测试、30 分钟真实 RS485、设备复位、陈旧状态、UI 响应和显式恢复均通过，无必须修复项。结论仅适用于当前安全低压短距离台架，不外推到 8/24 小时、工业长线、隔离或 EMC。
