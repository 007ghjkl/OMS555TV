#pragma once

namespace oms555tv::communication {

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Faulted,
};

class IModbusClient
{
public:
    virtual ~IModbusClient() = default;

    [[nodiscard]] virtual ConnectionState connectionState() const noexcept = 0;
};

} // namespace oms555tv::communication
