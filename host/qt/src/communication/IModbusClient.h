#pragma once

#include "communication/CommunicationTypes.h"

#include <QObject>

namespace oms555tv::communication {

class IModbusClient : public QObject
{
    Q_OBJECT

public:
    explicit IModbusClient(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IModbusClient() override = default;

    [[nodiscard]] virtual ConnectionState connectionState() const noexcept = 0;
    [[nodiscard]] virtual CommunicationOwner activeOwner() const noexcept = 0;

    virtual ControlSubmission open(const ModbusConnectionConfig &config) = 0;
    virtual ControlSubmission close(CloseMode mode = CloseMode::CancelAll) = 0;
    virtual ControlSubmission acquireOwnership(CommunicationOwner owner,
                                               HandoffMode mode) = 0;
    virtual ControlSubmission releaseOwnership(CommunicationOwner owner,
                                               HandoffMode mode) = 0;

    virtual RequestSubmission readHoldingRegisters(
        device::PduAddress startAddress,
        quint16 count,
        const RequestOptions &options) = 0;
    virtual RequestSubmission writeSingleRegister(
        device::PduAddress address,
        quint16 rawValue,
        const RequestOptions &options) = 0;
    virtual CommandAcceptance cancelRequest(RequestId requestId) = 0;

signals:
    void connectionStateChanged(oms555tv::communication::ConnectionState state);
    void controlCompleted(const oms555tv::communication::ControlResult &result);
    void ownershipChanged(const oms555tv::communication::OwnershipResult &result);
    void requestStateChanged(oms555tv::communication::RequestId id,
                             oms555tv::communication::RequestState state);
    void requestCompleted(const oms555tv::communication::ModbusRequestResult &result);
};

} // namespace oms555tv::communication
