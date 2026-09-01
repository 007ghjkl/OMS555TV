# Qt SerialBus 原始帧能力技术验证

> 状态：已完成
> 日期：2026-09-01
> 环境：Qt 6.8.3 MSVC 2022 x64 包

## 1. 验证目标

确认 Qt SerialBus 的公开 API 是否能满足后续测试平台对以下能力的要求：

- 发送自定义 Modbus 请求；
- 获取原始 TX/RX 证据；
- 注入 CRC 错误等非法 RTU 帧。

## 2. 已验证证据

本机 Qt 6.8.3 公开头文件提供：

- `QModbusClient::sendRawRequest(const QModbusRequest &, int)`；
- `QModbusReply::rawResult()`；
- `QModbusReply::intermediateErrors()`。

`host.technical.qtserialbus_raw_pdu` 测试已实际构造 `QModbusRequest`，验证功能码和 PDU 数据保持不变，并在编译期/运行期确认 `sendRawRequest` 是可调用的公开成员。该测试已由 CTest 执行通过。

## 3. 能力边界

这里的“Raw”指 Modbus PDU，不是串口线上完整的 RTU ADU。公开 API 不提供以下能力：

- 取得包含从站地址、PDU 和 CRC 的完整发送字节序列；
- 按原样取得完整接收 RTU ADU；
- 指定或故意破坏 CRC 后发送非法帧；
- 精确控制 RTU 帧间隔和非标准字节时序。

`intermediateErrors()` 可以报告响应 CRC 等中间错误，但不能替代完整原始帧证据，也不能用于主动 CRC 故障注入。Qt 私有头文件不属于稳定 API，本项目不依赖它们。

## 4. 决策

Phase 0 只保留最小 `IModbusClient` 抽象，不锁死后端。后续选择如下：

- 普通监控和标准功能测试可评估 Qt SerialBus 后端，以降低实现复杂度；
- 完整 TX/RX 报文留证、CRC 故障注入和精确时序测试，应采用基于 QSerialPort 的受控 Modbus RTU 后端，或增加独立的传输层旁路采集。

在后续通信 Spec 确认前，不实现完整协议状态机。
