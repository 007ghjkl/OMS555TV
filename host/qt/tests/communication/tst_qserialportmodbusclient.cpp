#include "IModbusClientContract.h"
#include "communication/QSerialPortModbusClient.h"
#include "communication/RtuCodec.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSerialPort>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <deque>
#include <memory>
#include <utility>

using namespace std::chrono_literals;
using namespace oms555tv;
using namespace oms555tv::communication;

namespace {

QByteArray readResponse(const QVector<quint16> &values)
{
    QByteArray bytes;
    bytes.append(char(1));
    bytes.append(char(readHoldingRegistersFunction));
    bytes.append(char(values.size() * 2));
    for (quint16 value : values) {
        bytes.append(char((value >> 8U) & 0xFFU));
        bytes.append(char(value & 0xFFU));
    }
    return appendModbusCrc(std::move(bytes));
}

struct TransportAction {
    QList<QByteArray> fragments;
    int fragmentDelayMs = 0;
    qint64 acceptedLength = -2; // -2 表示完整接受。
    std::optional<int> serialError;
};

struct TransportState {
    QMutex mutex;
    bool openSucceeds = true;
    bool configureSucceeds = true;
    bool opened = false;
    SerialPortConfig config;
    QThread *createdIn = nullptr;
    QList<QByteArray> writes;
    std::deque<TransportAction> actions;
};

class ScriptedTransport final : public SerialTransport
{
    Q_OBJECT

public:
    explicit ScriptedTransport(std::shared_ptr<TransportState> state)
        : state_(std::move(state))
    {
        QMutexLocker lock(&state_->mutex);
        state_->createdIn = QThread::currentThread();
    }

    bool configure(const SerialPortConfig &config) override
    {
        QMutexLocker lock(&state_->mutex);
        state_->config = config;
        return state_->configureSucceeds;
    }

    bool open() override
    {
        QMutexLocker lock(&state_->mutex);
        state_->opened = state_->openSucceeds;
        return state_->opened;
    }

    void close() override
    {
        QMutexLocker lock(&state_->mutex);
        state_->opened = false;
    }

    bool isOpen() const override
    {
        QMutexLocker lock(&state_->mutex);
        return state_->opened;
    }

    qint64 write(const QByteArray &bytes) override
    {
        TransportAction action;
        {
            QMutexLocker lock(&state_->mutex);
            state_->writes.append(bytes);
            if (state_->actions.empty()) {
                return bytes.size();
            }
            action = std::move(state_->actions.front());
            state_->actions.pop_front();
        }
        if (action.acceptedLength != -2) {
            return action.acceptedLength;
        }
        int delay = 0;
        for (const QByteArray &fragment : action.fragments) {
            delay += action.fragmentDelayMs;
            QTimer::singleShot(delay, this, [this, fragment] {
                rx_.append(fragment);
                emit readyRead();
            });
        }
        if (action.serialError.has_value()) {
            QTimer::singleShot(std::max(1, delay), this,
                               [this, code = *action.serialError] {
                                   lastError_ = code;
                                   emit errorOccurred(code);
                               });
        }
        return bytes.size();
    }

    QByteArray readAll() override
    {
        return std::exchange(rx_, {});
    }

    QString errorString() const override
    {
        return QStringLiteral("scripted transport error");
    }

