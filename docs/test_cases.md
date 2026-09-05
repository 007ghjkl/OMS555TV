# 测试用例目录

> 状态：Phase 5 基础套件已实测；TASK-019 的 Phase 6 正式 20+4 套件已完成 Fake/offscreen 验证，真实 RS485 全量验收仍待 TASK-020。

## 1. Phase 6 正式目录

| ID | 类别 | 名称 | 类型 | 环境 | 套件 | 当前状态 |
|---|---|---|---|---|---|---|
| TC-F001 | 功能 | A 相温度符号与范围 | read_register | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-F002 | 功能 | 四路温度单请求快照 | read_registers | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-F003 | 功能 | 四路告警阈值快照 | read_registers | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-F004 | 功能 | A 相阈值写回读与恢复 | write_and_verify | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-F005 | 功能 | Firmware 版本序列 | read_registers | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-F006 | 功能 | 光敏模拟电压范围 | read_register | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-F007 | 功能 | 告警与设备状态位 | read_registers | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-F008 | 功能 | 运行时间双字快照 | read_registers | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-P001 | 协议 | 0x03 正常读取 | read_register | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-P002 | 协议 | 0x06 正常写入 | write_and_verify | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-P004 | 协议 | 非法地址返回 0x02 | expect_exception | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-P007 | 协议 | 连续请求严格串行 | sequence | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-B003 | 边界 | 阈值最小值写入边界 | write_and_verify | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-B004 | 边界 | 阈值最大值写入边界 | write_and_verify | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-B005 | 边界 | 阈值超上限返回 0x03 | expect_exception | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-B006 | 边界 | 寄存器末端跨界返回 0x02 | expect_exception | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-D002 | 数据一致性 | 运行时间低高字组合 | consistency | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-D003 | 数据一致性 | 运行时间非递减 | consistency | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-R-AUTO-001 | 自动恢复 | 协议异常后合法请求恢复 | sequence | both | 主套件 | Fake PASS；实机待 TASK-020 |
| TC-S001 | 稳定性 | 10 分钟连续轮询稳定性 | stability | real_rs485 | 主套件 | 虚拟 10 分钟 PASS；实机待 TASK-020 |
| TC-P006 | 协议 | 确定性响应超时 | expect_timeout | fake | Fake 套件 | Fake PASS |
| TC-B001 | 边界 | int16 最小值解释 | read_register | fake | Fake 套件 | Fake PASS |
| TC-B002 | 边界 | int16 最大值解释 | read_register | fake | Fake 套件 | Fake PASS |
| TC-D001 | 数据一致性 | 负温度符号与十进制缩放 | read_registers | fake | Fake 套件 | Fake PASS |

“主套件”指 `testcases/phase6/phase6-rs485-full.json`，“Fake 套件”指 `testcases/phase6/phase6-fake-boundaries.json`。每条正式 ID 的预期、环境和清理策略见 `docs/phase6_coverage_matrix.md`。

## 2. 验证口径

TASK-019 自动测试从正式 JSON 原样加载并执行：主套件 20/20 PASS，Fake 边界套件 4/4 PASS；正式稳定性配置保持 10 分钟，CTest 通过共享虚拟单调时钟完成 600 次采样。另有受控断言 FAIL、通信 ERROR、恢复失败升级 ERROR 和中止路径，因此“Fake PASS”表示测试资产及 Host 执行语义通过，不表示真实设备通过。

所有成功阈值写入均先保存原值、独立回读并恢复；被 Firmware 以 0x03 拒绝的 TC-B005 不产生成功写入。自动恢复 TC-R-AUTO-001 只证明协议异常后新的合法请求成功，不包含隐式重连、物理拔插或设备复位。

## 3. 与其他阶段的边界

- Phase 5 `testcases/functional/phase5-smoke.json` 的 COM6 8/8 实测结论继续有效，但不能替代 Phase 6 主套件实测。
- TC-P003 非法功能码依赖 Raw Frame，未纳入本阶段。
- 原候选 TC-P005 与 TC-B005 目标重复，不作为另一个正式 ID 计数。
- TC-R001 物理拔插、TC-R002 设备复位和传感器人工操作属于 Phase 7。
- TASK-020 完成前，不得宣称 Phase 6 或 20 条主套件真实 RS485 验收已完成。
