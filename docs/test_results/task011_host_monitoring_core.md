# TASK-011 Host 应用状态与监控核心验证记录

> 日期：2026-09-04
> 结论：通过
> 验证范围：无 QWidget、无串口、无真实硬件的 Host 核心与回归测试

## 1. 实现范围

- 新增集中式 `AppStateController`，管理 `DISCONNECTED`、`CONNECTED_IDLE`、`MONITORING`、`TESTING`、`STOPPING` 和 `ERROR`。
- 新增 `MonitorService`，仅通过 `IModbusClient` 和 Monitor owner 访问通信。
- 每批按 `RegisterMap::deviceSnapshotReadBlocks` 严格串行读取五块，并通过 `RegisterCodec` 生成完整 `DeviceSnapshot`。
- 新增不可变批次/快照元数据、结构化监控错误、Online/Degraded/Offline 健康状态和饱和通信统计。
- 新增 Qt 生产调度器接口及手动虚拟时间测试适配器；测试不使用真实 `sleep()`。
- Fake 仅增加一次性异步 open 失败注入，用于上层连接失败状态测试；未修改 `IModbusClient` 契约。

## 2. 构建环境

| 项目 | 值 |
|---|---|
| 操作系统 | Windows x64 |
| Qt | 6.8.3 `msvc2022_64` |
| 编译器 | MSVC 19.51.36256.0 |
| 构建系统 | CMake + Ninja |
| 构建类型 | Debug |
| 全新构建目录 | `build-host-task011-final` |

全新配置和 61 个构建步骤均成功完成。

## 3. 监控核心测试覆盖

`host.monitoring.core` 覆盖以下场景：

1. 五个合法读块的顺序、地址、数量和单在途约束；
2. 完整快照的批次 ID、请求 ID、UTC 时间和领域值；
3. 每个块位置分别发生 Timeout、远端异常、CRC、协议和串口错误；
4. 编解码失败不发布快照，下一批成功后恢复；
5. 失败批次保留上一成功快照及原时间戳，连续三批失败进入 Offline，成功批次恢复 Online；
6. 请求、成功、失败、超时、取消、连续失败、RTT 样本与累计值逐项核对；
7. 150 ms 批次对 100 ms 目标周期产生 overrun，下一批实际间隔为 150 ms，不重叠、不补发；
8. 首批尚未触发、请求 queued 和请求 in-flight 三种停止路径；
9. Monitor/Testing 所有权互斥、连接/关闭、重复命令、断开期间状态与 owner 释放；
10. 异步 open 失败、意外后端断开和显式恢复；
11. 周期/超时配置边界与 64 位计数饱和边界。

所有场景使用 `FakeModbusClient` 和手动调度器推进虚拟时间，没有不受控等待。

## 4. 全量 CTest 结果

执行命令：

```powershell
ctest --test-dir build-host-task011-final --output-on-failure
```

最终 `--clean-first` 全量重建后结果：10/10 通过，总耗时 4.67 秒；Fake 合同与监控核心另各重复运行 10 轮，均通过。

| CTest | 结果 |
|---|---|
| `host.logging.logentry` | PASS |
| `host.technical.qtserialbus_raw_pdu` | PASS |
| `host.technical.qserialport_rtu_public_api` | PASS |
| `host.device.registercodec` | PASS |
| `host.communication.types` | PASS |
| `host.communication.rtu_codec` | PASS |
| `host.communication.fake_contract` | PASS |
| `host.communication.qserialport_backend` | PASS |
| `host.monitoring.core` | PASS |
| `host.application.smoke` | PASS |

## 5. 验收核对

- Technical Spec 已明确状态转换、健康阈值、周期语义、停止策略、统计口径和错误恢复，无未决实现选择。
- 五块轮询严格串行；不发布部分或跨批拼接快照。
- 目标周期小于实际批次耗时时不重叠、不积压，并记录 overrun/effective interval。
- 无在途、queued、in-flight 停止均受控结束，Controller 最终释放 owner。
- Testing 只实现状态与所有权迁移边界，没有提前实现 TestEngine。
- 新核心不依赖 Qt Widgets，不直接访问 QSerialPort，不修改 Firmware。

## 6. Review 结论

### 必须修复

无。

### 建议修复

无阻塞 TASK-012 的建议项。

### 可选优化

- 后续会话日志任务可持久化当前内存统计和批次结果。
- TASK-012 可在 UI 中展示 effective interval 与 overrun，但不得重新计算或改变健康判定。

## 7. 范围声明

本记录只证明 TASK-011 无硬件核心行为。未实现最终监控 UI，也未执行 TASK-012 要求的 30 分钟真实 RS485 连续监控；不得据此声称 Phase 4 整体验收完成。
