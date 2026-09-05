# Host Phase 6 需求—用例—环境—证据覆盖矩阵

> 状态：TASK-017 覆盖模型已定义；正式用例由 TASK-019 建立，真实 RS485 结果由 TASK-020 记录。

## 1. 环境与证据口径

| 环境 | 用途 | 可以证明 | 不能冒充 |
|---|---|---|---|
| Fake | 确定性极值、无响应、虚拟时间、错误分支 | Schema/Engine 语义、边界与失败分类 | 真实串口、电气层、物理恢复 |
| 真实 RS485 | 当前安全低压点对点台架 | DUT 寄存器、协议响应、RTT、写入恢复与长期请求 | 工业长线、隔离、EMC、人工拔插自动恢复 |
| 两者 | 同一目标分别做确定性和实机验证 | 逻辑边界与台架行为一致 | 未执行的环境结果 |

每个通信步骤至少关联 case/step/RequestId、TX、RX、RTT、原始寄存器、解释值、断言与终态。稳定性内存只保留有界代表性证据，完整 attempt 进入 JSONL；报告只消费不可变结果，不重新计算 Engine 语义。

## 2. 覆盖矩阵

| 需求/候选 ID | 类别 | v2 表达能力 | 计划环境 | 核心证据 | 实现/验收任务 |
|---|---|---|---|---|---|
| TC-F001 A 相温度 | 功能 | read_register + int16 range/scale | 两者 | raw、符号值、0.1 ℃工程值 | TASK-019/020 |
| TC-F002 四路温度快照 | 功能 | read_registers + elements | 两者 | 单请求四元素索引与范围 | TASK-019/020 |
| TC-F003 四路阈值 | 功能 | read_registers + elements | 两者 | 四元素 int16/scale | TASK-019/020 |
| TC-F004 阈值写回读 | 功能 | write_and_verify | 两者 | 写前值、0x06、独立0x03、恢复 | TASK-019/020 |
| TC-F005 固件版本 | 功能 | register_sequence | 两者 | 地址39～40原始序列 | TASK-019/020 |
| TC-F006 光敏电压 | 功能 | uint16 range | 两者 | raw、mV范围 | TASK-019/020 |
| TC-F007 状态位 | 功能 | elements/bitmask | 两者 | 告警/设备状态保留位与模拟位 | TASK-019/020 |
| TC-F008 运行时间读取 | 功能 | consistency + uint32 range | 两者 | 低/高字及组合值 | TASK-019/020 |
| TC-P001 0x03 正常读取 | 协议 | read_register(s) | 两者 | TX/RX、功能码、数据 | TASK-019/020 |
| TC-P002 0x06 正常写入 | 协议 | write_and_verify | 两者 | 回显、独立回读、恢复 | TASK-019/020 |
| TC-P003 非法功能码 0x01 | 协议 | 当前不纳入 v2 标准请求 | 后续 Raw Frame | 原始非法帧与0x01 | 非 TASK-017～020 |
| TC-P004 非法地址 0x02 | 协议 | expect_exception | 两者 | 远端异常与后续合法请求 | TASK-019/020 |
| TC-P005 非法数据 0x03 | 协议 | expect_exception | 两者 | 越界阈值写与0x03 | TASK-019/020 |
| TC-P006 响应超时 | 协议 | expect_timeout + no_response fault | Fake | 仅 ResponseTimeout 判 PASS | TASK-018/019 |
| TC-P007 连续请求 | 协议 | sequence + repeat_count | 两者 | 串行 RequestId、无重叠、总预算 | TASK-018～020 |
| TC-B001 温度最小值 | 边界 | elements/range + int16 | Fake | 0x8000→-32768与边界差异 | TASK-018/019 |
| TC-B002 温度最大值 | 边界 | elements/range + int16 | Fake | 0x7FFF→32767与边界差异 | TASK-018/019 |
| TC-B003 阈值最小值 | 边界 | write_and_verify | 两者 | -400原始值、回读、恢复 | TASK-019/020 |
| TC-B004 阈值最大值 | 边界 | write_and_verify | 两者 | 800原始值、回读、恢复 | TASK-019/020 |
| TC-B005 超范围阈值 | 边界 | expect_exception 0x03 | 两者 | 异常且原值不变 | TASK-019/020 |
| TC-B006 寄存器末端/越界 | 边界 | expect_exception + Loader overflow | 两者/Fake | 0x02 与配置 AddressRangeOverflow | TASK-017/019/020 |
| TC-D001 负温度一致性 | 数据一致性 | elements + int16 + decimal_places | Fake | raw、二补码、缩放工程值 | TASK-017～019 |
| TC-D002 uptime 字序 | 数据一致性 | consistency + uint32 low_word_first | 两者 | 两原始字、uint32、范围 | TASK-017～020 |
| TC-D003 uptime 单调性 | 数据一致性 | consistency + non_decreasing | 两者 | 多样本组合值与差异索引 | TASK-018～020 |
| TC-R-AUTO-001 协议异常后恢复 | 自动恢复 | sequence continue_on_failure | 两者 | 异常步骤后新合法 RequestId 成功 | TASK-018～020 |
| TC-R001 RS485 拔插恢复 | 人工恢复 | 不属于自动恢复 v2 | 半自动真实硬件 | 人工确认、Offline、恢复时间 | Phase 7 |
| TC-R002 设备复位恢复 | 人工恢复 | 不属于自动恢复 v2 | 半自动真实硬件 | 人工确认、重连与恢复时间 | Phase 7 |
| TC-S001 连续轮询 | 稳定性/性能 | stability + stability_summary | Fake+真实 RS485 | 总数、成功/失败/timeout、ppm、RTT | TASK-018～020 |

## 3. 六类 Phase 6 关闭路径

| 类别 | 最低正式数量 | TASK-017 输入保证 | TASK-018 执行保证 | TASK-019 套件/UI | TASK-020 实机关闭 |
|---|---:|---|---|---|---|
| 功能 | 8 | 基础/逐元素/uint32 | 请求与断言映射 | 唯一 ID 和 Fake 全套 | 实机结果与寄存器证据 |
| 协议 | 4 | exception/timeout/sequence | 精确错误分类和串行 | Fake timeout、正式协议项 | 实机 0x03/0x06/异常恢复 |
| 边界 | 4 | int16、地址、数值上限 | 写入清理和差异 | 极值与安全写入 | 阈值最终恢复 |
| 数据一致性 | 2 | raw/符号/缩放/低高字 | 多样本组合 | 明细与差异展示 | 实机 uptime 快照 |
| 自动恢复 | 1 | continue sequence | 错误后新请求 | 明确非物理恢复 | 实机协议异常后合法请求 |
| 稳定性 | 1 | 10分钟～24小时、ppm、RTT | 虚拟长时与有界内存 | 正式10分钟配置/UI | 真实至少10分钟 |

## 4. 当前结论

TASK-017 只证明六类需求均有严格输入、环境和证据路径。未创建正式 20+ 套件、未执行 sequence/stability、未访问真实 RS485，所有计划项仍须按 TASK-018→019→020 顺序验收。
