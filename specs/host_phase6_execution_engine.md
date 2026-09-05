# Host Phase 6 复合、超时与稳定性执行核心技术规范

> 状态：已实现并完成最终评审
>
> 日期：2026-09-05
>
> 适用任务：TASK-018

## 1. 目的与边界

本文定义 TASK-017 规范化 v2 用例在 `TestEngine` 中的执行语义。实现复用既有 `AppStateController`、Testing owner、`IModbusClient`、`TestResultManager` 和 `SessionLogService`，不读取原始 JSON、不访问 QWidget、不增加并发请求，也不修改 Firmware、寄存器表或串口线程模型。

本任务实现 sequence、expect_timeout、consistency 和 stability 的无阻塞执行核心。正式 20+ 套件和页面展示由 TASK-019 完成，真实 RS485 全量验收由 TASK-020 完成。

## 2. 调度与时间

- Engine 使用注入的调度器提供单调时间、UTC 和一次性延迟；生产环境复用 `QtMonitorScheduler`，测试复用手动虚拟调度器。
- duration、interval、case budget 和请求启动间隔只使用单调时间；UTC 只记录审计时间戳。
- 延迟期间不占用通信请求。延迟到期回调必须检查当前运行、用例和代次，迟到回调不得启动请求。
- 公共命令继续同步接受/拒绝、异步完成，所有信号在 Engine 所在线程发出。
- 同一时刻最多一个用例、一个 Modbus 请求在途。

## 3. 状态机

### 3.1 Engine 与用例

Engine 保持 `IDLE -> RUNNING -> ABORTING -> RELEASING -> IDLE`。用例保持 `NOT_RUN -> RUNNING -> PASS|FAIL|ERROR`，未执行项使用 `SKIPPED`。

运行中的用例任一时刻只能处于以下子状态之一：

1. 处理器计算下一决策；
2. 等待有界延迟；
3. 等待唯一请求完成；
4. 生成终态；
5. 必要的写入清理。

处理器决策只能包含“下一请求”“一次延迟”或“用例终态”之一；其他组合属于内部不变量错误。

### 3.2 sequence

sequence 按 `repetition -> step index` 顺序执行。每个逻辑步骤状态为 `RUNNING -> PASS|FAIL|ERROR`，并保存 repetition、step ID/index、断言、实际结果、错误和关联 attempt sequence。

- `delay_before_ms` 在每次 repetition 的对应步骤前执行。
- 每次 retry 只重试当前逻辑步骤，使用步骤自己的 retry；不会重复已完成步骤。
- `stop_on_failure` 在首个 FAIL/ERROR 后结束用例。
- `continue_on_failure` 保存失败步骤并继续；最终任一 ERROR 使 case 为 ERROR，否则任一 FAIL 使 case 为 FAIL，否则为 PASS。
- sequence 不包含写步骤，因此不会绕过 `write_and_verify` 清理规则。

### 3.3 consistency

consistency 首次立即读取，后续采样在前一次请求完成后等待 `interval_ms`。成功读取恰好两个寄存器后追加样本，达到 `sample_count` 时一次性执行 uint32 范围和单调性断言。远端异常形成 FAIL，本地通信错误形成 ERROR。

### 3.4 stability

stability 首次立即启动请求。后续请求的计划开始时间为“上一次实际开始时间 + interval”；若前一请求耗时已超过 interval，则完成后立即开始下一次，不补发积压请求。到达 `duration_ms` 后不再启动新请求；duration 前已启动的最后一个请求允许在 request timeout 内完成。

每个已接受请求恰好计入一次：

- 正常成功：`successes + 1`；
- `ResponseTimeout`：`timeouts + 1`；
- 远端异常、CRC 或协议错误：`failures + 1`；
- 连接、串口、owner、取消、内部错误或提交拒绝：受控终止为 ERROR，保留已形成的部分统计。

普通失败不会提前结束稳定性用例。自然结束时使用 `stability_summary` 断言决定 PASS/FAIL。

## 4. 预期超时

standalone 或 sequence 内的 expect_timeout 都只按结构化错误判定：

- `ErrorCode::ResponseTimeout`：构造 `ActualResult::responseTimeout()`，断言 PASS；
- 正常响应：FAIL；
- 远端 Modbus 异常：FAIL；
- Cancelled、ClosedByPeer、连接、串口、CRC、协议或其他错误：ERROR。

不得匹配诊断文本，也不得把 case budget 到期当作预期超时。

## 5. Case budget、retry 与中止

