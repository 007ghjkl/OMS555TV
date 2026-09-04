#include "app/AppStateController.h"
#include "communication/FakeModbusClient.h"
#include "configuration/ConfigurationService.h"
#include "device/RegisterMap.h"
#include "monitor/MonitorService.h"
#include "../monitor/ManualMonitorScheduler.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace oms555tv;

namespace {

communication::ModbusConnectionConfig connectionConfig()
{
    communication::ModbusConnectionConfig config;
    config.serial.portName = QStringLiteral("FAKE");
    return config;
}

communication::FakeStep readStep(device::PduAddress address,
                                 quint16 count,
                                 communication::FakeOutcome outcome)
{
    return {{communication::CommunicationOwner::ManualDebug,
             communication::ReadRequestDescriptor{address, count},
             std::chrono::milliseconds(500)},
            std::move(outcome), {}, {}, {}};
}

communication::FakeStep writeStep(device::PduAddress address,
                                  quint16 raw,
                                  communication::FakeOutcome outcome)
{
    return {{communication::CommunicationOwner::ManualDebug,
             communication::WriteRequestDescriptor{address, raw},
             std::chrono::milliseconds(500)},
            std::move(outcome), {}, {}, {}};
}

communication::FakeOutcome readSuccess(QVector<quint16> values)
{
    communication::FakeOutcome outcome;
    outcome.kind = communication::FakeOutcomeKind::ReadSuccess;
    outcome.readValues = std::move(values);
    return outcome;
}

communication::FakeOutcome writeSuccess()
{
    communication::FakeOutcome outcome;
    outcome.kind = communication::FakeOutcomeKind::WriteSuccess;
    return outcome;
}

struct Fixture {
    std::shared_ptr<communication::ManualScheduler> scheduler
        = std::make_shared<communication::ManualScheduler>();
    communication::FakeModbusClient client{scheduler};
    monitor::test::ManualMonitorScheduler monitorScheduler{scheduler};
    monitor::MonitorService monitorService{client, monitorScheduler};
    app::AppStateController controller{client, monitorService};
    configuration::ConfigurationService service{controller, client};

    void connectIdle()
    {
        QVERIFY(controller.connectDevice(connectionConfig()).accepted());
        scheduler->runUntilIdle();
        QCOMPARE(controller.state(), app::AppState::ConnectedIdle);
    }
};

void enqueueInitialRead(Fixture &fixture, QVector<quint16> values = {600, 610, 620, 400})
{
    fixture.client.enqueueStep(readStep(device::thresholdsReadBlock.startAddress,
                                        device::thresholdsReadBlock.count,
                                        readSuccess(std::move(values))));
}

void enqueueSuccessfulItem(Fixture &fixture, int index, quint16 raw)
{
    const device::PduAddress address(
        static_cast<quint16>(device::thresholdsReadBlock.startAddress.value() + index));
    fixture.client.enqueueStep(writeStep(address, raw, writeSuccess()));
    fixture.client.enqueueStep(readStep(address, 1, readSuccess({raw})));
}

} // namespace

class ConfigurationTest final : public QObject
{
    Q_OBJECT

private slots:
    void readsFourCurrentThresholdsAndReleasesOwner()
    {
        Fixture fixture;
        fixture.connectIdle();
        enqueueInitialRead(fixture, {600, 610, 620, 400});
        QVERIFY(fixture.service.readThresholds().accepted());
        fixture.scheduler->runUntilIdle();
        QVERIFY(fixture.service.lastResult()->succeeded);
        QCOMPARE(fixture.service.thresholds()->phaseB.deciCelsius, qint16(610));
        QCOMPARE(fixture.client.activeOwner(), communication::CommunicationOwner::None);
    }

    void readsAndWritesFourChannelsWithIndependentReadback()
    {
        Fixture fixture;
        fixture.connectIdle();
        enqueueInitialRead(fixture);
        const std::array<quint16, 4> raw{650, 660, 670, 450};
        for (int index = 0; index < 4; ++index) {
            enqueueSuccessfulItem(fixture, index, raw[index]);
        }
        QSignalSpy completed(&fixture.service,
                             &configuration::ConfigurationService::operationCompleted);
        QVERIFY(fixture.service.writeThresholds({65.0, 66.0, 67.0, 45.0}).accepted());
        fixture.scheduler->runUntilIdle();

        QCOMPARE(completed.count(), 1);
        const auto result = qvariant_cast<configuration::ConfigurationOperationResult>(
            completed.at(0).at(0));
        QVERIFY(result.succeeded);
        QCOMPARE(result.items.size(), 4);
        for (const auto &item : result.items) {
            QCOMPARE(item.status, configuration::ConfigurationItemStatus::Succeeded);
            QVERIFY(item.writeRequestId.has_value());
            QVERIFY(item.readbackRequestId.has_value());
            QVERIFY(item.writeRequestId != item.readbackRequestId);
        }
        QCOMPARE(fixture.service.thresholds()->phaseA.deciCelsius, qint16(650));
        QCOMPARE(fixture.client.activeOwner(), communication::CommunicationOwner::None);
        QCOMPARE(fixture.controller.state(), app::AppState::ConnectedIdle);
        QVERIFY(fixture.client.scriptConsumed());
    }

    void invalidInputIsRejectedBeforeOwnerOrRequestId()
    {
        Fixture fixture;
        fixture.connectIdle();
        QSignalSpy requests(&fixture.client, &communication::IModbusClient::requestCompleted);
        const auto submission = fixture.service.writeThresholds({20.05, 60.0, 60.0, 40.0});
        QVERIFY(!submission.accepted());
        QCOMPARE(submission.rejection->code,
                 configuration::ConfigurationErrorCode::InvalidInput);
        QCOMPARE(requests.count(), 0);
        QCOMPARE(fixture.client.activeOwner(), communication::CommunicationOwner::None);
    }

