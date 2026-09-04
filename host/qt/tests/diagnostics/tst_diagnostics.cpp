#include "communication/FakeModbusClient.h"
#include "diagnostics/CommunicationDiagnostics.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace oms555tv;

namespace {

communication::ModbusConnectionConfig config()
{
    communication::ModbusConnectionConfig value;
    value.serial.portName = QStringLiteral("FAKE");
    return value;
}

communication::RequestOptions options()
{
    communication::RequestOptions value;
    value.owner = communication::CommunicationOwner::Testing;
    return value;
}

void connectTesting(communication::FakeModbusClient &client,
                    const std::shared_ptr<communication::ManualScheduler> &scheduler)
{
    QVERIFY(client.open(config()).accepted());
    scheduler->runUntilIdle();
    QVERIFY(client.acquireOwnership(communication::CommunicationOwner::Testing,
                                    communication::HandoffMode::FinishInFlight).accepted());
    scheduler->runUntilIdle();
}

communication::FakeStep readStep(quint16 address,
                                 communication::FakeOutcomeKind kind,
                                 QVector<quint16> values = {})
{
    communication::FakeOutcome outcome;
    outcome.kind = kind;
    outcome.readValues = std::move(values);
    if (kind == communication::FakeOutcomeKind::RemoteException) {
        outcome.exceptionCode = 0x02;
    }
    return {{communication::CommunicationOwner::Testing,
             communication::ReadRequestDescriptor{device::PduAddress(address), 1},
             std::chrono::milliseconds(500)}, outcome, {}, {}, {}};
}

} // namespace

class DiagnosticsTest final : public QObject
{
    Q_OBJECT

private slots:
    void boundedRecordsFilterAndClearPreserveEvidence()
    {
        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler);
        diagnostics::CommunicationDiagnosticsModel model(client, 3);
        QSignalSpy added(&model, &diagnostics::CommunicationDiagnosticsModel::recordAdded);
        connectTesting(client, scheduler);

        client.enqueueStep(readStep(0, communication::FakeOutcomeKind::ReadSuccess, {1}));
        client.enqueueStep(readStep(1, communication::FakeOutcomeKind::RemoteException));
        client.enqueueStep(readStep(2, communication::FakeOutcomeKind::Timeout));
        client.enqueueStep(readStep(3, communication::FakeOutcomeKind::CrcMismatch));
        for (quint16 address = 0; address < 4; ++address) {
            QVERIFY(client.readHoldingRegisters(device::PduAddress(address), 1,
                                                options()).accepted());
            scheduler->runUntilIdle();
        }
        QCOMPARE(added.count(), 4);
        QCOMPARE(model.records().size(), 3);
        QCOMPARE(model.records().front().result.requestId.value, quint64(2));
        QCOMPARE(model.records().back().level, logging::LogLevel::Error);
        QVERIFY(!model.records().back().result.evidence.txAdu.isEmpty());
        QCOMPARE(model.records().back().result.evidence.rxCrcStatus,
                 communication::CrcStatus::Invalid);

        diagnostics::DiagnosticFilter warnings;
        warnings.level = logging::LogLevel::Warning;
        QCOMPARE(model.filtered(warnings).size(), 2);
        diagnostics::DiagnosticFilter request;
        request.requestId = communication::RequestId{4};
        QCOMPARE(model.filtered(request).size(), 1);
        const QByteArray retainedTx = model.records().back().result.evidence.txAdu;
        model.clear();
        QVERIFY(model.records().isEmpty());
        QCOMPARE(retainedTx, qvariant_cast<diagnostics::DiagnosticRecord>(
            added.at(3).at(0)).result.evidence.txAdu);
    }

    void cancellationIsInfoAndHexFormattingIsStable()
    {
        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler);
        diagnostics::CommunicationDiagnosticsModel model(client);
        connectTesting(client, scheduler);
        communication::FakeOutcome pending;
        pending.kind = communication::FakeOutcomeKind::Pending;
        client.enqueueStep({{communication::CommunicationOwner::Testing,
                             communication::ReadRequestDescriptor{device::PduAddress(0), 1},
                             std::chrono::milliseconds(500)}, pending, {}, {}, {}});
        const auto submission = client.readHoldingRegisters(device::PduAddress(0), 1,
                                                            options());
        QVERIFY(submission.accepted());
        scheduler->runUntilIdle();
        QVERIFY(client.cancelRequest(*submission.requestId).isAccepted);
        QCOMPARE(model.records().size(), 1);
        QCOMPARE(model.records().front().level, logging::LogLevel::Info);
        QCOMPARE(diagnostics::byteArrayHex(QByteArray::fromHex("0103A0")),
                 QStringLiteral("01 03 A0"));
    }
};

QTEST_APPLESS_MAIN(DiagnosticsTest)

#include "tst_diagnostics.moc"
