# 测试用例目录

> 状态：Phase 5/6 用例规划基线；TASK-014 已固化 v1 Schema、Loader 与断言语义，正式执行与用例结果仍待 TASK-015/016。

| ID | 类别 | 名称 | 类型 | 自动化方式 | 当前状态 |
|---|---|---|---|---|---|
| TC-F001 | 功能 | 读取 A 相温度 | read_register | 自动 | 待实现 |
| TC-F002 | 功能 | 读取四路温度 | read_registers | 自动 | 待实现 |
| TC-F003 | 功能 | 读取告警阈值 | read_registers | 自动 | 待实现 |
| TC-F004 | 功能 | 写入阈值并回读 | write_and_verify | 自动 | 待实现 |
| TC-F005 | 功能 | 读取固件版本 | read_registers | 自动 | 待实现 |
| TC-F006 | 功能 | 读取光敏模拟电压 | read_register | 自动 | 待实现 |
| TC-F007 | 功能 | 环境温度模拟状态位 | read_registers | 自动/Fake | 待实现 |
| TC-P001 | 协议 | 0x03 正常读取 | read_register | 自动 | 待实现 |
| TC-P002 | 协议 | 0x06 正常写入 | write_register | 自动 | 待实现 |
| TC-P003 | 协议 | 非法功能码返回 0x01 | expect_exception | 待定 Raw Frame | 待实现 |
| TC-P004 | 协议 | 非法地址返回 0x02 | expect_exception | 自动 | 待实现 |
| TC-P005 | 协议 | 非法数据返回 0x03 | expect_exception | 自动 | 待实现 |
| TC-P006 | 协议 | 响应超时 | expect_timeout | 自动 | 待实现 |
| TC-P007 | 协议 | 连续请求串行执行 | sequence | 自动 | 待实现 |
| TC-B001 | 边界 | 温度最小值解析 | range | 自动/Fake | 待实现 |
| TC-B002 | 边界 | 温度最大值解析 | range | 自动/Fake | 待实现 |
| TC-B003 | 边界 | 阈值最小值写入 | write_and_verify | 自动 | 待实现 |
| TC-B004 | 边界 | 阈值最大值写入 | write_and_verify | 自动 | 待实现 |
| TC-B005 | 边界 | 超范围阈值被拒绝 | expect_exception | 自动 | 待实现 |
| TC-D001 | 一致性 | 负温度有符号解析 | consistency | 自动/Fake | 待实现 |
| TC-D002 | 一致性 | 运行时间高低字组合 | consistency | 自动/Fake | 待实现 |
| TC-R001 | 恢复 | RS485 断线与恢复 | guided_recovery | 半自动 | 待实现 |
| TC-R002 | 恢复 | 设备复位后恢复 | guided_recovery | 半自动 | 待实现 |
| TC-S001 | 稳定性 | 可配置连续轮询 | stability | 自动 | 待实现 |

当前目录列出 24 个候选用例，不代表已实现或已通过。TASK-014 已完成输入契约，`testcases/examples/phase5-schema-example.json` 仅为中文 Schema 示例，不计入正式用例；TASK-015/016 仍需实现 TestEngine 和至少 5 条真实 RS485 最小套件。其余候选用例与完整 20 条有效套件仍属于后续 Phase 6，用例 JSON 应在对应执行器和数据范围确认后逐批落地。
