# TASK-010 Host 真实 RS485 系统联调记录

> 结论：通过
>
> 日期：2026-09-04
>
> 对应任务：`TASK-010 Host 真实 RS485 系统联调与迁移验收`

## 1. 测试范围与环境

Host 使用生产 `QSerialPortModbusClient`、`IModbusClient`、串行队列和 `RegisterCodec`，未创建第二套 RS485 通信后端。硬件、电气连接和串口参数与 TASK-009 记录一致。

运行时枚举到：

- COM3：STMicroelectronics STLink Virtual COM Port，`0483:374B`；
- COM6：MacroSilicon USB Serial Ports，`345F:3020`，序列号 `A02001JS`；
- 本次显式选择 COM6；产品代码和默认配置未写死该端口。

## 2. Host 回归

全新目录 `build-host-task010-final` 使用 Qt 6.8.3、MSVC 19.51、CMake/Ninja 完成配置和构建。CTest 9/9 通过：

1. LogEntry；
2. Qt SerialBus 探针；
3. QSerialPort RTU API；
4. RegisterCodec；
5. 通信类型；
6. RTU Codec；
7. Fake 契约；
8. QSerialPort 后端；
9. 应用 smoke test。

## 3. 功能、异常与压力测试

正式集成工具通过 COM6 得到：

- 全部合法读块和领域快照成功：温度 27.1/26.9/27.0 ℃、环境温度 25.0 ℃、光敏 2190 mV、告警 `0x0000`、状态 `0x0021`、通信错误计数 2、固件版本 0.2；
- 四路阈值写入、0x03 回读和最终整块恢复通过，最终值为 600/600/600/400（0.1 ℃）；
- 保留地址正确分类为 `ModbusIllegalDataAddress`/`0x02`；越界值正确分类为 `ModbusIllegalDataValue`/`0x03`；
- 错误 Slave ID 返回 `ResponseTimeout`，关闭并重新打开串口后合法读取恢复；
- 连续 500 次 0x03：成功 500，失败 0，超时 0，无隐式重试；
- Host API RTT 最小/平均/最大：26.311/32.477/46.087 ms；
- Firmware 通信错误计数 2→2，连续访问未产生新错误；
- 全程保留请求 TX、响应 RX、CRC 校验、RequestId、状态和 RTT 证据；这里的 RTT 是 Host API 往返时间，不是物理传播时延。

## 4. 物理断线恢复

在阈值已恢复且端口关闭后，用户仅断开 T/R+，保持 T/R-、GND、VCC 和 USB 不变：

- 合法请求均无 RX，并在约 500 ms 后分类为 `ResponseTimeout`；
- 未执行任何阈值写入，也未收到畸形或误响应；
- 用户重连 T/R+ 后，Host 显式重新打开 COM6；完整快照恢复，20/20 次请求成功，通信错误计数保持 2→2；
- 阈值再次确认并恢复为 600/600/600/400。

## 5. 设备复位恢复

用户按住 NUCLEO RESET 时：

- 合法请求均无 RX，并在约 500 ms 后分类为 `ResponseTimeout`；
- 未收到畸形或误响应。

用户释放 RESET 并等待启动后，Host 显式重新打开/重新请求：

- 完整快照恢复，状态 `0x0021`；
- 运行时间从复位后的 42 秒重新累计，通信错误计数复位为 0；
- 20/20 次连续请求成功，RTT 最小/平均/最大为 26.824/31.434/44.770 ms；
- 阈值为默认的 600/600/600/400，测试写入后已再次恢复；
- 错误 Slave ID 超时后的关闭/重开恢复再次通过。

## 6. 评审结论

- 正确性：全量读写、异常、连续请求、断线和复位恢复均符合 TASK-010；
- 架构：复用生产 `QSerialPortModbusClient`，未修改公共 API、并发模型或寄存器语义；
- 安全与数据完整性：所有写测试均先保存基线并最终恢复，没有遗留配置；
- 范围：未实现 Phase 4、Monitor、TestEngine、Raw 公共接口或自动重连；
- 局限：结论仅适用于当前约 20 cm、无额外终端的安全低压点对点台架。

评审结论：**无必须修复项，TASK-010 完成；TASK-002 的 RS485 迁移总门禁可以关闭。**
