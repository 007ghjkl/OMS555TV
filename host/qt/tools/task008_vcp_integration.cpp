#include "communication/QSerialPortModbusClient.h"
#include "device/RegisterCodec.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QSerialPortInfo>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <optional>
#include <variant>

using namespace std::chrono_literals;
using namespace oms555tv;
using namespace oms555tv::communication;

namespace {

QTextStream output(stdout);
QTextStream errors(stderr);

QString hex(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

// ErrorCode 不是 Q_ENUM，报告仍使用稳定的整数和关键分支名称。
QString stableErrorName(ErrorCode code)
{
    switch (code) {
    case ErrorCode::ResponseTimeout:
        return QStringLiteral("ResponseTimeout");
    case ErrorCode::ModbusIllegalDataAddress:
        return QStringLiteral("ModbusIllegalDataAddress");
    case ErrorCode::ModbusIllegalDataValue:
        return QStringLiteral("ModbusIllegalDataValue");
    default:
        return QStringLiteral("ErrorCode(%1)").arg(static_cast<int>(code));
    }
}

template<typename Result, typename Signal, typename Match>
std::optional<Result> waitForSignal(IModbusClient *sender,
                                    Signal signal,
                                    Match match,
                                    int timeoutMs = 5000)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    std::optional<Result> found;
    QObject::connect(sender, signal, &loop, [&](const Result &result) {
        if (match(result)) {
            found = result;
            loop.quit();
        }
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    return found;
}

std::optional<ControlResult> waitControl(QSerialPortModbusClient &client,
                                         const ControlSubmission &submission)
{
    if (!submission.accepted()) {
        return std::nullopt;
    }
    const OperationId id = *submission.operationId;
    return waitForSignal<ControlResult>(
        &client, &IModbusClient::controlCompleted,
        [id](const ControlResult &result) { return result.operationId == id; });
}

std::optional<OwnershipResult> waitOwnership(QSerialPortModbusClient &client,
                                             const ControlSubmission &submission)
{
    if (!submission.accepted()) {
        return std::nullopt;
    }
    const OperationId id = *submission.operationId;
    return waitForSignal<OwnershipResult>(
        &client, &IModbusClient::ownershipChanged,
        [id](const OwnershipResult &result) { return result.operationId == id; });
}

std::optional<ModbusRequestResult> waitRequest(QSerialPortModbusClient &client,
                                               const RequestSubmission &submission,
                                               int timeoutMs = 5000)
{
    if (!submission.accepted()) {
        return std::nullopt;
    }
    const RequestId id = *submission.requestId;
    return waitForSignal<ModbusRequestResult>(
        &client, &IModbusClient::requestCompleted,
        [id](const ModbusRequestResult &result) { return result.requestId == id; },
        timeoutMs);
}

void printEvidence(const ModbusRequestResult &result)
{
    const double rttMs = result.evidence.rtt.has_value()
        ? std::chrono::duration<double, std::milli>(*result.evidence.rtt).count()
        : -1.0;
    output << "request=" << result.requestId.value
           << " state=" << static_cast<int>(result.state)
           << " TX=[" << hex(result.evidence.txAdu) << "]"
           << " RX=[" << hex(result.evidence.rxAdu) << "]"
           << " RTT_ms=" << QString::number(rttMs, 'f', 3);
    if (result.error) {
        output << " error=" << stableErrorName(result.error->code);
        if (result.error->exceptionCode) {
            output << " exception=0x"
                   << QString::number(*result.error->exceptionCode, 16).rightJustified(2, '0');
        }
    }
    output << Qt::endl;
}

QString selectPort(const QString &requested)
{
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    output << "枚举串口数量=" << ports.size() << Qt::endl;
    QList<QString> candidates;
    for (const auto &port : ports) {
        output << "PORT name=" << port.portName()
               << " description=" << port.description()
               << " manufacturer=" << port.manufacturer()
               << " serial=" << port.serialNumber();
        if (port.hasVendorIdentifier()) {
            output << " vid=0x" << QString::number(port.vendorIdentifier(), 16);
        }
        if (port.hasProductIdentifier()) {
            output << " pid=0x" << QString::number(port.productIdentifier(), 16);
        }
        output << Qt::endl;
        if (port.portName().compare(requested, Qt::CaseInsensitive) == 0) {
            return port.portName();
        }
        const QString identity = port.description() + QLatin1Char(' ')
            + port.manufacturer();
        if (identity.contains(QStringLiteral("STLink"), Qt::CaseInsensitive)
            || identity.contains(QStringLiteral("ST-LINK"), Qt::CaseInsensitive)
            || identity.contains(QStringLiteral("STMicroelectronics"),
                                 Qt::CaseInsensitive)
            || (port.hasVendorIdentifier() && port.vendorIdentifier() == 0x0483)) {
            candidates.append(port.portName());
        }
    }
    if (!requested.isEmpty()) {
        return {};
    }
    return candidates.size() == 1 ? candidates.front() : QString{};
}

ModbusConnectionConfig connectionConfig(const QString &port, quint8 serverAddress = 1)
{
    ModbusConnectionConfig config;
    config.serial.portName = port;
    config.serial.baudRate = 115200;
    config.serial.dataBits = 8;
    config.serial.parity = SerialParity::None;
    config.serial.stopBits = SerialStopBits::One;
    config.serial.flowControl = SerialFlowControl::None;
    config.serverAddress = serverAddress;
    config.defaultResponseTimeout = 500ms;
    config.maxPendingRequests = 64;
    return config;
}

bool connectTesting(QSerialPortModbusClient &client,
                    const ModbusConnectionConfig &config)
{
    const auto opened = waitControl(client, client.open(config));
    if (!opened || !opened->succeeded) {
        errors << "打开失败";
        if (opened && opened->error) {
            errors << ": " << opened->error->diagnostic;
        }
        errors << Qt::endl;
        return false;
    }
    const auto owned = waitOwnership(
        client,
        client.acquireOwnership(CommunicationOwner::Testing,
                                HandoffMode::FinishInFlight));
    return owned && owned->succeeded;
}

std::optional<ModbusRequestResult> readBlock(QSerialPortModbusClient &client,
                                             device::ReadBlockDefinition block,
                                             bool print = true)
{
    RequestOptions options;
    options.owner = CommunicationOwner::Testing;
    options.responseTimeout = 500ms;
    const auto result = waitRequest(
        client,
        client.readHoldingRegisters(block.startAddress, block.count, options));
    if (result && print) {
        printEvidence(*result);
    }
    return result;
}

std::optional<QVector<quint16>> readValues(QSerialPortModbusClient &client,
                                          device::ReadBlockDefinition block,
                                          bool print = true)
{
    const auto result = readBlock(client, block, print);
    if (!result || result->state != RequestState::Succeeded || !result->success) {
        return std::nullopt;
    }
    return std::get<ReadHoldingRegistersResult>(*result->success).values;
}

std::optional<ModbusRequestResult> writeRaw(QSerialPortModbusClient &client,
                                            device::PduAddress address,
                                            quint16 value,
                                            bool print = true)
{
    RequestOptions options;
    options.owner = CommunicationOwner::Testing;
    options.responseTimeout = 500ms;
    const auto result = waitRequest(
        client, client.writeSingleRegister(address, value, options));
    if (result && print) {
        printEvidence(*result);
    }
    return result;
}

bool restoreThresholds(QSerialPortModbusClient &client,
                       const QVector<quint16> &baseline)
{
    if (baseline.size() != 4) {
        return false;
    }
    bool ok = true;
    for (int index = 0; index < baseline.size(); ++index) {
        const auto result = writeRaw(
            client,
            device::PduAddress(static_cast<quint16>(
                device::thresholdsReadBlock.startAddress.value() + index)),
            baseline[index]);
        ok = ok && result && result->state == RequestState::Succeeded;
    }
    const auto finalValues = readValues(client, device::thresholdsReadBlock);
    return ok && finalValues && *finalValues == baseline;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("task008_vcp_integration"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("port"),
                      QStringLiteral("指定本次枚举结果中的串口名。"),
                      QStringLiteral("name")});
    parser.addOption({QStringLiteral("stress-count"),
                      QStringLiteral("连续 0x03 次数。"),
                      QStringLiteral("count"),
                      QStringLiteral("500")});
    parser.process(app);

    bool countOk = false;
    const int stressCount = parser.value(QStringLiteral("stress-count")).toInt(&countOk);
    if (!countOk || stressCount < 1) {
        errors << "stress-count 必须为正整数。" << Qt::endl;
        return 2;
    }
    const QString port = selectPort(parser.value(QStringLiteral("port")));
    if (port.isEmpty()) {
        errors << "未能唯一确定 ST-LINK VCP；请使用 --port 指定枚举结果。" << Qt::endl;
        return 2;
    }
    output << "SELECTED_PORT=" << port << Qt::endl;
    output << "CONFIG=115200 8N1 flow=None slave=1 timeout=500ms" << Qt::endl;

    QSerialPortModbusClient client;
    if (!connectTesting(client, connectionConfig(port))) {
        return 3;
    }

    bool passed = true;
    std::optional<quint16> initialCommunicationErrors;
    QVector<device::RegisterBlock> blocks;
    for (const auto block : device::deviceSnapshotReadBlocks) {
        const auto values = readValues(client, block);
        if (!values) {
            passed = false;
            break;
        }
        blocks.append({block.startAddress, *values});
    }
    const auto decoded = device::decodeDeviceSnapshot(blocks);
    if (const auto *snapshot = std::get_if<device::DeviceSnapshot>(&decoded)) {
        output << "SNAPSHOT tempA=" << snapshot->measurements.phaseATemperature.celsius()
               << " tempB=" << snapshot->measurements.phaseBTemperature.celsius()
               << " tempC=" << snapshot->measurements.phaseCTemperature.celsius()
               << " ambient=" << snapshot->measurements.ambientTemperature.celsius()
               << " light_mV=" << snapshot->measurements.lightMillivolts
               << " alarm=0x" << QString::number(snapshot->status.alarms.raw, 16)
               << " status=0x" << QString::number(snapshot->status.device.raw, 16)
               << " comm_errors=" << snapshot->diagnostics.communicationErrorCount
               << " uptime_s=" << snapshot->diagnostics.uptimeSeconds
               << " firmware=" << snapshot->firmwareVersion.major << '.'
               << snapshot->firmwareVersion.minor << Qt::endl;
        passed = passed && snapshot->firmwareVersion.major == 0
            && snapshot->firmwareVersion.minor == 2
            && snapshot->status.device.running();
        initialCommunicationErrors = snapshot->diagnostics.communicationErrorCount;
    } else {
        errors << "RegisterCodec 完整快照解码失败。" << Qt::endl;
        passed = false;
    }

    const auto baseline = readValues(client, device::thresholdsReadBlock);
    if (!baseline) {
        passed = false;
    } else {
        output << "THRESHOLD_BASELINE=";
        for (quint16 value : *baseline) {
            output << value << ' ';
        }
        output << Qt::endl;
        const std::array<double, 4> testValues{55.0, 80.0, -40.0, 45.0};
        const std::array<device::TemperatureChannel, 4> channels{
            device::TemperatureChannel::PhaseA,
            device::TemperatureChannel::PhaseB,
            device::TemperatureChannel::PhaseC,
            device::TemperatureChannel::Ambient};
        for (std::size_t index = 0; index < channels.size(); ++index) {
            const auto encoded = device::encodeAlarmThreshold(channels[index],
                                                               testValues[index]);
            const auto *write = std::get_if<device::RegisterWrite>(&encoded);
            if (!write) {
                passed = false;
                continue;
            }
            const auto written = writeRaw(client, write->address, write->rawValue);
            const auto readBack = readValues(client, {write->address, 1});
            passed = passed && written && written->state == RequestState::Succeeded
                && readBack && readBack->size() == 1
                && readBack->front() == write->rawValue;
        }

        RequestOptions options;
        options.owner = CommunicationOwner::Testing;
        options.responseTimeout = 500ms;
        const auto illegalAddress = waitRequest(
            client,
            client.readHoldingRegisters(device::PduAddress(5), 1, options));
        if (illegalAddress) {
            printEvidence(*illegalAddress);
        }
        passed = passed && illegalAddress && illegalAddress->error
            && illegalAddress->error->code == ErrorCode::ModbusIllegalDataAddress;
        const auto illegalValue = writeRaw(client, device::PduAddress(9), 801);
        passed = passed && illegalValue && illegalValue->error
            && illegalValue->error->code == ErrorCode::ModbusIllegalDataValue;

        const bool restored = restoreThresholds(client, *baseline);
        output << "THRESHOLD_RESTORED=" << (restored ? "YES" : "NO") << Qt::endl;
        passed = passed && restored;
    }

    int succeeded = 0;
    int failed = 0;
    int timedOut = 0;
    QVector<double> rttMilliseconds;
    rttMilliseconds.reserve(stressCount);
    for (int index = 0; index < stressCount; ++index) {
        const auto result = readBlock(client, device::measurementsReadBlock, false);
        if (result && result->state == RequestState::Succeeded) {
            ++succeeded;
            if (result->evidence.rtt) {
                rttMilliseconds.append(
                    std::chrono::duration<double, std::milli>(
                        *result->evidence.rtt).count());
            }
        } else {
            ++failed;
            if (result && result->error
                && result->error->code == ErrorCode::ResponseTimeout) {
                ++timedOut;
            }
        }
    }
    const auto minmax = std::minmax_element(rttMilliseconds.begin(),
                                            rttMilliseconds.end());
    const double average = rttMilliseconds.isEmpty()
        ? 0.0
        : std::accumulate(rttMilliseconds.begin(),
                          rttMilliseconds.end(), 0.0)
            / rttMilliseconds.size();
    output << "STRESS total=" << stressCount
           << " success=" << succeeded
           << " failed=" << failed
           << " timeout=" << timedOut;
    if (!rttMilliseconds.isEmpty()) {
        output << " RTT_ms_min=" << QString::number(*minmax.first, 'f', 3)
               << " avg=" << QString::number(average, 'f', 3)
               << " max=" << QString::number(*minmax.second, 'f', 3);
    }
    output << Qt::endl;
    passed = passed && succeeded == stressCount && failed == 0;

    const auto diagnosticsAfterStress = readValues(client, device::diagnosticsReadBlock);
    const bool diagnosticsStable = initialCommunicationErrors
        && diagnosticsAfterStress && diagnosticsAfterStress->size() == 3
        && diagnosticsAfterStress->front() == *initialCommunicationErrors;
    output << "COMM_ERROR_COUNT baseline="
           << (initialCommunicationErrors
                   ? QString::number(*initialCommunicationErrors)
                   : QStringLiteral("NA"))
           << " after_stress="
           << (diagnosticsAfterStress && !diagnosticsAfterStress->isEmpty()
                   ? QString::number(diagnosticsAfterStress->front())
                   : QStringLiteral("NA"))
           << " stable=" << (diagnosticsStable ? "YES" : "NO") << Qt::endl;
    passed = passed && diagnosticsStable;

    const auto closed = waitControl(client, client.close());
    passed = passed && closed && closed->succeeded;
    if (connectTesting(client, connectionConfig(port, 247))) {
        RequestOptions options;
        options.owner = CommunicationOwner::Testing;
        options.responseTimeout = 100ms;
        const auto noResponse = waitRequest(
            client,
            client.readHoldingRegisters(device::versionReadBlock.startAddress,
                                        device::versionReadBlock.count,
                                        options));
        if (noResponse) {
            printEvidence(*noResponse);
        }
        const bool timeoutObserved = noResponse && noResponse->error
            && noResponse->error->code == ErrorCode::ResponseTimeout;
        output << "NO_RESPONSE_TIMEOUT=" << (timeoutObserved ? "YES" : "NO")
               << Qt::endl;
        passed = passed && timeoutObserved;
        const auto wrongSlaveClosed = waitControl(client, client.close());
        passed = passed && wrongSlaveClosed && wrongSlaveClosed->succeeded;
    } else {
        passed = false;
    }
    if (connectTesting(client, connectionConfig(port))) {
        const auto recovered = readValues(client, device::versionReadBlock);
        const bool recoveredOk = recovered && recovered->size() == 2
            && recovered->at(0) == 0 && recovered->at(1) == 2;
        output << "REOPEN_RECOVERED=" << (recoveredOk ? "YES" : "NO") << Qt::endl;
        passed = passed && recoveredOk;
        (void)waitControl(client, client.close());
    } else {
        passed = false;
    }

    output << "RESULT=" << (passed ? "PASS" : "FAIL") << Qt::endl;
    return passed ? 0 : 4;
}
