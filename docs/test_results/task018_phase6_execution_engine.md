# TASK-018 复合与稳定性执行核心验证记录

## 1. 验证范围

本记录对应 `TASK-018`，验证 Host Phase 6 的 sequence、预期超时、数据一致性和稳定性执行核心。验证全部使用 `FakeModbusClient`、共享手动调度器和 Qt offscreen 测试，不访问串口、真实 RS485、开发板或 Firmware。

## 2. 环境

- Windows 11 x64
- Qt 6.8.3 `msvc2022_64`
- MSVC 19.51
- CMake + Ninja，Debug
- 全新构建目录：`build-host-task018`

## 3. 自动化结果

全新配置和完整构建成功，CTest 19/19 通过，最终回归总耗时 6.56 秒。

`host.testing.engine` 共 33 个测试，覆盖：

- sequence 顺序、步骤延迟、重复、逐步断言、步骤级 retry、继续/停止策略、通信错误、预算耗尽和等待态中止；
- standalone 与 sequence 内 `expect_timeout` 的结构化 `ResponseTimeout`，以及正常响应、远端异常和串口错误分类；
- consistency 多样本、虚拟间隔、低字在低地址的 uint32 非递减断言；
- stability 成功、CRC 失败、超时、失败率边界、RTT min/avg/max、零有效 RTT、缺失 RTT 和请求耗时超过 interval 时不追赶；
- 10 分钟、1 小时、8 小时和 24 小时虚拟运行，结果 attempt 数始终不超过 `evidence_sample_limit`；
- 活动 Session ID 进入 suite/case 结果，内存只保留代表证据时 JSONL 仍包含全部 `request_completed` 与 `request_attempt`；
- 中止后不再启动迭代，并返回 `CONNECTED_IDLE`、释放 Testing owner；
- Phase 5 基础处理器、自动化 UI 和应用 smoke test 回归。

## 4. 长时间验证边界

10 分钟至 24 小时场景均通过虚拟单调时钟瞬时推进，没有调用 `sleep()`，也没有启动超过 5 分钟的人工或实机测试。TASK-018 非范围内的正式 20+ JSON 套件、真实 RS485 全量运行、物理断线和设备复位留给后续任务。

## 5. Review 结论

- 必须修复：无。
- 建议修复：无。
- 可选优化：TASK-019 在 UI 中展示复合步骤和稳定性摘要；TASK-020 使用正式套件执行真实 RS485 验收。

TASK-018 验收标准已满足，可以进入 TASK-019。