    int errorCode() const override
    {
        return lastError_;
    }

private:
    std::shared_ptr<TransportState> state_;
    QByteArray rx_;
    int lastError_ = 0;
};

ModbusConnectionConfig config(qsizetype maxPending = 4,
                              std::chrono::milliseconds timeout = 100ms)
{
    ModbusConnectionConfig value;
    value.serial.portName = QStringLiteral("TEST");
    value.maxPendingRequests = maxPending;
    value.defaultResponseTimeout = timeout;
    return value;
}

RequestOptions testingOptions(std::chrono::milliseconds timeout = 100ms)
{
    RequestOptions options;
    options.owner = CommunicationOwner::Testing;
    options.responseTimeout = timeout;
    return options;
}

void openAndOwn(QSerialPortModbusClient &client,
                const ModbusConnectionConfig &connectionConfig)
{
    QSignalSpy controls(&client, &IModbusClient::controlCompleted);
    QVERIFY(client.open(connectionConfig).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(), ConnectionState::Connected, 1000);
    QCOMPARE(controls.count(), 1);
    QSignalSpy ownership(&client, &IModbusClient::ownershipChanged);
    QVERIFY(client.acquireOwnership(CommunicationOwner::Testing,
                                    HandoffMode::FinishInFlight).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(client.activeOwner(), CommunicationOwner::Testing, 1000);
    QCOMPARE(ownership.count(), 1);
    QCOMPARE(controls.count(), 2);
    const auto ownershipControl = qvariant_cast<ControlResult>(controls.at(1).at(0));
    QCOMPARE(ownershipControl.kind, ControlKind::AcquireOwnership);
    QVERIFY(ownershipControl.succeeded);
}

void enqueueAction(const std::shared_ptr<TransportState> &state,
                   TransportAction action)
{
    QMutexLocker lock(&state->mutex);
    state->actions.push_back(std::move(action));
}

ModbusRequestResult resultAt(const QSignalSpy &spy, int index)
{
    return qvariant_cast<ModbusRequestResult>(spy.at(index).at(0));
}

} // namespace

class QSerialPortModbusClientTest final : public QObject
{
    Q_OBJECT

private slots:
    void disconnectedContractAndOpenFailure();
    void workerThreadFragmentedReadAndEvidence();
    void fifoQueueFullCancellationAndRecovery();
    void timeoutAndCancelAllClose();
    void partialWriteAndSerialFault();
    void crcTrailingBytesAndResynchronization();
    void handoffAndCloseFinishInFlight();
    void repeatedLifecycleDoesNotDeadlock();
};

void QSerialPortModbusClientTest::disconnectedContractAndOpenFailure()
{
    auto state = std::make_shared<TransportState>();
    state->openSucceeds = false;
    QSerialPortModbusClient client([state] { return new ScriptedTransport(state); });
    test::verifyDisconnectedSubmissionContract(client);

    QSignalSpy completed(&client, &IModbusClient::controlCompleted);
    const auto submission = client.open(config());
    QVERIFY(submission.accepted());
    QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(), ConnectionState::Faulted, 1000);
    QCOMPARE(completed.count(), 1);
    const auto result = qvariant_cast<ControlResult>(completed.at(0).at(0));
    QVERIFY(!result.succeeded);
    QCOMPARE(result.error->code, ErrorCode::OpenFailed);

    QSignalSpy closed(&client, &IModbusClient::controlCompleted);
    QVERIFY(client.close().accepted());
    QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(), ConnectionState::Disconnected, 1000);
    QCOMPARE(closed.count(), 1);
}

