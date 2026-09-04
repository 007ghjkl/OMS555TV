#include "app/AppStateController.h"
#include "communication/FakeModbusClient.h"
#include "monitor/MonitorService.h"
#include "ManualMonitorScheduler.h"

#include <QSignalSpy>
#include <QTest>

#include <array>
#include <limits>
#include <memory>

using namespace oms555tv;

namespace {

communication::ModbusConnectionConfig connectionConfig()
{
    communication::ModbusConnectionConfig config;
    config.serial.portName = QStringLiteral("FAKE");
    return config;
}

QVector<quint16> validValues(qsizetype blockIndex)
{
    switch (blockIndex) {
    case 0:
        return {250, 260, 270, 230, 1200};
    case 1:
        return {600, 610, 620, 400};
    case 2:
        return {0x0001, 0x0021};
    case 3:
        return {2, 1234, 0};
    case 4:
        return {1, 2};
    default:
        return {};
    }
}

communication::FakeStep stepForBlock(
    qsizetype blockIndex,
    communication::FakeOutcome outcome,
    std::chrono::milliseconds delay = std::chrono::milliseconds::zero(),
    QString correlationId = {})
{
    const auto block = device::deviceSnapshotReadBlocks[static_cast<std::size_t>(blockIndex)];
    return {{communication::CommunicationOwner::Monitor,
             communication::ReadRequestDescriptor{block.startAddress, block.count},
             std::chrono::milliseconds(500),
             std::move(correlationId)},
            std::move(outcome), delay, {}, {}};
}

void enqueueSuccessfulBatch(communication::FakeModbusClient &client,
                            std::chrono::milliseconds delay = std::chrono::milliseconds::zero(),
                            std::optional<QVector<quint16>> firstBlockOverride = std::nullopt,
                            QString correlationId = {})
{
    for (qsizetype index = 0;
         index < static_cast<qsizetype>(device::deviceSnapshotReadBlocks.size());
         ++index) {
        communication::FakeOutcome outcome;
        outcome.kind = communication::FakeOutcomeKind::ReadSuccess;
        outcome.readValues = index == 0 && firstBlockOverride
            ? *firstBlockOverride
            : validValues(index);
        client.enqueueStep(stepForBlock(index, std::move(outcome), delay,
                                        correlationId));
    }
}

void connectAndAcquireMonitor(
    communication::FakeModbusClient &client,
    const std::shared_ptr<communication::ManualScheduler> &scheduler)
{
    QVERIFY(client.open(connectionConfig()).accepted());
    scheduler->advanceBy(std::chrono::nanoseconds::zero());
    QCOMPARE(client.connectionState(), communication::ConnectionState::Connected);
    QVERIFY(client.acquireOwnership(communication::CommunicationOwner::Monitor,
                                    communication::HandoffMode::FinishInFlight).accepted());
    scheduler->advanceBy(std::chrono::nanoseconds::zero());
    QCOMPARE(client.activeOwner(), communication::CommunicationOwner::Monitor);
}

monitor::PollBatchResult batchAt(const QSignalSpy &spy, qsizetype index)
{
    return qvariant_cast<monitor::PollBatchResult>(spy.at(index).at(0));
}

app::AppCommandResult commandAt(const QSignalSpy &spy, qsizetype index)
{
    return qvariant_cast<app::AppCommandResult>(spy.at(index).at(0));
}

monitor::MonitorConfig monitorConfig(std::chrono::milliseconds period =
                                         std::chrono::milliseconds(100))
{
    monitor::MonitorConfig config;
    config.targetPeriod = period;
    config.requestTimeout = std::chrono::milliseconds(500);
    return config;
}

} // namespace

class MonitoringCoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void completeSnapshotIsStrictAndStatisticsAreAuditable()
    {
        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler);
        connectAndAcquireMonitor(client, scheduler);
        monitor::test::ManualMonitorScheduler monitorScheduler(scheduler);
        monitor::MonitorService service(client, monitorScheduler);
        QSignalSpy snapshots(&service, &monitor::MonitorService::snapshotPublished);
        QSignalSpy batches(&service, &monitor::MonitorService::batchCompleted);

        enqueueSuccessfulBatch(client, std::chrono::milliseconds(5), std::nullopt,
                               QStringLiteral("monitor/1"));
        QVERIFY(service.start(monitorConfig()).accepted);
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(service.statistics().requests, quint64(1));
        for (int index = 0; index < 5; ++index) {
            scheduler->advanceBy(std::chrono::milliseconds(5));
        }

        QCOMPARE(snapshots.count(), 1);
        QCOMPARE(batches.count(), 1);
        const auto batch = batchAt(batches, 0);
        QVERIFY(batch.complete);
        QVERIFY(!batch.stopped);
        QCOMPARE(batch.requestIds.size(), 5);
        QCOMPARE(batch.requestResults.size(), 5);
        QCOMPARE(batch.duration, std::chrono::milliseconds(25));
        for (qsizetype index = 0; index < batch.requestResults.size(); ++index) {
            const auto &descriptor = std::get<communication::ReadRequestDescriptor>(
                batch.requestResults[index].descriptor);
            QCOMPARE(descriptor.startAddress,
                     device::deviceSnapshotReadBlocks[static_cast<std::size_t>(index)].startAddress);
            QCOMPARE(descriptor.count,
                     device::deviceSnapshotReadBlocks[static_cast<std::size_t>(index)].count);
        }

        const auto snapshot = qvariant_cast<monitor::MonitoringSnapshot>(
            snapshots.at(0).at(0));
        QCOMPARE(snapshot.batchId, monitor::PollBatchId{1});
        QCOMPARE(snapshot.requestIds.size(), 5);
        QCOMPARE(snapshot.snapshot.measurements.phaseATemperature.deciCelsius, qint16(250));
        QCOMPARE(snapshot.snapshot.measurements.lightMillivolts, quint16(1200));
        QCOMPARE(snapshot.snapshot.thresholds.ambient.deciCelsius, qint16(400));
        QVERIFY(snapshot.snapshot.status.device.running());
        QCOMPARE(snapshot.snapshot.diagnostics.uptimeSeconds, quint32(1234));
        QCOMPARE(snapshot.snapshot.firmwareVersion.major, quint16(1));
        QCOMPARE(snapshot.lastSuccessfulUtc, snapshot.completedUtc);

        const auto &stats = service.statistics();
        QCOMPARE(stats.requests, quint64(5));
        QCOMPARE(stats.succeeded, quint64(5));
        QCOMPARE(stats.failed, quint64(0));
        QCOMPARE(stats.rttSamples, quint64(5));
        QCOMPARE(stats.totalRttNanoseconds,
                 quint64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::milliseconds(25)).count()));
        QCOMPARE(stats.minimumRtt, std::chrono::milliseconds(5));
        QCOMPARE(stats.maximumRtt, std::chrono::milliseconds(5));
        QCOMPARE(stats.batchesSucceeded, quint64(1));
        QCOMPARE(service.health(), monitor::DeviceHealth::Online);
        QVERIFY(service.lastSnapshot().has_value());
        QVERIFY(client.verificationError().isEmpty());

        QVERIFY(service.stop().accepted);
        QCOMPARE(service.health(), monitor::DeviceHealth::Unknown);
        scheduler->advanceBy(std::chrono::seconds(1));
        QCOMPARE(service.statistics().requests, quint64(5));
    }

    void everyBlockFailureStopsTheBatchWithoutMixedSnapshot()
    {
        for (qsizetype failedIndex = 0; failedIndex < 5; ++failedIndex) {
            auto scheduler = std::make_shared<communication::ManualScheduler>();
            communication::FakeModbusClient client(scheduler);
            connectAndAcquireMonitor(client, scheduler);
            monitor::test::ManualMonitorScheduler monitorScheduler(scheduler);
            monitor::MonitorService service(client, monitorScheduler);
            QSignalSpy snapshots(&service, &monitor::MonitorService::snapshotPublished);
            QSignalSpy batches(&service, &monitor::MonitorService::batchCompleted);

            for (qsizetype index = 0; index < failedIndex; ++index) {
                communication::FakeOutcome success;
                success.kind = communication::FakeOutcomeKind::ReadSuccess;
                success.readValues = validValues(index);
                client.enqueueStep(stepForBlock(index, std::move(success)));
            }
            communication::FakeOutcome failure;
            switch (failedIndex) {
            case 0:
                failure.kind = communication::FakeOutcomeKind::Timeout;
                break;
            case 1:
                failure.kind = communication::FakeOutcomeKind::RemoteException;
                failure.exceptionCode = 0x02;
                break;
            case 2:
                failure.kind = communication::FakeOutcomeKind::CrcMismatch;
                break;
            case 3:
                failure.kind = communication::FakeOutcomeKind::ProtocolError;
                break;
            default:
                failure.kind = communication::FakeOutcomeKind::SerialError;
                failure.serialError = communication::ErrorCode::ResourceError;
                break;
            }
            client.enqueueStep(stepForBlock(failedIndex, std::move(failure),
                                            std::chrono::milliseconds(10)));

            QVERIFY(service.start(monitorConfig()).accepted);
            scheduler->advanceBy(std::chrono::nanoseconds::zero());
            scheduler->advanceBy(std::chrono::milliseconds(10));
            QCOMPARE(batches.count(), 1);
            QCOMPARE(snapshots.count(), 0);
            const auto batch = batchAt(batches, 0);
            QVERIFY(!batch.complete);
            QVERIFY(batch.error.has_value());
            QCOMPARE(batch.error->code, monitor::MonitorErrorCode::RequestFailed);
            QCOMPARE(batch.requestIds.size(), failedIndex + 1);
            QCOMPARE(service.statistics().requests,
                     static_cast<quint64>(failedIndex + 1));
            QCOMPARE(service.statistics().failed, quint64(1));
            QCOMPARE(service.statistics().timedOut,
                     failedIndex == 0 ? quint64(1) : quint64(0));
            QCOMPARE(service.health(), monitor::DeviceHealth::Degraded);
            QVERIFY(!service.lastSnapshot().has_value());
            QVERIFY(client.verificationError().isEmpty());
            QVERIFY(service.stop().accepted);
        }
    }

    void failuresRetainLastSnapshotAndRecoverFromOffline()
    {
        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler);
        connectAndAcquireMonitor(client, scheduler);
        monitor::test::ManualMonitorScheduler monitorScheduler(scheduler);
        monitor::MonitorService service(client, monitorScheduler);
        QSignalSpy snapshots(&service, &monitor::MonitorService::snapshotPublished);
        QSignalSpy batches(&service, &monitor::MonitorService::batchCompleted);

        enqueueSuccessfulBatch(client);
        for (int index = 0; index < 3; ++index) {
            communication::FakeOutcome remote;
            remote.kind = communication::FakeOutcomeKind::RemoteException;
            remote.exceptionCode = 0x03;
            client.enqueueStep(stepForBlock(0, std::move(remote)));
        }
        enqueueSuccessfulBatch(client);

        QVERIFY(service.start(monitorConfig()).accepted);
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(snapshots.count(), 1);
        const auto firstSnapshot = *service.lastSnapshot();

        scheduler->advanceBy(std::chrono::milliseconds(100));
        QCOMPARE(service.health(), monitor::DeviceHealth::Degraded);
        QCOMPARE(service.lastSnapshot()->batchId, firstSnapshot.batchId);
        QCOMPARE(service.lastSnapshot()->completedUtc, firstSnapshot.completedUtc);
        scheduler->advanceBy(std::chrono::milliseconds(100));
        QCOMPARE(service.health(), monitor::DeviceHealth::Degraded);
        scheduler->advanceBy(std::chrono::milliseconds(100));
        QCOMPARE(service.health(), monitor::DeviceHealth::Offline);
        QCOMPARE(snapshots.count(), 1);
        QCOMPARE(service.lastSnapshot()->completedUtc, firstSnapshot.completedUtc);

        scheduler->advanceBy(std::chrono::milliseconds(100));
        QCOMPARE(service.health(), monitor::DeviceHealth::Online);
        QCOMPARE(snapshots.count(), 2);
        QCOMPARE(service.lastSnapshot()->batchId, monitor::PollBatchId{5});
        QVERIFY(service.lastSnapshot()->completedUtc > firstSnapshot.completedUtc);
        QCOMPARE(batches.count(), 5);
        QCOMPARE(service.statistics().requests, quint64(13));
        QCOMPARE(service.statistics().succeeded, quint64(10));
        QCOMPARE(service.statistics().failed, quint64(3));
        QCOMPARE(service.statistics().batchesSucceeded, quint64(2));
        QCOMPARE(service.statistics().batchesFailed, quint64(3));
        QCOMPARE(service.statistics().consecutiveBatchFailures, quint64(0));
        QVERIFY(client.verificationError().isEmpty());
        QVERIFY(service.stop().accepted);
    }

    void codecFailureDoesNotPublishAndNextBatchCanRecover()
    {
        auto scheduler = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(scheduler);
        connectAndAcquireMonitor(client, scheduler);
        monitor::test::ManualMonitorScheduler monitorScheduler(scheduler);
        monitor::MonitorService service(client, monitorScheduler);
        QSignalSpy snapshots(&service, &monitor::MonitorService::snapshotPublished);
        QSignalSpy batches(&service, &monitor::MonitorService::batchCompleted);

        enqueueSuccessfulBatch(client, std::chrono::milliseconds::zero(),
                               QVector<quint16>{250, 260, 270, 230, 4000});
        enqueueSuccessfulBatch(client);
        QVERIFY(service.start(monitorConfig()).accepted);
        scheduler->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(batches.count(), 1);
        QCOMPARE(batchAt(batches, 0).error->code,
                 monitor::MonitorErrorCode::DecodeFailed);
        QCOMPARE(snapshots.count(), 0);
        QCOMPARE(service.statistics().succeeded, quint64(5));
        QCOMPARE(service.statistics().batchesFailed, quint64(1));

        scheduler->advanceBy(std::chrono::milliseconds(100));
        QCOMPARE(batches.count(), 2);
        QCOMPARE(snapshots.count(), 1);
        QCOMPARE(service.health(), monitor::DeviceHealth::Online);
        QVERIFY(client.verificationError().isEmpty());
        QVERIFY(service.stop().accepted);
    }

    void overrunUsesEffectiveIntervalWithoutOverlapOrBacklog()
    {
        auto clientClock = std::make_shared<communication::ManualScheduler>();
        auto monitorClock = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(clientClock);
        connectAndAcquireMonitor(client, clientClock);
        monitor::test::ManualMonitorScheduler monitorScheduler(monitorClock);
        monitor::MonitorService service(client, monitorScheduler);
        QSignalSpy batches(&service, &monitor::MonitorService::batchCompleted);

        enqueueSuccessfulBatch(client, std::chrono::milliseconds(30));
        communication::FakeOutcome pending;
        pending.kind = communication::FakeOutcomeKind::Pending;
        client.enqueueStep(stepForBlock(0, std::move(pending)));

        QVERIFY(service.start(monitorConfig()).accepted);
        monitorClock->advanceBy(std::chrono::nanoseconds::zero());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        for (int index = 0; index < 5; ++index) {
            monitorClock->advanceBy(std::chrono::milliseconds(30));
            clientClock->advanceBy(std::chrono::milliseconds(30));
        }
        QCOMPARE(batches.count(), 1);
        const auto first = batchAt(batches, 0);
        QVERIFY(first.complete);
        QVERIFY(first.overrun);
        QCOMPARE(first.duration, std::chrono::milliseconds(150));
        QCOMPARE(service.statistics().overruns, quint64(1));
        QCOMPARE(service.statistics().requests, quint64(5));

        monitorClock->advanceBy(std::chrono::nanoseconds::zero());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(service.statistics().batchesStarted, quint64(2));
        QCOMPARE(service.statistics().requests, quint64(6));
        QCOMPARE(service.statistics().recentEffectiveInterval,
                 std::chrono::milliseconds(150));
        QVERIFY(service.stop().accepted);
        QCOMPARE(batches.count(), 2);
        QVERIFY(batchAt(batches, 1).stopped);
        QCOMPARE(service.statistics().cancelled, quint64(1));
        QCOMPARE(service.statistics().failed, quint64(0));
        monitorClock->advanceBy(std::chrono::seconds(1));
        clientClock->advanceBy(std::chrono::seconds(1));
        QCOMPARE(service.statistics().requests, quint64(6));
        QVERIFY(client.verificationError().isEmpty());
    }

    void stopHandlesNoRequestQueuedAndInFlightWithoutLatePolls()
    {
        // 尚未触发首批：停止只取消定时任务。
        {
            auto clock = std::make_shared<communication::ManualScheduler>();
            communication::FakeModbusClient client(clock);
            connectAndAcquireMonitor(client, clock);
            monitor::test::ManualMonitorScheduler scheduler(clock);
            monitor::MonitorService service(client, scheduler);
            QSignalSpy stopped(&service, &monitor::MonitorService::stopped);
            QVERIFY(service.start(monitorConfig()).accepted);
            QVERIFY(service.stop().accepted);
            QCOMPARE(stopped.count(), 1);
            clock->advanceBy(std::chrono::seconds(1));
            QCOMPARE(service.statistics().requests, quint64(0));
        }

        // 请求仍在 Fake 队列：取消 queued 请求，不能消费脚本或留下迟到轮询。
        {
            auto clientClock = std::make_shared<communication::ManualScheduler>();
            auto monitorClock = std::make_shared<communication::ManualScheduler>();
            communication::FakeModbusClient client(clientClock);
            connectAndAcquireMonitor(client, clientClock);
            monitor::test::ManualMonitorScheduler scheduler(monitorClock);
            monitor::MonitorService service(client, scheduler);
            QSignalSpy batches(&service, &monitor::MonitorService::batchCompleted);
            QVERIFY(service.start(monitorConfig()).accepted);
            monitorClock->advanceBy(std::chrono::nanoseconds::zero());
            QCOMPARE(service.statistics().requests, quint64(1));
            QVERIFY(service.stop().accepted);
            QCOMPARE(batches.count(), 1);
            QVERIFY(batchAt(batches, 0).stopped);
            QCOMPARE(service.statistics().cancelled, quint64(1));
            QCOMPARE(service.statistics().failed, quint64(0));
            clientClock->advanceBy(std::chrono::seconds(1));
            monitorClock->advanceBy(std::chrono::seconds(1));
            QCOMPARE(service.statistics().requests, quint64(1));
            QVERIFY(client.verificationError().isEmpty());
        }

        // 请求已经 in-flight：受控取消并等待其唯一终态。
        {
            auto clientClock = std::make_shared<communication::ManualScheduler>();
            auto monitorClock = std::make_shared<communication::ManualScheduler>();
            communication::FakeModbusClient client(clientClock);
            connectAndAcquireMonitor(client, clientClock);
            monitor::test::ManualMonitorScheduler scheduler(monitorClock);
            monitor::MonitorService service(client, scheduler);
            communication::FakeOutcome pending;
            pending.kind = communication::FakeOutcomeKind::Pending;
            client.enqueueStep(stepForBlock(0, std::move(pending)));
            QVERIFY(service.start(monitorConfig()).accepted);
            monitorClock->advanceBy(std::chrono::nanoseconds::zero());
            clientClock->advanceBy(std::chrono::nanoseconds::zero());
            QVERIFY(client.hasNonTerminalRequests());
            QVERIFY(service.stop().accepted);
            QVERIFY(!client.hasNonTerminalRequests());
            QCOMPARE(service.statistics().cancelled, quint64(1));
            QCOMPARE(service.health(), monitor::DeviceHealth::Unknown);
            clientClock->advanceBy(std::chrono::seconds(1));
            monitorClock->advanceBy(std::chrono::seconds(1));
            QCOMPARE(service.statistics().requests, quint64(1));
            QVERIFY(client.verificationError().isEmpty());
        }
    }

    void appStateCoordinatesConnectionModesDisconnectAndRecovery()
    {
        auto clientClock = std::make_shared<communication::ManualScheduler>();
        auto monitorClock = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(clientClock);
        monitor::test::ManualMonitorScheduler monitorScheduler(monitorClock);
        monitor::MonitorService service(client, monitorScheduler);
        app::AppStateController controller(client, service);
        QSignalSpy commands(&controller, &app::AppStateController::commandCompleted);

        const auto open = controller.connectDevice(connectionConfig());
        QVERIFY(open.accepted());
        const auto duplicateOpen = controller.connectDevice(connectionConfig());
        QVERIFY(!duplicateOpen.accepted());
        QCOMPARE(duplicateOpen.rejection->code, app::AppErrorCode::CommandInProgress);
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::ConnectedIdle);
        QCOMPARE(commandAt(commands, 0).command, app::AppCommand::Connect);

        QVERIFY(controller.startMonitoring(monitorConfig()).accepted());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::Monitoring);
        QCOMPARE(client.activeOwner(), communication::CommunicationOwner::Monitor);
        const auto testingConflict = controller.startTesting();
        QVERIFY(!testingConflict.accepted());
        QCOMPARE(testingConflict.rejection->code, app::AppErrorCode::InvalidState);

        QVERIFY(controller.stopMonitoring().accepted());
        QCOMPARE(controller.state(), app::AppState::Stopping);
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::ConnectedIdle);
        QCOMPARE(client.activeOwner(), communication::CommunicationOwner::None);

        QVERIFY(controller.startTesting().accepted());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::Testing);
        QCOMPARE(client.activeOwner(), communication::CommunicationOwner::Testing);
        QVERIFY(!controller.startMonitoring(monitorConfig()).accepted());
        QVERIFY(controller.stopTesting().accepted());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::ConnectedIdle);

        QVERIFY(controller.disconnectDevice().accepted());
        QCOMPARE(controller.state(), app::AppState::Stopping);
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::Disconnected);
        QCOMPARE(client.connectionState(), communication::ConnectionState::Disconnected);

        communication::CommunicationError openFailure;
        openFailure.category = communication::ErrorCategory::Connection;
        openFailure.code = communication::ErrorCode::OpenFailed;
        client.failNextOpen(openFailure);
        QVERIFY(controller.connectDevice(connectionConfig()).accepted());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::Error);
        QVERIFY(!commandAt(commands, commands.count() - 1).succeeded);
        const auto recover = controller.recover();
        QVERIFY(recover.accepted());
        QCOMPARE(controller.state(), app::AppState::Disconnected);
        QCOMPARE(commandAt(commands, commands.count() - 1).command,
                 app::AppCommand::Recover);

        // 非 Controller 发起的后端断开必须成为不可恢复事件，不能伪装成正常关闭。
        QVERIFY(controller.connectDevice(connectionConfig()).accepted());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QVERIFY(controller.startMonitoring(monitorConfig()).accepted());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::Monitoring);
        QVERIFY(client.close().accepted());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::Error);
        QCOMPARE(service.health(), monitor::DeviceHealth::Offline);
        QVERIFY(controller.recover().accepted());
        QCOMPARE(controller.state(), app::AppState::Disconnected);
    }

    void disconnectDuringInFlightMonitorCancelsReleasesAndClosesExactlyOnce()
    {
        auto clientClock = std::make_shared<communication::ManualScheduler>();
        auto monitorClock = std::make_shared<communication::ManualScheduler>();
        communication::FakeModbusClient client(clientClock);
        monitor::test::ManualMonitorScheduler monitorScheduler(monitorClock);
        monitor::MonitorService service(client, monitorScheduler);
        app::AppStateController controller(client, service);
        QSignalSpy commands(&controller, &app::AppStateController::commandCompleted);

        QVERIFY(controller.connectDevice(connectionConfig()).accepted());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        communication::FakeOutcome pending;
        pending.kind = communication::FakeOutcomeKind::Pending;
        client.enqueueStep(stepForBlock(0, std::move(pending)));
        QVERIFY(controller.startMonitoring(monitorConfig()).accepted());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        monitorClock->advanceBy(std::chrono::nanoseconds::zero());
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QVERIFY(client.hasNonTerminalRequests());

        const auto disconnect = controller.disconnectDevice();
        QVERIFY(disconnect.accepted());
        QCOMPARE(controller.state(), app::AppState::Stopping);
        clientClock->advanceBy(std::chrono::nanoseconds::zero());
        QCOMPARE(controller.state(), app::AppState::Disconnected);
        QCOMPARE(client.activeOwner(), communication::CommunicationOwner::None);
        QCOMPARE(client.connectionState(), communication::ConnectionState::Disconnected);
        QVERIFY(!client.hasNonTerminalRequests());
        QCOMPARE(commandAt(commands, commands.count() - 1).operationId,
                 *disconnect.operationId);
        QVERIFY(commandAt(commands, commands.count() - 1).succeeded);
        QVERIFY(client.verificationError().isEmpty());
    }

    void controllerStopReleasesOwnerForQueuedAndInFlightRequests()
    {
        for (const bool startInFlight : {false, true}) {
            auto clientClock = std::make_shared<communication::ManualScheduler>();
            auto monitorClock = std::make_shared<communication::ManualScheduler>();
            communication::FakeModbusClient client(clientClock);
            monitor::test::ManualMonitorScheduler monitorScheduler(monitorClock);
            monitor::MonitorService service(client, monitorScheduler);
            app::AppStateController controller(client, service);

            QVERIFY(controller.connectDevice(connectionConfig()).accepted());
            clientClock->advanceBy(std::chrono::nanoseconds::zero());
            if (startInFlight) {
                communication::FakeOutcome pending;
                pending.kind = communication::FakeOutcomeKind::Pending;
                client.enqueueStep(stepForBlock(0, std::move(pending)));
            }
            QVERIFY(controller.startMonitoring(monitorConfig()).accepted());
            clientClock->advanceBy(std::chrono::nanoseconds::zero());
            monitorClock->advanceBy(std::chrono::nanoseconds::zero());
            if (startInFlight) {
                clientClock->advanceBy(std::chrono::nanoseconds::zero());
            }

            QVERIFY(controller.stopMonitoring().accepted());
            QCOMPARE(controller.state(), app::AppState::Stopping);
            clientClock->advanceBy(std::chrono::nanoseconds::zero());
            QCOMPARE(controller.state(), app::AppState::ConnectedIdle);
            QCOMPARE(client.activeOwner(), communication::CommunicationOwner::None);
            QVERIFY(!client.hasNonTerminalRequests());
            QCOMPARE(service.statistics().cancelled, quint64(1));
            QVERIFY(client.verificationError().isEmpty());
        }
    }

    void configAndCounterBoundariesAreDeterministic()
    {
        monitor::MonitorConfig config;
        config.targetPeriod = std::chrono::milliseconds(9);
        QCOMPARE(monitor::validateMonitorConfig(config)->code,
                 monitor::MonitorErrorCode::InvalidPeriod);
        config.targetPeriod = std::chrono::milliseconds(60001);
        QCOMPARE(monitor::validateMonitorConfig(config)->code,
                 monitor::MonitorErrorCode::InvalidPeriod);
        config.targetPeriod = std::chrono::milliseconds(10);
        config.requestTimeout = std::chrono::milliseconds(0);
        QCOMPARE(monitor::validateMonitorConfig(config)->code,
                 monitor::MonitorErrorCode::InvalidRequestTimeout);
        config.requestTimeout = std::chrono::milliseconds(60000);
        QVERIFY(!monitor::validateMonitorConfig(config).has_value());

        quint64 value = std::numeric_limits<quint64>::max();
        monitor::detail::saturatingIncrement(value);
        QCOMPARE(value, std::numeric_limits<quint64>::max());
        value = std::numeric_limits<quint64>::max() - 2;
        monitor::detail::saturatingAdd(value, 10);
        QCOMPARE(value, std::numeric_limits<quint64>::max());
    }
};

QTEST_APPLESS_MAIN(MonitoringCoreTest)

#include "tst_monitoringcore.moc"