- 开始延迟、请求或 retry 前检查 case 剩余预算。
- 延迟大于剩余预算时不再启动请求，产生 `ERROR/CaseTimeout`。恰好到达预算边界的延迟允许触发一次恢复判定；恢复判定不得再接受新请求，可用于 stability 在 duration 边界自然结束，否则由请求提交前预算检查转为 `ERROR/CaseTimeout`。
- 请求 timeout 仍为 `min(request_ms, remaining case budget)`；case budget 导致的超时优先归类为 `CaseTimeout`。
- sequence 使用当前步骤 retry；其他类型使用用例 retry。断言 FAIL、远端异常、提交拒绝、中止和清理不 retry。
- 中止等待延迟时立即取消定时任务；中止在途普通请求时取消请求。两者都不再启动新步骤/迭代，并在有界时间内释放 Testing owner。
- 迟到定时器和非当前 RequestId 回调只忽略，不改变终态。

## 6. 统计与 RTT

稳定性统计包含 `total/successes/failures/timeouts`、有效和缺失 RTT 样本数，以及 RTT min/avg/max。

- 强制 `total = successes + failures + timeouts`。
- 失败率分子为 `failures + timeouts`，阈值使用 TASK-017 的整数 ppm 算法；等于阈值通过。
- RTT 只统计请求结果中存在的 RTT，缺失项计入 `missing_rtt_samples`，不能按 0 计算。
- RTT 使用整数毫秒。累计值上限由 `60000 ms * 8640000` 保证在 64 位无符号整数范围内；实现仍执行加法溢出检查。平均值为 `sum / valid_samples` 的确定性向下取整。

## 7. 结果与证据保留

### 7.1 公共结果

`TestSuiteResult` 保存活动 SessionLog 的 `sessionId`。`TestCaseResult` 增加：

- sequence 已完成逻辑步骤；
- 稳定性统计；
- 证据保留摘要：总 attempt、保留数、丢弃数、失败总数/保留数、容量与策略；
- SessionLog ID。

attempt 增加逻辑 step ID/index/repetition，保持既有 RequestId、TX/RX、RTT 和错误证据不变。v1 用例继续保留全部 attempt，现有字段语义不变。

### 7.2 stability 有界内存

容量使用 `evidence_sample_limit`，固定划分为：

1. 首条 attempt；
2. 最多一半容量的失败证据，其中保留最早失败并在溢出时持续保留最新失败；
3. 按理论最大启动数计算固定步长的均匀样本；
4. 最新一条 attempt。

合并时按 attempt sequence 排序并去重，数量永不超过配置容量。完整 `request_completed` 由诊断日志记录，Engine 对每个 attempt 继续写 `request_attempt` TEST 事件，因此内存抽样不会丢失 JSONL 全量证据。日志失败追加 `LoggingFailed`，不得伪造 PASS。

## 8. 错误优先级

从高到低：

1. owner 释放、清理、统计溢出或内部不变量错误：ERROR；
2. 用户中止：当前 case ERROR，后续 case SKIPPED；
3. case budget、致命通信错误或提交拒绝：ERROR；
4. sequence 步骤断言或 consistency/timeout 正常可判定不匹配：FAIL；
5. 全部条件满足：PASS。

`continue_on_failure` 只影响 sequence 的业务步骤推进，不得降低最终 ERROR，也不得覆盖前序失败证据。

## 9. 验证策略

- 使用 Fake 与共享虚拟调度器验证顺序、延迟、retry、失败策略、预算和中止，不调用 `sleep()`。
- 分别验证 expect_timeout 的 PASS/FAIL/ERROR 四类结构化结果。
- 验证 consistency 的逐元素执行映射、uint32 组合及单调性。
- 使用较大 interval 虚拟运行 10 分钟、1 小时、8 小时和 24 小时，验证计时无溢出、请求不并发、结果容量有界。
- 验证失败率等于/超过阈值、零有效 RTT、缺失 RTT、日志关联和 owner 释放。
- 全新构建并运行全部 Host CTest 与应用 smoke test，不访问真实串口或硬件。

## 10. 评审结论

- 调度、duration、interval 和 case budget 均使用单调时间，UTC 仅用于审计。
- sequence、consistency 和 stability 都通过统一处理器决策边界推进，Engine 不解析原始 JSON。
- 预期超时仅识别结构化 `ResponseTimeout`。
- stability 在内存有界的同时保留可定位代表证据，完整事务仍由 JSONL 关联 RequestId。
- 中止与致命错误均停止新请求并回到既有 owner 释放路径。
- 未引入新的架构、协议或第三方依赖，无未决项，可以进入实现。