    void partialFailureContinuesAndDoesNotPublishUnverifiedValue()
    {
        Fixture fixture;
        fixture.connectIdle();
        enqueueInitialRead(fixture);
        communication::FakeOutcome exception;
        exception.kind = communication::FakeOutcomeKind::RemoteException;
        exception.exceptionCode = 0x03;
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 650, exception));
        enqueueSuccessfulItem(fixture, 1, 660);
        fixture.client.enqueueStep(writeStep(device::PduAddress(11), 670, writeSuccess()));
        fixture.client.enqueueStep(readStep(device::PduAddress(11), 1,
                                            readSuccess({671})));
        enqueueSuccessfulItem(fixture, 3, 450);

        QVERIFY(fixture.service.writeThresholds({65.0, 66.0, 67.0, 45.0}).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = *fixture.service.lastResult();
        QVERIFY(!result.succeeded);
        QCOMPARE(result.items[0].status,
                 configuration::ConfigurationItemStatus::WriteFailed);
        QCOMPARE(result.items[1].status,
                 configuration::ConfigurationItemStatus::Succeeded);
        QCOMPARE(result.items[2].status,
                 configuration::ConfigurationItemStatus::ReadbackMismatch);
        QCOMPARE(result.items[2].expected.deciCelsius, qint16(670));
        QCOMPARE(result.items[2].actual->deciCelsius, qint16(671));
        QCOMPARE(result.verifiedThresholds->phaseA.deciCelsius, qint16(600));
        QCOMPARE(result.verifiedThresholds->phaseB.deciCelsius, qint16(660));
        QCOMPARE(result.verifiedThresholds->phaseC.deciCelsius, qint16(620));
        QCOMPARE(fixture.client.activeOwner(), communication::CommunicationOwner::None);
        QVERIFY(fixture.client.scriptConsumed());
    }

    void cancellationWaitsForTerminalAndReleasesOwner()
    {
        Fixture fixture;
        fixture.connectIdle();
        enqueueInitialRead(fixture);
        communication::FakeOutcome pending;
        pending.kind = communication::FakeOutcomeKind::Pending;
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 650, pending));
        QVERIFY(fixture.service.writeThresholds({65.0, 66.0, 67.0, 45.0}).accepted());
        fixture.scheduler->runUntilIdle();
        QCOMPARE(fixture.service.state(), configuration::ConfigurationState::Writing);
        QVERIFY(fixture.client.hasNonTerminalRequests());
        QVERIFY(fixture.service.cancel());
        fixture.scheduler->runUntilIdle();
        QVERIFY(fixture.service.lastResult()->cancelled);
        QCOMPARE(fixture.client.activeOwner(), communication::CommunicationOwner::None);
        QVERIFY(!fixture.client.hasNonTerminalRequests());
    }

    void cancellationDuringOwnerAcquisitionCompletesCleanly()
    {
        Fixture fixture;
        fixture.connectIdle();
        QVERIFY(fixture.service.readThresholds().accepted());
        QCOMPARE(fixture.service.state(),
                 configuration::ConfigurationState::AcquiringOwner);
        QVERIFY(fixture.service.cancel());
        fixture.scheduler->runUntilIdle();
        QVERIFY(fixture.service.lastResult().has_value());
        QVERIFY(fixture.service.lastResult()->cancelled);
        QCOMPARE(fixture.service.state(), configuration::ConfigurationState::Idle);
        QCOMPARE(fixture.client.activeOwner(), communication::CommunicationOwner::None);
    }

    void readbackTimeoutIsClassifiedAndLaterItemsContinue()
    {
        Fixture fixture;
        fixture.connectIdle();
        enqueueInitialRead(fixture);
        fixture.client.enqueueStep(writeStep(device::PduAddress(9), 650, writeSuccess()));
        communication::FakeOutcome timeout;
        timeout.kind = communication::FakeOutcomeKind::Timeout;
        fixture.client.enqueueStep(readStep(device::PduAddress(9), 1, timeout));
        for (int index = 1; index < 4; ++index) {
            enqueueSuccessfulItem(fixture, index,
                                  static_cast<quint16>(650 + index * 10));
        }
        QVERIFY(fixture.service.writeThresholds({65.0, 66.0, 67.0, 68.0}).accepted());
        fixture.scheduler->runUntilIdle();
        const auto &result = *fixture.service.lastResult();
        QCOMPARE(result.items[0].status,
                 configuration::ConfigurationItemStatus::ReadbackFailed);
        QCOMPARE(result.items[0].error->communicationError->code,
                 communication::ErrorCode::ResponseTimeout);
        QCOMPARE(result.items[3].status,
                 configuration::ConfigurationItemStatus::Succeeded);
        QCOMPARE(fixture.client.activeOwner(), communication::CommunicationOwner::None);
    }

    void monitoringStateRejectsConfiguration()
    {
        Fixture fixture;
        fixture.connectIdle();
        monitor::MonitorConfig config;
        config.targetPeriod = std::chrono::milliseconds(1000);
        QVERIFY(fixture.controller.startMonitoring(config).accepted());
        fixture.scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(fixture.controller.state(), app::AppState::Monitoring);
        const auto submission = fixture.service.readThresholds();
        QVERIFY(!submission.accepted());
        QCOMPARE(submission.rejection->code,
                 configuration::ConfigurationErrorCode::InvalidState);
    }
};

QTEST_APPLESS_MAIN(ConfigurationTest)

#include "tst_configuration.moc"
