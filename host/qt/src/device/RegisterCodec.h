#pragma once

#include "device/DeviceTypes.h"

namespace oms555tv::device {

// 分块解码要求地址和数量与对应 ReadBlockDefinition 完全一致。
[[nodiscard]] CodecResult<Measurements> decodeMeasurements(const RegisterBlock &block);
[[nodiscard]] CodecResult<AlarmThresholds> decodeThresholds(const RegisterBlock &block);
[[nodiscard]] CodecResult<Status> decodeStatus(const RegisterBlock &block);
[[nodiscard]] CodecResult<Diagnostics> decodeDiagnostics(const RegisterBlock &block);
[[nodiscard]] CodecResult<FirmwareVersion> decodeFirmwareVersion(const RegisterBlock &block);
// 完整快照接受任意块顺序，但要求五个已定义读块各出现一次。
[[nodiscard]] CodecResult<DeviceSnapshot> decodeDeviceSnapshot(
    const QVector<RegisterBlock> &blocks);
// 输入单位为摄氏度；成功结果可直接用于后续单寄存器写入与定点回读比较。
[[nodiscard]] CodecResult<RegisterWrite> encodeAlarmThreshold(TemperatureChannel channel,
                                                              double celsius);

} // namespace oms555tv::device
