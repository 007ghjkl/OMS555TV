#include "communication/FakeModbusClient.h"
#include "communication/RtuCodec.h"
#include "IModbusClientContract.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace oms555tv::communication;
using oms555tv::device::PduAddress;

namespace {

ModbusConnectionConfig config(qsizetype maxPending = 64)
{
    ModbusConnectionConfig value;
    value.serial.portName = QStringLiteral("FAKE");
    value.maxPendingRequests = maxPending;
    return value;
}

RequestOptions options(CommunicationOwner owner = CommunicationOwner::Testing,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
{
    RequestOptions value;
    value.owner = owner;
    value.responseTimeout = timeout;
    return value;
}

void connectAndAcquire(FakeModbusClient &client,
                       const std::shared_ptr<ManualScheduler> &scheduler,
                       ModbusConnectionConfig connectionConfig = config(),
                       CommunicationOwner owner = CommunicationOwner::Testing)
{
    QVERIFY(client.open(connectionConfig).accepted());
    QCOMPARE(client.connectionState(), ConnectionState::Opening);
    scheduler->runUntilIdle();
    QCOMPARE(client.connectionState(), ConnectionState::Connected);
    QVERIFY(client.acquireOwnership(owner, HandoffMode::FinishInFlight).accepted());
    scheduler->runUntilIdle();
    QCOMPARE(client.activeOwner(), owner);
}

FakeStep readStep(quint16 address,
                  quint16 count,
                  FakeOutcome outcome,
                  std::chrono::milliseconds delay = std::chrono::milliseconds(0),
                  CommunicationOwner owner = CommunicationOwner::Testing,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
{
    return {{owner, ReadRequestDescriptor{PduAddress(address), count}, timeout},
            std::move(outcome),
            delay,
            {},
            {}};
}

ModbusRequestResult resultAt(const QSignalSpy &spy, int index)
{
    return qvariant_cast<ModbusRequestResult>(spy.at(index).at(0));
}

} // namespace

class FakeModbusClientTest final : public QObject
{
    Q_OBJECT

private slots:
    void asynchronousSuccessCarriesCompleteEvidence()
    {
        auto scheduler = std::make_shared<ManualScheduler>();
        FakeModbusClient client(scheduler, QDateTime::fromString(
            QStringLiteral("2026-09-03T00:00:00.000Z"), Qt::ISODateWithMs));
        QSignalSpy completed(&client, &IModbusClient::requestCompleted);
        QSignalSpy states(&client, &IModbusClient::requestStateChanged);
        connectAndAcquire(client, scheduler);

        FakeOutcome outcome;
        outcome.kind = FakeOutcomeKind::ReadSuccess;
        outcome.readValues = {0x1234, 0xFFFE};
        FakeStep step = readStep(0, 2, outcome, std::chrono::milliseconds(25));
        step.expectedTxAdu = QByteArray::fromHex("010300000002c40b");
        client.enqueueStep(std::move(step));

        const auto submission = client.readHoldingRegisters(PduAddress(0), 2, options());
        QVERIFY(submission.accepted());
        QCOMPARE(completed.count(), 0);
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(states.count(), 2);
        QCOMPARE(qvariant_cast<RequestState>(states.at(0).at(1)), RequestState::Queued);
        QCOMPARE(qvariant_cast<RequestState>(states.at(1).at(1)), RequestState::InFlight);
        scheduler->advanceBy(std::chrono::milliseconds(24));
        QCOMPARE(completed.count(), 0);
        scheduler->advanceBy(std::chrono::milliseconds(1));

        QCOMPARE(completed.count(), 1);
        const auto result = resultAt(completed, 0);
        QCOMPARE(result.requestId, *submission.requestId);
        QCOMPARE(result.state, RequestState::Succeeded);
        QVERIFY(result.success.has_value());
        const auto &read = std::get<ReadHoldingRegistersResult>(*result.success);
        QCOMPARE(read.values, outcome.readValues);
        QCOMPARE(result.evidence.txAdu, QByteArray::fromHex("010300000002c40b"));
        QVERIFY(hasValidModbusCrc(result.evidence.rxAdu));
        QCOMPARE(result.evidence.txCrcStatus, CrcStatus::Valid);
        QCOMPARE(result.evidence.rxCrcStatus, CrcStatus::Valid);
        QCOMPARE(*result.evidence.rtt, std::chrono::milliseconds(25));
        QVERIFY(result.evidence.txAcceptedUtc.has_value());
        QVERIFY(result.evidence.firstRxUtc.has_value());
        QVERIFY(client.scriptConsumed());
        QVERIFY(!client.hasNonTerminalRequests());
        QVERIFY(client.verificationError().isEmpty());
    }

    void fifoSingleInFlightAndQueueFullAreDeterministic()
    {
        auto scheduler = std::make_shared<ManualScheduler>();
        FakeModbusClient client(scheduler);
        QSignalSpy completed(&client, &IModbusClient::requestCompleted);
        connectAndAcquire(client, scheduler, config(1));

        FakeOutcome pending;
        pending.kind = FakeOutcomeKind::Pending;
        client.enqueueStep(readStep(0, 1, pending));
        FakeOutcome success;
        success.kind = FakeOutcomeKind::ReadSuccess;
        success.readValues = {22};
        client.enqueueStep(readStep(1, 1, success));

        const auto first = client.readHoldingRegisters(PduAddress(0), 1, options());
        QVERIFY(first.accepted());
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        const auto second = client.readHoldingRegisters(PduAddress(1), 1, options());
        QVERIFY(second.accepted());
        const auto rejected = client.readHoldingRegisters(PduAddress(2), 1, options());
        QVERIFY(!rejected.accepted());
        QCOMPARE(rejected.rejection->code, ErrorCode::QueueFull);
        QVERIFY(!rejected.requestId.has_value());

        QVERIFY(client.cancelRequest(*first.requestId).isAccepted);
        scheduler->runUntilIdle();
        QCOMPARE(completed.count(), 2);
        QCOMPARE(resultAt(completed, 0).requestId, *first.requestId);
        QCOMPARE(resultAt(completed, 0).state, RequestState::Cancelled);
        QCOMPARE(resultAt(completed, 1).requestId, *second.requestId);
        QCOMPARE(resultAt(completed, 1).state, RequestState::Succeeded);
        QVERIFY(first.requestId->value < second.requestId->value);
        QVERIFY(client.verificationError().isEmpty());
    }

    void scriptedFailuresUseSharedStructuredResults()
    {
        struct Case {
            FakeOutcome outcome;
            ErrorCategory category;
            ErrorCode code;
        };
        QVector<Case> cases;

        FakeOutcome remote;
        remote.kind = FakeOutcomeKind::RemoteException;
        remote.exceptionCode = 0x02;
        cases.append({remote, ErrorCategory::RemoteException,
                      ErrorCode::ModbusIllegalDataAddress});
        FakeOutcome timeout;
        timeout.kind = FakeOutcomeKind::Timeout;
        cases.append({timeout, ErrorCategory::Timeout, ErrorCode::ResponseTimeout});
        FakeOutcome crc;
        crc.kind = FakeOutcomeKind::CrcMismatch;
        cases.append({crc, ErrorCategory::Crc, ErrorCode::ResponseCrcMismatch});
        FakeOutcome protocol;
        protocol.kind = FakeOutcomeKind::ProtocolError;
        cases.append({protocol, ErrorCategory::Protocol, ErrorCode::WrongServerAddress});
        FakeOutcome serial;
        serial.kind = FakeOutcomeKind::SerialError;
        serial.serialError = ErrorCode::ResourceError;
        cases.append({serial, ErrorCategory::Serial, ErrorCode::ResourceError});

        for (const auto &testCase : cases) {
            auto scheduler = std::make_shared<ManualScheduler>();
            FakeModbusClient client(scheduler);
            QSignalSpy completed(&client, &IModbusClient::requestCompleted);
            connectAndAcquire(client, scheduler);
            client.enqueueStep(readStep(0, 1, testCase.outcome));
            const auto submission = client.readHoldingRegisters(PduAddress(0), 1, options());
            QVERIFY(submission.accepted());
            scheduler->runUntilIdle();
            QCOMPARE(completed.count(), 1);
            const auto result = resultAt(completed, 0);
            QCOMPARE(result.state, RequestState::Failed);
            QCOMPARE(result.error->category, testCase.category);
            QCOMPARE(result.error->code, testCase.code);
            QCOMPARE(result.error->requestId, submission.requestId);
            QVERIFY(result.error->evidence.has_value());
            QVERIFY(client.verificationError().isEmpty());
        }
    }

    void cancellationCompletesEachAcceptedIdExactlyOnce()
    {
        auto scheduler = std::make_shared<ManualScheduler>();
        FakeModbusClient client(scheduler);
        test::verifyDisconnectedSubmissionContract(client);
        connectAndAcquire(client, scheduler);

        FakeOutcome pending;
        pending.kind = FakeOutcomeKind::Pending;
        client.enqueueStep(readStep(0, 1, pending));
        test::verifyCancellationExactlyOnceContract(client, [scheduler] {
            scheduler->advanceBy(std::chrono::nanoseconds::zero());
        });
        scheduler->runUntilIdle();
        QVERIFY(client.verificationError().isEmpty());
    }

    void writeSuccessAndScriptMismatchAreObservable()
    {
        auto scheduler = std::make_shared<ManualScheduler>();
        FakeModbusClient client(scheduler);
        QSignalSpy completed(&client, &IModbusClient::requestCompleted);
        connectAndAcquire(client, scheduler);

        FakeOutcome writeSuccess;
        writeSuccess.kind = FakeOutcomeKind::WriteSuccess;
        client.enqueueStep({{CommunicationOwner::Testing,
                             WriteRequestDescriptor{PduAddress(9), 550},
                             std::chrono::milliseconds(500)},
                            writeSuccess,
                            {},
                            QByteArray::fromHex("010600090226d972"),
                            {}});
        const auto write = client.writeSingleRegister(PduAddress(9), 550, options());
        QVERIFY(write.accepted());
        scheduler->runUntilIdle();
        const auto writeResult = resultAt(completed, 0);
        QCOMPARE(writeResult.state, RequestState::Succeeded);
        const auto &value = std::get<WriteSingleRegisterResult>(*writeResult.success);
        QCOMPARE(value.address, PduAddress(9));
        QCOMPARE(value.rawValue, quint16(550));

        FakeOutcome pending;
        pending.kind = FakeOutcomeKind::Pending;
        client.enqueueStep(readStep(5, 1, pending));
        const auto mismatch = client.readHoldingRegisters(PduAddress(6), 1, options());
        QVERIFY(mismatch.accepted());
        scheduler->runUntilIdle();
        QCOMPARE(completed.count(), 2);
        QCOMPARE(resultAt(completed, 1).error->code, ErrorCode::FakeScriptMismatch);
        QVERIFY(client.verificationError().isEmpty());
    }

    void ownershipHandoffCancelsQueueAndWaitsForInFlight()
    {
        auto scheduler = std::make_shared<ManualScheduler>();
        FakeModbusClient client(scheduler);
        QSignalSpy completed(&client, &IModbusClient::requestCompleted);
        QSignalSpy ownership(&client, &IModbusClient::ownershipChanged);
        connectAndAcquire(client, scheduler, config(), CommunicationOwner::Monitor);
        ownership.clear();

        FakeOutcome pending;
        pending.kind = FakeOutcomeKind::Pending;
        client.enqueueStep(readStep(0, 1, pending, {}, CommunicationOwner::Monitor));
        const auto active = client.readHoldingRegisters(
            PduAddress(0), 1, options(CommunicationOwner::Monitor));
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        const auto queued = client.readHoldingRegisters(
            PduAddress(1), 1, options(CommunicationOwner::Monitor));
        QVERIFY(active.accepted());
        QVERIFY(queued.accepted());

        const auto handoff = client.acquireOwnership(
            CommunicationOwner::Testing, HandoffMode::FinishInFlight);
        QVERIFY(handoff.accepted());
        QCOMPARE(completed.count(), 1);
        QCOMPARE(resultAt(completed, 0).requestId, *queued.requestId);
        QCOMPARE(resultAt(completed, 0).error->code, ErrorCode::CancelledByHandoff);
        QCOMPARE(client.activeOwner(), CommunicationOwner::Monitor);
        const auto gated = client.readHoldingRegisters(
            PduAddress(2), 1, options(CommunicationOwner::Monitor));
        QVERIFY(!gated.accepted());
        QCOMPARE(gated.rejection->code, ErrorCode::NotAcceptingRequests);

        QVERIFY(client.cancelRequest(*active.requestId).isAccepted);
        scheduler->runUntilIdle();
        QCOMPARE(client.activeOwner(), CommunicationOwner::Testing);
        QCOMPARE(ownership.count(), 1);
        QCOMPARE(completed.count(), 2);

        client.enqueueStep(readStep(2, 1, pending, {}, CommunicationOwner::Testing));
        const auto testingRequest = client.readHoldingRegisters(
            PduAddress(2), 1, options(CommunicationOwner::Testing));
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QVERIFY(client.acquireOwnership(CommunicationOwner::ManualDebug,
                                        HandoffMode::CancelInFlight).accepted());
        scheduler->runUntilIdle();
        QCOMPARE(resultAt(completed, 2).requestId, *testingRequest.requestId);
        QCOMPARE(resultAt(completed, 2).error->code, ErrorCode::CancelledByHandoff);
        QCOMPARE(client.activeOwner(), CommunicationOwner::ManualDebug);
        QCOMPARE(ownership.count(), 2);
        QVERIFY(client.releaseOwnership(CommunicationOwner::ManualDebug,
                                        HandoffMode::FinishInFlight).accepted());
        scheduler->runUntilIdle();
        QCOMPARE(client.activeOwner(), CommunicationOwner::None);
        QCOMPARE(ownership.count(), 3);
        QVERIFY(client.verificationError().isEmpty());
    }

    void closeCancelsAllAndRejectsFurtherRequests()
    {
        auto scheduler = std::make_shared<ManualScheduler>();
        FakeModbusClient client(scheduler);
        QSignalSpy completed(&client, &IModbusClient::requestCompleted);
        connectAndAcquire(client, scheduler);

        FakeOutcome pending;
        pending.kind = FakeOutcomeKind::Pending;
        client.enqueueStep(readStep(0, 1, pending));
        const auto request = client.readHoldingRegisters(PduAddress(0), 1, options());
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QVERIFY(request.accepted());
        QVERIFY(client.close().accepted());
        QCOMPARE(completed.count(), 1);
        QCOMPARE(resultAt(completed, 0).error->code, ErrorCode::CancelledByClose);
        scheduler->runUntilIdle();
        QCOMPARE(client.connectionState(), ConnectionState::Disconnected);
        QCOMPARE(client.activeOwner(), CommunicationOwner::None);
        const auto rejected = client.writeSingleRegister(PduAddress(9), 500, options());
        QVERIFY(!rejected.accepted());
        QCOMPARE(rejected.rejection->code, ErrorCode::NotConnected);
        QVERIFY(client.verificationError().isEmpty());
    }

    void finishInFlightCloseWaitsForTerminalResult()
    {
        auto scheduler = std::make_shared<ManualScheduler>();
        FakeModbusClient client(scheduler);
        QSignalSpy completed(&client, &IModbusClient::requestCompleted);
        connectAndAcquire(client, scheduler);

        FakeOutcome success;
        success.kind = FakeOutcomeKind::ReadSuccess;
        success.readValues = {42};
        client.enqueueStep(readStep(0, 1, success, std::chrono::milliseconds(10)));
        const auto active = client.readHoldingRegisters(PduAddress(0), 1, options());
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        const auto queued = client.readHoldingRegisters(PduAddress(1), 1, options());
        QVERIFY(active.accepted());
        QVERIFY(queued.accepted());
        QVERIFY(client.close(CloseMode::FinishInFlight).accepted());
        QCOMPARE(completed.count(), 1);
        QCOMPARE(resultAt(completed, 0).requestId, *queued.requestId);
        QCOMPARE(resultAt(completed, 0).state, RequestState::Cancelled);
        QCOMPARE(client.connectionState(), ConnectionState::Closing);

        scheduler->runUntilIdle();
        QCOMPARE(completed.count(), 2);
        QCOMPARE(resultAt(completed, 1).requestId, *active.requestId);
        QCOMPARE(resultAt(completed, 1).state, RequestState::Succeeded);
        QCOMPARE(client.connectionState(), ConnectionState::Disconnected);
        QVERIFY(client.verificationError().isEmpty());
    }
};

QTEST_APPLESS_MAIN(FakeModbusClientTest)

#include "tst_fakemodbusclient.moc"
