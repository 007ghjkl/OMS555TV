#pragma once

#include "communication/IModbusClient.h"

#include <QSignalSpy>
#include <QTest>

#include <functional>

namespace oms555tv::communication::test {

// TASK-008 的生产 Facade 测试可复用这些函数，只需提供对应的无硬件驱动器。
inline void verifyDisconnectedSubmissionContract(IModbusClient &client)
{
    ModbusConnectionConfig invalidConfig;
    const auto invalidOpen = client.open(invalidConfig);
    QVERIFY(!invalidOpen.accepted());
    QVERIFY(!invalidOpen.operationId.has_value());
    QCOMPARE(invalidOpen.rejection->code, ErrorCode::EmptyPortName);

    RequestOptions options;
    options.owner = CommunicationOwner::Testing;
    const auto invalidRead = client.readHoldingRegisters(device::PduAddress(0), 0, options);
    QVERIFY(!invalidRead.accepted());
    QVERIFY(!invalidRead.requestId.has_value());
    QCOMPARE(invalidRead.rejection->code, ErrorCode::InvalidQuantity);

    const auto disconnectedRead = client.readHoldingRegisters(
        device::PduAddress(0), 1, options);
    QVERIFY(!disconnectedRead.accepted());
    QVERIFY(!disconnectedRead.requestId.has_value());
    QCOMPARE(disconnectedRead.rejection->code, ErrorCode::NotConnected);
}

inline void verifyCancellationExactlyOnceContract(
    IModbusClient &client,
    const std::function<void()> &driveAcceptedRequestToInFlight)
{
    QSignalSpy completed(&client, &IModbusClient::requestCompleted);
    RequestOptions options;
    options.owner = CommunicationOwner::Testing;
    options.responseTimeout = std::chrono::milliseconds(500);
    const auto submission = client.readHoldingRegisters(
        device::PduAddress(0), 1, options);
    QVERIFY(submission.accepted());
    driveAcceptedRequestToInFlight();

    QVERIFY(client.cancelRequest(*submission.requestId).isAccepted);
    QCOMPARE(completed.count(), 1);
    const auto result = qvariant_cast<ModbusRequestResult>(completed.at(0).at(0));
    QCOMPARE(result.requestId, *submission.requestId);
    QCOMPARE(result.state, RequestState::Cancelled);
    QCOMPARE(result.error->code, ErrorCode::CancelledByCaller);

    const auto duplicate = client.cancelRequest(*submission.requestId);
    QVERIFY(!duplicate.isAccepted);
    QCOMPARE(duplicate.rejection->code, ErrorCode::RequestNotFoundOrCompleted);
    QCOMPARE(completed.count(), 1);
}

} // namespace oms555tv::communication::test