void QSerialPortModbusClientTest::workerThreadFragmentedReadAndEvidence()
{
    auto state = std::make_shared<TransportState>();
    const QByteArray response = readResponse({0x0102, 0xA0B0});
    TransportAction action;
    for (char byte : response) {
        action.fragments.append(QByteArray(1, byte));
    }
    action.fragmentDelayMs = 1;
    enqueueAction(state, std::move(action));

    QSerialPortModbusClient client([state] { return new ScriptedTransport(state); });
    openAndOwn(client, config());
    QCOMPARE(client.thread(), QThread::currentThread());
    {
        QMutexLocker lock(&state->mutex);
        QVERIFY(state->createdIn != nullptr);
        QVERIFY(state->createdIn != QThread::currentThread());
        QCOMPARE(state->config.baudRate, 115200);
        QCOMPARE(state->config.dataBits, quint8(8));
    }

    QThread *completionThread = nullptr;
    connect(&client, &IModbusClient::requestCompleted, &client,
            [&completionThread](const ModbusRequestResult &) {
                completionThread = QThread::currentThread();
            });
    QSignalSpy completed(&client, &IModbusClient::requestCompleted);
    const auto submission = client.readHoldingRegisters(device::PduAddress(0),
                                                        2,
                                                        testingOptions());
    QVERIFY(submission.accepted());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
    const auto result = resultAt(completed, 0);
    QCOMPARE(result.state, RequestState::Succeeded);
    const auto read = std::get<ReadHoldingRegistersResult>(*result.success);
    QCOMPARE(read.values, QVector<quint16>({0x0102, 0xA0B0}));
    QCOMPARE(result.evidence.txAdu,
             encodeRequestAdu(1, ReadRequestDescriptor{device::PduAddress(0), 2}));
    QCOMPARE(result.evidence.rxAdu, response);
    QCOMPARE(result.evidence.txCrcStatus, CrcStatus::Valid);
    QCOMPARE(result.evidence.rxCrcStatus, CrcStatus::Valid);
    QVERIFY(result.evidence.txStartedUtc.has_value());
    QVERIFY(result.evidence.txAcceptedUtc.has_value());
    QVERIFY(result.evidence.firstRxUtc.has_value());
    QVERIFY(result.evidence.queueDelay.has_value());
    QVERIFY(result.evidence.rtt.has_value());
    QCOMPARE(completionThread, QThread::currentThread());

    QSignalSpy controls(&client, &IModbusClient::controlCompleted);
    QVERIFY(client.releaseOwnership(CommunicationOwner::Testing,
                                    HandoffMode::FinishInFlight).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(client.activeOwner(), CommunicationOwner::None, 1000);
    QCOMPARE(controls.count(), 1);
    const auto releaseControl = qvariant_cast<ControlResult>(controls.at(0).at(0));
    QCOMPARE(releaseControl.kind, ControlKind::ReleaseOwnership);
    QVERIFY(releaseControl.succeeded);
}

void QSerialPortModbusClientTest::fifoQueueFullCancellationAndRecovery()
{
    auto state = std::make_shared<TransportState>();
    enqueueAction(state, {}); // 第一请求保持在途。
    enqueueAction(state, TransportAction{{readResponse({0x1234})}, 0});
    QSerialPortModbusClient client([state] { return new ScriptedTransport(state); });
    openAndOwn(client, config(1, 30ms));

    QSignalSpy completed(&client, &IModbusClient::requestCompleted);
    const auto first = client.readHoldingRegisters(device::PduAddress(0), 1,
                                                   testingOptions(500ms));
    QVERIFY(first.accepted());
    QTRY_VERIFY_WITH_TIMEOUT([&state] {
        QMutexLocker lock(&state->mutex);
        return state->writes.size() == 1;
    }(), 1000);
    const auto queued = client.readHoldingRegisters(device::PduAddress(9), 1,
                                                    testingOptions(500ms));
    QVERIFY(queued.accepted());
    const auto full = client.readHoldingRegisters(device::PduAddress(10), 1,
                                                  testingOptions(500ms));
    QVERIFY(!full.accepted());
    QCOMPARE(full.rejection->code, ErrorCode::QueueFull);

    QVERIFY(client.cancelRequest(*queued.requestId).isAccepted);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
    QCOMPARE(resultAt(completed, 0).requestId, *queued.requestId);
    QCOMPARE(resultAt(completed, 0).state, RequestState::Cancelled);
    QVERIFY(!resultAt(completed, 0).evidence.txAcceptedUtc.has_value());
    QVERIFY(!resultAt(completed, 0).evidence.rtt.has_value());
    QVERIFY(client.cancelRequest(*first.requestId).isAccepted);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 1000);
    QCOMPARE(resultAt(completed, 1).requestId, *first.requestId);
    QCOMPARE(resultAt(completed, 1).state, RequestState::Cancelled);
    QVERIFY(!client.cancelRequest(*first.requestId).isAccepted);

    const auto recovered = client.readHoldingRegisters(device::PduAddress(19), 1,
                                                       testingOptions(100ms));
    QVERIFY(recovered.accepted());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 3, 1000);
    QCOMPARE(resultAt(completed, 2).state, RequestState::Succeeded);
    {
        QMutexLocker lock(&state->mutex);
        QCOMPARE(state->writes.size(), 2);
    }
}

