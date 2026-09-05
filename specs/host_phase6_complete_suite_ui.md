# Host Phase 6 正式自动化套件与 UI 集成技术规范

> 状态：已实现并验证
>
> 日期：2026-09-05
>
> 适用任务：TASK-019

## 1. 目标与边界

本规范将 TASK-017/018 的 v2 输入和执行能力落成正式测试资产，并扩展现有 Qt 自动化测试页面。TASK-019 只使用 Fake、虚拟时间和 offscreen UI 完成确定性验证，不执行真实 RS485，不实现物理拔插/复位、Raw Frame 或 HTML/PDF 报告。

## 2. 正式套件划分

### 2.1 真实 RS485 主套件

`testcases/phase6/phase6-rs485-full.json` 固定包含 20 条启用且 ID 唯一的 v2 用例：

| 类别 | 数量 | 正式 ID |
|---|---:|---|
| functional | 8 | TC-F001～TC-F008 |
| protocol | 4 | TC-P001、TC-P002、TC-P004、TC-P007 |
| boundary | 4 | TC-B003～TC-B006 |
| consistency | 2 | TC-D002、TC-D003 |
| recovery | 1 | TC-R-AUTO-001 |
| stability | 1 | TC-S001 |

主套件只使用 `environment=both` 或 `real_rs485`。动态数据使用范围、逐元素或位掩码；Firmware 版本使用稳定序列。阈值写入只使用 `write_and_verify`，必须写前读取、独立回读并恢复运行前原值。自动恢复只证明预期远端异常后新的合法请求成功，不代表物理链路自动重连。

### 2.2 Fake 边界套件

`testcases/phase6/phase6-fake-boundaries.json` 固定包含 TC-P006、TC-B001、TC-B002、TC-D001。它们分别验证确定性无响应、int16 最小/最大解释和负温度十进制缩放，只能计入 Fake 证据。

两个套件合计 24 个正式 ID，互不重复。覆盖矩阵保存 ID、套件路径、环境、预期和清理策略的唯一映射。

## 3. 执行与 Fake 脚本

- 自动测试必须经 `TestCaseLoader` 从正式 JSON 加载，不在测试中重建用例模型。
- Fake 脚本逐 ID 声明期望请求和结果；缺失、增加或重排导致脚本不匹配时测试失败。
- 主套件 Fake 全通过路径执行全部 20 条用例，包括 10 分钟 stability 的 600 次虚拟采样。
- 另行注入断言 FAIL、通信 ERROR、中止和恢复失败，证明终态与清理优先级。
- 不调用 `sleep()`；10 分钟 stability 使用共享虚拟单调时钟瞬时推进。

## 4. UI 展示契约

页面只读取 `TestAutomationController` 与 `TestResultManager`，不得重新计算 Engine 断言或统计。

- 当前步骤：显示 case、步骤类型、sequence step ID/index/repetition、stability iteration、attempt 和 RequestId。
- 用例详情：显示 sequence 每个不可变步骤的状态、预期、实际、耗时和 attempt sequence。
- 稳定性详情：显示 total/success/failure/timeout、有效/缺失 RTT、RTT min/avg/max。
- 证据详情：显示保留策略、总数、保留/丢弃数、失败保留数、容量、说明和 Session ID。
- 长循环运行期间按钮与主窗口事件循环保持响应，中止按钮可立即终止等待或取消在途请求。

## 5. 验证门禁

1. Loader 测试核对 20+4 数量、启用状态、唯一 ID、分类下限、环境和写入恢复。
2. 覆盖矩阵、JSON 与自动测试硬编码正式 ID 集合一致。
3. Engine Fake 全套进入 PASS，RequestId 唯一且最多一个请求在途；stability 结果有界但 JSONL 完整。
4. UI offscreen 测试覆盖 v2 加载、sequence 明细、稳定性进度/汇总、证据说明和中止。
5. 全新 Host 构建与全部 CTest 通过；不访问串口或开发板。

## 6. 评审结论

20 条主套件独立满足 TASK-019 的六类数量门槛，4 条 Fake 套件不会混入 TASK-020 实机“全部运行”。写入安全、动态值预期、自动/物理恢复边界、长时默认配置和 UI 数据来源均已确定，无架构或协议未决项，可以进入实现。