void QSerialPortModbusClientTest::partialWriteAndSerialFault()
{
    auto state = std::make_shared<TransportState>();
    enqueueAction(state, TransportAction{{}, 0, 3});
    enqueueAction(state, TransportAction{{}, 0, -2,
        static_cast<int>(QSerialPort::ResourceError)});
    QSerialPortModbusClient client([state] { return new ScriptedTransport(state); });
    openAndOwn(client, config(4, 100ms));
    QSignalSpy completed(&client, &IModbusClient::requestCompleted);

    QVERIFY(client.readHoldingRegisters(device::PduAddress(0), 1,
                                        testingOptions()).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
    QCOMPARE(resultAt(completed, 0).error->code, ErrorCode::PartialWrite);
    QVERIFY(client.readHoldingRegisters(device::PduAddress(0), 1,
                                        testingOptions()).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(), ConnectionState::Faulted, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 1000);
    QCOMPARE(resultAt(completed, 1).error->code, ErrorCode::ResourceError);
    QVERIFY(client.close().accepted());
    QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(), ConnectionState::Disconnected, 1000);
}

void QSerialPortModbusClientTest::timeoutAndCancelAllClose()
{
    auto state = std::make_shared<TransportState>();
    enqueueAction(state, {}); // 超时。
    enqueueAction(state, TransportAction{{readResponse({0xCAFE})}, 0});
    enqueueAction(state, {}); // close(CancelAll) 取消在途。
    QSerialPortModbusClient client([state] { return new ScriptedTransport(state); });
    openAndOwn(client, config(4, 20ms));
    QSignalSpy completed(&client, &IModbusClient::requestCompleted);

    QVERIFY(client.readHoldingRegisters(device::PduAddress(0), 1,
                                        testingOptions(20ms)).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
    QCOMPARE(resultAt(completed, 0).error->code, ErrorCode::ResponseTimeout);
    QVERIFY(resultAt(completed, 0).evidence.txAcceptedUtc.has_value());
    QVERIFY(resultAt(completed, 0).evidence.rtt.has_value());

    QVERIFY(client.readHoldingRegisters(device::PduAddress(0), 1,
                                        testingOptions(100ms)).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 1000);
    QCOMPARE(resultAt(completed, 1).state, RequestState::Succeeded);

    QVERIFY(client.readHoldingRegisters(device::PduAddress(0), 1,
                                        testingOptions(500ms)).accepted());
    QTRY_VERIFY_WITH_TIMEOUT([&state] {
        QMutexLocker lock(&state->mutex);
        return state->writes.size() == 3;
    }(), 1000);
    QVERIFY(client.close(CloseMode::CancelAll).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 3, 1000);
    QCOMPARE(resultAt(completed, 2).error->code, ErrorCode::CancelledByClose);
    QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(), ConnectionState::Disconnected, 1000);
}

void QSerialPortModbusClientTest::crcTrailingBytesAndResynchronization()
{
    auto state = std::make_shared<TransportState>();
    QByteArray trailing = readResponse({1});
    trailing.append(char(0x55));
    QByteArray badCrc = readResponse({2});
    badCrc[badCrc.size() - 1] = char(badCrc.at(badCrc.size() - 1) ^ 0x01);
    enqueueAction(state, TransportAction{{trailing}, 0});
    enqueueAction(state, TransportAction{{badCrc}, 0});
    enqueueAction(state, TransportAction{{readResponse({3})}, 0});
    QSerialPortModbusClient client([state] { return new ScriptedTransport(state); });
    openAndOwn(client, config(4, 100ms));
    QSignalSpy completed(&client, &IModbusClient::requestCompleted);

    for (int index = 0; index < 3; ++index) {
        QVERIFY(client.readHoldingRegisters(device::PduAddress(0), 1,
                                            testingOptions()).accepted());
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), index + 1, 1000);
    }
    QCOMPARE(resultAt(completed, 0).error->code, ErrorCode::UnexpectedTrailingBytes);
    QCOMPARE(resultAt(completed, 0).evidence.rxAdu, trailing);
    QCOMPARE(resultAt(completed, 1).error->code, ErrorCode::ResponseCrcMismatch);
    QCOMPARE(resultAt(completed, 1).evidence.rxCrcStatus, CrcStatus::Invalid);
    QCOMPARE(resultAt(completed, 2).state, RequestState::Succeeded);
}

void QSerialPortModbusClientTest::handoffAndCloseFinishInFlight()
{
    auto state = std::make_shared<TransportState>();
    enqueueAction(state, TransportAction{{readResponse({1})}, 15});
    enqueueAction(state, TransportAction{{readResponse({2})}, 15});
    QSerialPortModbusClient client([state] { return new ScriptedTransport(state); });
    openAndOwn(client, config(4, 200ms));
    QSignalSpy completed(&client, &IModbusClient::requestCompleted);
    QSignalSpy ownership(&client, &IModbusClient::ownershipChanged);

    const auto first = client.readHoldingRegisters(device::PduAddress(0), 1,
                                                   testingOptions(200ms));
    const auto queued = client.readHoldingRegisters(device::PduAddress(9), 1,
                                                    testingOptions(200ms));
    QVERIFY(first.accepted());
    QVERIFY(queued.accepted());
    QVERIFY(client.acquireOwnership(CommunicationOwner::Monitor,
                                    HandoffMode::FinishInFlight).accepted());
    QVERIFY(!client.readHoldingRegisters(device::PduAddress(0), 1,
                                         testingOptions()).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 1000);
    QCOMPARE(resultAt(completed, 0).requestId, *queued.requestId);
    QCOMPARE(resultAt(completed, 0).error->code, ErrorCode::CancelledByHandoff);
    QCOMPARE(resultAt(completed, 1).requestId, *first.requestId);
    QCOMPARE(resultAt(completed, 1).state, RequestState::Succeeded);
    QTRY_COMPARE_WITH_TIMEOUT(client.activeOwner(), CommunicationOwner::Monitor, 1000);
    QVERIFY(ownership.count() >= 1);

    RequestOptions monitor;
    monitor.owner = CommunicationOwner::Monitor;
    monitor.responseTimeout = 200ms;
    const auto last = client.readHoldingRegisters(device::PduAddress(19), 1, monitor);
    QVERIFY(last.accepted());
    QVERIFY(client.close(CloseMode::FinishInFlight).accepted());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 3, 1000);
    QCOMPARE(resultAt(completed, 2).requestId, *last.requestId);
    QCOMPARE(resultAt(completed, 2).state, RequestState::Succeeded);
    QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(), ConnectionState::Disconnected, 1000);
}

void QSerialPortModbusClientTest::repeatedLifecycleDoesNotDeadlock()
{
    for (int iteration = 0; iteration < 20; ++iteration) {
        auto state = std::make_shared<TransportState>();
        QSerialPortModbusClient client([state] { return new ScriptedTransport(state); });
        if ((iteration % 2) == 0) {
            QSignalSpy completed(&client, &IModbusClient::controlCompleted);
            QVERIFY(client.open(config()).accepted());
            QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(),
                                      ConnectionState::Connected,
                                      1000);
            QVERIFY(client.close().accepted());
            QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(),
                                      ConnectionState::Disconnected,
                                      1000);
            QCOMPARE(completed.count(), 2);
        }
    }
}

QTEST_GUILESS_MAIN(QSerialPortModbusClientTest)

#include "tst_qserialportmodbusclient.moc"
