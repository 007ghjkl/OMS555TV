#include "app/AppStateController.h"
#include "app/MainWindow.h"
#include "communication/QSerialPortModbusClient.h"
#include "configuration/ConfigurationService.h"
#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorScheduler.h"
#include "monitor/MonitorService.h"
#include "testing/TestAutomationController.h"
#include "testing/TestEngine.h"
#include "testing/TestResultManager.h"
#include "ui/MonitoringViewModel.h"
#include "ui/SerialPortCatalog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <optional>

using namespace oms555tv;

namespace {

enum class ValidationMode { Preflight, Abort, Full };

QString modeName(const ValidationMode mode)
{
    switch (mode) {
    case ValidationMode::Preflight: return QStringLiteral("preflight");
    case ValidationMode::Abort: return QStringLiteral("abort");
    case ValidationMode::Full: return QStringLiteral("full");
    }
    return {};
}

std::optional<ValidationMode> parseMode(const QString &value)
{
    if (value == QStringLiteral("preflight")) return ValidationMode::Preflight;
    if (value == QStringLiteral("abort")) return ValidationMode::Abort;
    if (value == QStringLiteral("full")) return ValidationMode::Full;
    return std::nullopt;
}

QString sha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex().toUpper());
}

QString thresholdText(const device::AlarmThresholds &value)
{
    return QStringLiteral("A=%1, B=%2, C=%3, ambient=%4 ℃")
        .arg(value.phaseA.celsius(), 0, 'f', 1)
        .arg(value.phaseB.celsius(), 0, 'f', 1)
        .arg(value.phaseC.celsius(), 0, 'f', 1)
        .arg(value.ambient.celsius(), 0, 'f', 1);
}

bool equalThresholds(const device::AlarmThresholds &left,
                     const device::AlarmThresholds &right)
{
    return left.phaseA.deciCelsius == right.phaseA.deciCelsius
        && left.phaseB.deciCelsius == right.phaseB.deciCelsius
        && left.phaseC.deciCelsius == right.phaseC.deciCelsius
        && left.ambient.deciCelsius == right.ambient.deciCelsius;
}

QSet<QString> fullCaseIds()
{
    return {QStringLiteral("TC-F001"), QStringLiteral("TC-F002"),
            QStringLiteral("TC-F003"), QStringLiteral("TC-F004"),
            QStringLiteral("TC-F005"), QStringLiteral("TC-F006"),
            QStringLiteral("TC-F007"), QStringLiteral("TC-F008"),
            QStringLiteral("TC-P001"), QStringLiteral("TC-P002"),
            QStringLiteral("TC-P004"), QStringLiteral("TC-P007"),
            QStringLiteral("TC-B003"), QStringLiteral("TC-B004"),
            QStringLiteral("TC-B005"), QStringLiteral("TC-B006"),
            QStringLiteral("TC-D002"), QStringLiteral("TC-D003"),
            QStringLiteral("TC-R-AUTO-001"), QStringLiteral("TC-S001")};
}

QSet<QString> preflightCaseIds()
{
    return {QStringLiteral("TC-F001"), QStringLiteral("TC-F005"),
            QStringLiteral("TC-P004"), QStringLiteral("TC-R-AUTO-001")};
}

const testing::TestCaseResult *caseResult(const testing::TestSuiteResult &result,
                                          const QString &id)
{
    const auto found = std::find_if(
        result.cases.cbegin(), result.cases.cend(), [&id](const auto &candidate) {
            return candidate.caseId == id;
        });
    return found == result.cases.cend() ? nullptr : &*found;
}

QString actualText(const testing::TestCaseResult &result)
{
    if (result.assertion) return result.assertion->actualSummary;
    if (result.error) return result.error->diagnostic;
    if (result.skipReason) return testing::testSkipReasonName(*result.skipReason);
    return QStringLiteral("--");
}

class ValidationRunner final : public QObject
{
public:
    ValidationRunner(QString portName, QString suitePath, QString sourceRevision,
                     ValidationMode mode, int timeoutMs, int abortAfterMs,
                     QObject *parent = nullptr)
        : QObject(parent)
        , portName_(std::move(portName))
        , suitePath_(QFileInfo(std::move(suitePath)).absoluteFilePath())
        , sourceRevision_(std::move(sourceRevision))
        , mode_(mode)
        , timeoutMs_(timeoutMs)
        , abortAfterMs_(abortAfterMs)
        , monitorService_(client_, monitorScheduler_)
        , appState_(client_, monitorService_)
        , monitoring_(appState_, monitorService_, ports_)
        , configuration_(appState_, client_)
        , diagnostics_(client_, 2000)
        , sessionLog_(diagnostics_, {}, 2000)
        , engine_(appState_, client_, results_, &sessionLog_)
        , automation_(appState_, engine_, results_)
        , window_(monitoring_, configuration_, diagnostics_, sessionLog_, automation_)
    {
        window_.setWindowTitle(QStringLiteral("TASK-020 Phase 6 真实 RS485 验收 — %1")
                                   .arg(modeName(mode_)));
        connect(&automation_, &testing::TestAutomationController::suiteChanged,
                this, [this] { connectDeviceAfterLoad(); });
        connect(&automation_, &testing::TestAutomationController::workflowFinished,
                this, [this] { handleWorkflowFinished(); });
        connect(&automation_, &testing::TestAutomationController::currentStepChanged,
                this, [this] { armAbortWhenRunning(); });
        connect(&appState_, &app::AppStateController::commandCompleted,
                this, [this](const app::AppCommandResult &result) {
            handleAppCommand(result);
        });
        connect(&configuration_, &configuration::ConfigurationService::operationCompleted,
                this, [this](const configuration::ConfigurationOperationResult &result) {
            handleThresholdResult(result);
        });

        heartbeat_.setInterval(100);
        connect(&heartbeat_, &QTimer::timeout, this, [this] { recordHeartbeat(); });
        watchdog_.setSingleShot(true);
        connect(&watchdog_, &QTimer::timeout, this, [this] {
            fail(QStringLiteral("验收流程超过 %1 ms watchdog").arg(watchdog_.interval()));
        });
        abortTimer_.setSingleShot(true);
        connect(&abortTimer_, &QTimer::timeout, this, [this] {
            QTextStream(stdout) << "ABORT_TRIGGERED_AFTER_MS=" << abortAfterMs_ << Qt::endl;
            if (!automation_.abort()) {
                fail(QStringLiteral("中止专项触发时控制器未接受 abort"));
            }
        });
    }

    void start()
    {
        window_.show();
        elapsed_.start();
        heartbeat_.start();
        suiteSha256_ = sha256(suitePath_);
        binarySha256_ = sha256(QCoreApplication::applicationFilePath());
        if (suiteSha256_.isEmpty() || binarySha256_.isEmpty()) {
            fail(QStringLiteral("无法计算套件或验收程序 SHA-256"));
            return;
        }
        if (!validatePortIdentity()) return;

        const auto log = sessionLog_.startSession({
            {QStringLiteral("task"), QStringLiteral("TASK-020")},
            {QStringLiteral("mode"), modeName(mode_)},
            {QStringLiteral("source_revision"), sourceRevision_},
            {QStringLiteral("host_version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("host_binary_sha256"), binarySha256_},
            {QStringLiteral("firmware_expected"), QStringLiteral("0.2")},
            {QStringLiteral("hardware"),
             QStringLiteral("NUCLEO-F411RE / USART1 / MAX13487EESA / DTECH USB-RS485 / 20cm")},
            {QStringLiteral("port"), portName_},
            {QStringLiteral("baud"), 115200},
            {QStringLiteral("serial_format"), QStringLiteral("8N1/no-flow-control")},
            {QStringLiteral("slave_id"), 1},
            {QStringLiteral("timeout_ms"), timeoutMs_},
            {QStringLiteral("suite"), suitePath_},
            {QStringLiteral("suite_sha256"), suiteSha256_},
        });
        if (!log.succeeded) {
            fail(QStringLiteral("无法创建会话日志：%1").arg(log.error->diagnostic));
            return;
        }
        logPath_ = log.filePath;
        QTextStream(stdout) << "MODE=" << modeName(mode_) << Qt::endl
                            << "SOURCE_REVISION=" << sourceRevision_ << Qt::endl
                            << "SUITE_PATH=" << suitePath_ << Qt::endl
                            << "SUITE_SHA256=" << suiteSha256_ << Qt::endl
                            << "HOST_BINARY_SHA256=" << binarySha256_ << Qt::endl
                            << "SESSION_LOG=" << logPath_ << Qt::endl;

        watchdog_.setInterval(mode_ == ValidationMode::Full ? 900000 : 60000);
        watchdog_.start();
        stage_ = Stage::Loading;
        if (!automation_.loadSuiteFile(suitePath_)) {
            fail(QStringLiteral("套件加载命令被拒绝"));
        }
    }

private:
    enum class Stage { Starting, Loading, Connecting, ReadingBaseline, Running,
                       ReadingFinal, Disconnecting, Finished };

    bool validatePortIdentity()
    {
        const auto available = ports_.availablePorts();
        const ui::SerialPortEntry *selected = nullptr;
        QTextStream out(stdout);
        for (const auto &port : available) {
            const QString vid = port.vendorIdentifier
                ? QStringLiteral("%1").arg(*port.vendorIdentifier, 4, 16, QLatin1Char('0')).toUpper()
                : QStringLiteral("----");
            const QString pid = port.productIdentifier
                ? QStringLiteral("%1").arg(*port.productIdentifier, 4, 16, QLatin1Char('0')).toUpper()
                : QStringLiteral("----");
            out << "PORT=" << port.portName << " DESCRIPTION=" << port.description
                << " MANUFACTURER=" << port.manufacturer << " SERIAL=" << port.serialNumber
                << " VID=" << vid << " PID=" << pid << Qt::endl;
            if (port.portName.compare(portName_, Qt::CaseInsensitive) == 0) selected = &port;
        }
        if (!selected) {
            fail(QStringLiteral("运行时枚举中不存在端口 %1").arg(portName_));
            return false;
        }
        if (!selected->vendorIdentifier || !selected->productIdentifier
            || *selected->vendorIdentifier != 0x345f
            || *selected->productIdentifier != 0x3020
            || selected->serialNumber != QStringLiteral("A02001JS")) {
            fail(QStringLiteral(
                "所选端口身份不符合 TASK-010 基线（要求 VID=345F PID=3020 SERIAL=A02001JS）"));
            return false;
        }
        QTextStream(stdout) << "PORT_BASELINE=PASS" << Qt::endl;
        return true;
    }

    bool validateSuite(const testing::TestSuite &suite)
    {
        if (suite.schemaVersion != testing::testSuiteSchemaVersionV2
            || suite.id != QStringLiteral("phase6-rs485-full")
            || suite.cases.size() != 20) {
            return false;
        }
        QSet<QString> ids;
        QMap<QString, int> counts;
        for (const auto &testCase : suite.cases) {
            if (!testCase.enabled || testCase.environment == testing::ExecutionEnvironment::Fake) {
                return false;
            }
            ids.insert(testCase.id);
            ++counts[testCase.category];
        }
        return ids == fullCaseIds()
            && counts[QStringLiteral("functional")] >= 8
            && counts[QStringLiteral("protocol")] >= 4
            && counts[QStringLiteral("boundary")] >= 4
            && counts[QStringLiteral("consistency")] >= 2
            && counts[QStringLiteral("recovery")] >= 1
            && counts[QStringLiteral("stability")] >= 1;
    }

    void connectDeviceAfterLoad()
    {
        if (stage_ != Stage::Loading) return;
        if (!automation_.suite()) {
            QStringList errors;
            for (const auto &error : automation_.loadErrors()) {
                errors << QStringLiteral("%1 %2：%3")
                    .arg(testing::configErrorCodeName(error.code), error.path,
                         error.diagnostic);
            }
            fail(automation_.lastError().isEmpty()
                ? QStringLiteral("套件加载失败：%1").arg(errors.join(QStringLiteral("；")))
                : automation_.lastError());
            return;
        }
        if (!validateSuite(*automation_.suite())) {
            fail(QStringLiteral("套件不是经审核的 20 条 Phase 6 正式主套件"));
            return;
        }
        QTextStream(stdout) << "SUITE=" << automation_.suite()->id
                            << " CASES=" << automation_.suite()->cases.size()
                            << " CATALOG=PASS" << Qt::endl;
        monitoring_.refreshPorts();
        monitoring_.setSelectedPortName(portName_);
        monitoring_.setSlaveAddress(1);
        monitoring_.setResponseTimeoutMs(timeoutMs_);
        stage_ = Stage::Connecting;
        communication::ModbusConnectionConfig config;
        config.serial.portName = portName_;
        config.serial.baudRate = 115200;
        config.serial.dataBits = 8;
        config.serial.parity = communication::SerialParity::None;
        config.serial.stopBits = communication::SerialStopBits::One;
        config.serial.flowControl = communication::SerialFlowControl::None;
        config.serverAddress = 1;
        config.defaultResponseTimeout = std::chrono::milliseconds(timeoutMs_);
        if (!appState_.connectDevice(config).accepted()) {
            fail(QStringLiteral("连接命令未被接受"));
        }
    }

    void handleAppCommand(const app::AppCommandResult &result)
    {
        if (result.command == app::AppCommand::Connect) {
            if (!result.succeeded) {
                fail(QStringLiteral("连接命令失败"));
                return;
            }
            if (failed_) {
                disconnectDevice();
                return;
            }
            stage_ = Stage::ReadingBaseline;
            if (!configuration_.readThresholds().accepted()) {
                fail(QStringLiteral("测试前阈值读取未被接受"));
            }
        } else if (result.command == app::AppCommand::Disconnect) {
            if (!result.succeeded) failed_ = true;
            finish();
        }
    }

    void handleThresholdResult(const configuration::ConfigurationOperationResult &result)
    {
        if (stage_ == Stage::ReadingBaseline) {
            if (failed_ || !result.succeeded || !result.verifiedThresholds) {
                fail(QStringLiteral("无法建立测试前阈值基线"));
                if (appState_.state() == app::AppState::ConnectedIdle) disconnectDevice();
                return;
            }
            baseline_ = *result.verifiedThresholds;
            QTextStream(stdout) << "BEFORE=" << thresholdText(*baseline_) << Qt::endl;
            startSuite();
            return;
        }
        if (stage_ == Stage::ReadingFinal) {
            finalThresholds_ = result.verifiedThresholds;
            thresholdPassed_ = result.succeeded && result.verifiedThresholds && baseline_
                && equalThresholds(*result.verifiedThresholds, *baseline_);
            if (result.verifiedThresholds) {
                QTextStream(stdout) << "FINAL=" << thresholdText(*result.verifiedThresholds)
                                    << Qt::endl;
            }
            if (!thresholdPassed_) {
                failed_ = true;
                QTextStream(stderr) << "ERROR=最终阈值与测试前基线不一致" << Qt::endl;
            }
            disconnectDevice();
        }
    }

    void startSuite()
    {
        stage_ = Stage::Running;
        runHeartbeatTicks_ = 0;
        maximumHeartbeatLateMs_ = 0;
        lastHeartbeatMs_ = elapsed_.elapsed();
        monitor::MonitorConfig config;
        config.requestTimeout = std::chrono::milliseconds(timeoutMs_);
        bool accepted = false;
        if (mode_ == ValidationMode::Full) {
            accepted = automation_.runAll(config);
        } else if (mode_ == ValidationMode::Preflight) {
            accepted = automation_.runSelected(preflightCaseIds(), config);
        } else {
            accepted = automation_.runSelected({QStringLiteral("TC-S001")}, config);
        }
        if (!accepted) {
            fail(QStringLiteral("自动化运行未被接受：%1").arg(automation_.lastError()));
        }
    }

    void armAbortWhenRunning()
    {
        if (mode_ != ValidationMode::Abort || stage_ != Stage::Running
            || abortTimer_.isActive() || abortArmed_
            || automation_.currentCaseId() != QStringLiteral("TC-S001")
            || !automation_.currentRequestId()) {
            return;
        }
        abortArmed_ = true;
        abortTimer_.start(abortAfterMs_);
        QTextStream(stdout) << "ABORT_ARMED_REQUEST_ID="
                            << automation_.currentRequestId()->value << Qt::endl;
    }

    void recordHeartbeat()
    {
        const qint64 now = elapsed_.elapsed();
        if (stage_ == Stage::Running) {
            if (lastHeartbeatMs_ > 0) {
                maximumHeartbeatLateMs_ = std::max(
                    maximumHeartbeatLateMs_, std::max<qint64>(0, now - lastHeartbeatMs_ - 100));
            }
            ++runHeartbeatTicks_;
        }
        lastHeartbeatMs_ = now;
    }

    bool auditEvidence(const testing::TestSuiteResult &result)
    {
        quint64 expectedTotal = 0;
        QSet<quint64> retainedIds;
        bool retainedUnique = true;
        bool retainedEvidence = true;
        for (const auto &testCase : result.cases) {
            expectedTotal += testCase.evidenceRetention.totalAttempts;
            for (const auto &attempt : testCase.attempts) {
                if (retainedIds.contains(attempt.requestId.value)) retainedUnique = false;
                retainedIds.insert(attempt.requestId.value);
                const auto &evidence = attempt.requestResult.evidence;
                if (evidence.txAdu.isEmpty() || !evidence.rtt) retainedEvidence = false;
                if (mode_ != ValidationMode::Abort && evidence.rxAdu.isEmpty()) {
                    retainedEvidence = false;
                }
            }
        }

        QSet<quint64> diagnosticIds;
        QMap<quint64, communication::ModbusRequestResult> diagnosticsById;
        quint64 diagnosticCount = 0;
        bool diagnosticUnique = true;
        for (const auto &record : diagnostics_.records()) {
            if (record.result.evidence.owner != communication::CommunicationOwner::Testing) continue;
            ++diagnosticCount;
            if (diagnosticIds.contains(record.result.requestId.value)) diagnosticUnique = false;
            diagnosticIds.insert(record.result.requestId.value);
            diagnosticsById.insert(record.result.requestId.value, record.result);
        }

        QSet<quint64> testLogIds;
        quint64 testLogCount = 0;
        bool testLogUnique = true;
        QSet<quint64> communicationLogIds;
        QMap<quint64, logging::LogEntry> communicationLogsById;
        quint64 communicationLogCount = 0;
        bool communicationLogUnique = true;
        for (const auto &entry : sessionLog_.entries()) {
            if (entry.module == QStringLiteral("testing")
                && entry.event == QStringLiteral("request_attempt") && entry.requestId) {
                ++testLogCount;
                if (testLogIds.contains(*entry.requestId)) testLogUnique = false;
                testLogIds.insert(*entry.requestId);
            }
            if (entry.module == QStringLiteral("communication")
                && entry.event == QStringLiteral("request_completed") && entry.requestId
                && entry.metadata.value(QStringLiteral("owner")).toString()
                    == QStringLiteral("Testing")) {
                ++communicationLogCount;
                if (communicationLogIds.contains(*entry.requestId)) {
                    communicationLogUnique = false;
                }
                communicationLogIds.insert(*entry.requestId);
                communicationLogsById.insert(*entry.requestId, entry);
            }
        }

        bool retainedCrossLayerMatch = true;
        for (const auto &testCase : result.cases) {
            for (const auto &attempt : testCase.attempts) {
                const quint64 requestId = attempt.requestId.value;
                const auto diagnostic = diagnosticsById.constFind(requestId);
                const auto communicationLog = communicationLogsById.constFind(requestId);
                if (diagnostic == diagnosticsById.cend()
                    || communicationLog == communicationLogsById.cend()) {
                    retainedCrossLayerMatch = false;
                    continue;
                }
                const auto &evidence = attempt.requestResult.evidence;
                const auto &diagnosticEvidence = diagnostic->evidence;
                if (diagnostic->state != attempt.requestResult.state
                    || diagnosticEvidence.txAdu != evidence.txAdu
                    || diagnosticEvidence.rxAdu != evidence.rxAdu
                    || diagnosticEvidence.rtt != evidence.rtt
                    || communicationLog->tx != evidence.txAdu
                    || communicationLog->rx != evidence.rxAdu
                    || !evidence.rtt
                    || !communicationLog->metadata.contains(QStringLiteral("rtt_ns"))
                    || communicationLog->metadata.value(QStringLiteral("rtt_ns")).toLongLong()
                        != evidence.rtt->count()) {
                    retainedCrossLayerMatch = false;
                }
            }
        }

        QSet<quint64> retainedWithoutDiagnostic = retainedIds;
        retainedWithoutDiagnostic.subtract(diagnosticIds);
        const bool passed = expectedTotal > 0 && retainedUnique && retainedEvidence
            && retainedCrossLayerMatch && diagnosticUnique && testLogUnique
            && communicationLogUnique && diagnosticCount == expectedTotal
            && testLogCount == expectedTotal && communicationLogCount == expectedTotal
            && diagnosticIds == testLogIds && diagnosticIds == communicationLogIds
            && retainedWithoutDiagnostic.isEmpty();
        QTextStream(stdout) << "ATTEMPT_TOTAL=" << expectedTotal
                            << " RETAINED=" << retainedIds.size()
                            << " DIAGNOSTIC=" << diagnosticCount
                            << " COMM_LOG=" << communicationLogCount
                            << " TEST_LOG=" << testLogCount
                            << " EVIDENCE_AUDIT=" << (passed ? "PASS" : "FAIL")
                            << Qt::endl;
        return passed;
    }

    bool auditFull(const testing::TestSuiteResult &result)
    {
        if (result.status != testing::TestStatus::Pass || result.aborted
            || result.cases.size() != 20 || !result.auxiliaryErrors.isEmpty()) {
            return false;
        }
        for (const auto &item : result.cases) {
            if (item.status != testing::TestStatus::Pass || item.skipReason) return false;
        }
        const auto *sequence = caseResult(result, QStringLiteral("TC-P007"));
        const auto *recovery = caseResult(result, QStringLiteral("TC-R-AUTO-001"));
        const auto *stability = caseResult(result, QStringLiteral("TC-S001"));
        if (!sequence || sequence->steps.size() != 15
            || !recovery || recovery->steps.size() != 2 || recovery->attempts.size() != 2
            || !stability || !stability->stability) {
            return false;
        }
        for (const auto &step : sequence->steps) {
            if (step.status != testing::TestStatus::Pass) return false;
        }
        for (const auto &step : recovery->steps) {
            if (step.status != testing::TestStatus::Pass) return false;
        }
        if (recovery->attempts[0].requestId == recovery->attempts[1].requestId
            || recovery->attempts[1].requestResult.state
                != communication::RequestState::Succeeded) {
            return false;
        }
        for (const auto &id : {QStringLiteral("TC-F004"), QStringLiteral("TC-P002"),
                               QStringLiteral("TC-B003"), QStringLiteral("TC-B004")}) {
            const auto *written = caseResult(result, id);
            if (!written || written->attempts.size() != 4
                || written->attempts.back().purpose
                    != testing::TestStepPurpose::RestoreOriginal
                || written->attempts.back().requestResult.state
                    != communication::RequestState::Succeeded) {
                return false;
            }
        }
        const auto &stats = *stability->stability;
        const quint64 unsuccessful = stats.failures + stats.timeouts;
        const bool ratePassed = stats.total > 0
            && unsuccessful * testing::failureRateScalePpm
                <= stats.total * quint64(10000);
        return stability->duration >= std::chrono::milliseconds(600000)
            && stats.total == stats.successes + stats.failures + stats.timeouts
            && stats.successes >= 594 && stats.timeouts <= 6 && stats.failures <= 6
            && ratePassed && stats.validRttSamples == stats.total
            && stats.missingRttSamples == 0 && stats.minimumRttMs
            && stats.averageRttMs && stats.maximumRttMs;
    }

    bool auditPreflight(const testing::TestSuiteResult &result)
    {
        if (result.status != testing::TestStatus::Pass || result.aborted
            || result.cases.size() != 20 || !result.auxiliaryErrors.isEmpty()) {
            return false;
        }
        for (const auto &id : preflightCaseIds()) {
            const auto *selected = caseResult(result, id);
            if (!selected || selected->status != testing::TestStatus::Pass) return false;
        }
        for (const auto &item : result.cases) {
            if (!preflightCaseIds().contains(item.caseId)
                && item.status != testing::TestStatus::Skipped) {
                return false;
            }
        }
        return true;
    }

    bool auditAbort(const testing::TestSuiteResult &result)
    {
        const auto *stability = caseResult(result, QStringLiteral("TC-S001"));
        if (!abortArmed_ || !result.aborted || result.status != testing::TestStatus::Error
            || result.cases.size() != 20 || !result.auxiliaryErrors.isEmpty()
            || !stability) {
            return false;
        }
        for (const auto &item : result.cases) {
            if (item.caseId != QStringLiteral("TC-S001")
                && item.status != testing::TestStatus::Skipped) {
                return false;
            }
        }
        return stability->status == testing::TestStatus::Error
            && stability->error
            && stability->error->code == testing::TestErrorCode::Aborted;
    }

    void printResults(const testing::TestSuiteResult &result)
    {
        QTextStream out(stdout);
        for (const auto &item : result.cases) {
            out << "CASE=" << item.caseId
                << " STATUS=" << testing::testStatusName(item.status)
                << " DURATION_MS=" << item.duration.count()
                << " ATTEMPT_TOTAL=" << item.evidenceRetention.totalAttempts
                << " ATTEMPT_RETAINED=" << item.evidenceRetention.retainedAttempts
                << " EXPECTED="
                << (item.assertion ? item.assertion->expectedSummary : QStringLiteral("--"))
                << " ACTUAL=" << actualText(item) << Qt::endl;
            for (const auto &step : item.steps) {
                out << "COMPOSITE_STEP=" << step.stepId
                    << " INDEX=" << step.stepIndex
                    << " REPETITION=" << step.repetition
                    << " STATUS=" << testing::testStatusName(step.status)
                    << " DURATION_MS=" << step.duration.count()
                    << " EXPECTED="
                    << (step.assertion ? step.assertion->expectedSummary : QStringLiteral("--"))
                    << " ACTUAL="
                    << (step.assertion ? step.assertion->actualSummary : QStringLiteral("--"))
                    << " ATTEMPT_COUNT=" << step.attemptSequences.size() << Qt::endl;
            }
            for (const auto &attempt : item.attempts) {
                const auto &evidence = attempt.requestResult.evidence;
                out << "ATTEMPT=" << attempt.sequence
                    << " REQUEST_ID=" << attempt.requestId.value
                    << " STEP=" << testing::testStepPurposeName(attempt.purpose)
                    << " LOGICAL_STEP=" << attempt.logicalStepId
                    << " REPETITION=" << attempt.repetition
                    << " STATE=" << diagnostics::requestStateName(attempt.requestResult.state)
                    << " TX=" << diagnostics::byteArrayHex(evidence.txAdu)
                    << " RX=" << diagnostics::byteArrayHex(evidence.rxAdu)
                    << " RTT_NS="
                    << (evidence.rtt ? QString::number(evidence.rtt->count())
                                     : QStringLiteral("--"))
                    << " DIAGNOSTIC="
                    << (attempt.requestResult.error
                            ? attempt.requestResult.error->diagnostic
                            : QStringLiteral("--"))
                    << Qt::endl;
            }
            if (item.stability) {
                const auto &stats = *item.stability;
                out << "STABILITY_TOTAL=" << stats.total
                    << " SUCCESS=" << stats.successes
                    << " FAILURE=" << stats.failures
                    << " TIMEOUT=" << stats.timeouts
                    << " RTT_VALID=" << stats.validRttSamples
                    << " RTT_MISSING=" << stats.missingRttSamples
                    << " RTT_MIN_MS=" << (stats.minimumRttMs ? QString::number(*stats.minimumRttMs) : "--")
                    << " RTT_AVG_MS=" << (stats.averageRttMs ? QString::number(*stats.averageRttMs) : "--")
                    << " RTT_MAX_MS=" << (stats.maximumRttMs ? QString::number(*stats.maximumRttMs) : "--")
                    << Qt::endl;
            }
        }
    }

    void handleWorkflowFinished()
    {
        if (stage_ != Stage::Running) return;
        abortTimer_.stop();
        const auto snapshot = results_.snapshot();
        if (!snapshot) {
            fail(QStringLiteral("工作流结束但没有不可变结果快照"));
            return;
        }
        validationMetadata_.insert(QStringLiteral("suite_status"),
                                   testing::testStatusName(snapshot->status));
        validationMetadata_.insert(QStringLiteral("suite_aborted"), snapshot->aborted);
        validationMetadata_.insert(QStringLiteral("suite_duration_ms"),
                                   static_cast<qlonglong>(snapshot->duration.count()));
        validationMetadata_.insert(QStringLiteral("case_count"), snapshot->cases.size());
        quint64 attemptTotal = 0;
        int passedCases = 0;
        for (const auto &testCase : snapshot->cases) {
            attemptTotal += testCase.evidenceRetention.totalAttempts;
            if (testCase.status == testing::TestStatus::Pass) ++passedCases;
        }
        validationMetadata_.insert(QStringLiteral("passed_case_count"), passedCases);
        validationMetadata_.insert(QStringLiteral("attempt_total"),
                                   static_cast<qulonglong>(attemptTotal));
        if (const auto *stability = caseResult(*snapshot, QStringLiteral("TC-S001"));
            stability && stability->stability) {
            const auto &stats = *stability->stability;
            validationMetadata_.insert(QStringLiteral("stability_status"),
                                       testing::testStatusName(stability->status));
            validationMetadata_.insert(QStringLiteral("stability_duration_ms"),
                                       static_cast<qlonglong>(stability->duration.count()));
            validationMetadata_.insert(QStringLiteral("stability_total"),
                                       static_cast<qulonglong>(stats.total));
            validationMetadata_.insert(QStringLiteral("stability_success"),
                                       static_cast<qulonglong>(stats.successes));
            validationMetadata_.insert(QStringLiteral("stability_failure"),
                                       static_cast<qulonglong>(stats.failures));
            validationMetadata_.insert(QStringLiteral("stability_timeout"),
                                       static_cast<qulonglong>(stats.timeouts));
            validationMetadata_.insert(QStringLiteral("stability_valid_rtt"),
                                       static_cast<qulonglong>(stats.validRttSamples));
            validationMetadata_.insert(QStringLiteral("stability_missing_rtt"),
                                       static_cast<qulonglong>(stats.missingRttSamples));
            if (stats.minimumRttMs) {
                validationMetadata_.insert(QStringLiteral("stability_rtt_min_ms"),
                                           *stats.minimumRttMs);
            }
            if (stats.averageRttMs) {
                validationMetadata_.insert(QStringLiteral("stability_rtt_avg_ms"),
                                           *stats.averageRttMs);
            }
            if (stats.maximumRttMs) {
                validationMetadata_.insert(QStringLiteral("stability_rtt_max_ms"),
                                           *stats.maximumRttMs);
            }
        }
        printResults(*snapshot);
        evidencePassed_ = auditEvidence(*snapshot);
        if (mode_ == ValidationMode::Full) modePassed_ = auditFull(*snapshot);
        else if (mode_ == ValidationMode::Preflight) modePassed_ = auditPreflight(*snapshot);
        else modePassed_ = auditAbort(*snapshot);
        uiPassed_ = mode_ == ValidationMode::Preflight
            || (runHeartbeatTicks_ > 0 && maximumHeartbeatLateMs_ <= 1000);
        QTextStream(stdout) << "MODE_AUDIT=" << (modePassed_ ? "PASS" : "FAIL")
                            << " UI_HEARTBEAT=" << (uiPassed_ ? "PASS" : "FAIL")
                            << " UI_TICKS=" << runHeartbeatTicks_
                            << " UI_MAX_LATE_MS=" << maximumHeartbeatLateMs_ << Qt::endl;
        if (!modePassed_ || !evidencePassed_ || !uiPassed_
            || !automation_.lastError().isEmpty()) {
            failed_ = true;
            QTextStream(stderr) << "ERROR=结果、证据或 UI 心跳核对失败："
                                << automation_.lastError() << Qt::endl;
        }
        stage_ = Stage::ReadingFinal;
        if (!configuration_.readThresholds().accepted()) {
            fail(QStringLiteral("最终阈值读取未被接受"));
        }
    }

    void disconnectDevice()
    {
        if (stage_ == Stage::Disconnecting || stage_ == Stage::Finished) return;
        if (appState_.state() == app::AppState::Disconnected) {
            finish();
            return;
        }
        stage_ = Stage::Disconnecting;
        if (!appState_.disconnectDevice().accepted()) {
            fail(QStringLiteral("断开连接命令未被接受"));
        }
    }

    void fail(const QString &diagnostic)
    {
        if (stage_ == Stage::Finished) return;
        failed_ = true;
        if (failureReason_.isEmpty()) failureReason_ = diagnostic;
        QTextStream(stderr) << "ERROR=" << diagnostic << Qt::endl;
        if (automation_.state() == testing::TestAutomationState::Running) {
            (void)automation_.abort();
            return;
        }
        if (configuration_.busy()) {
            (void)configuration_.cancel();
            return;
        }
        if (appState_.state() == app::AppState::ConnectedIdle) {
            disconnectDevice();
            return;
        }
        if (appState_.state() == app::AppState::Disconnected) finish();
    }

    void finish()
    {
        if (stage_ == Stage::Finished) return;
        stage_ = Stage::Finished;
        watchdog_.stop();
        heartbeat_.stop();
        abortTimer_.stop();
        const bool statePassed = client_.activeOwner() == communication::CommunicationOwner::None
            && appState_.state() == app::AppState::Disconnected;
        const bool beforeLogEnd = !failed_ && modePassed_ && evidencePassed_
            && thresholdPassed_ && uiPassed_ && statePassed;
        if (sessionLog_.active()) {
            logging::LogEntry entry{
                QDateTime::currentDateTimeUtc(),
                beforeLogEnd ? logging::LogLevel::Test : logging::LogLevel::Error,
                QStringLiteral("validation"),
                beforeLogEnd ? QStringLiteral("TASK-020 RS485 验收模式通过")
                             : QStringLiteral("TASK-020 RS485 验收模式失败")};
            entry.event = QStringLiteral("validation_complete");
            entry.metadata.insert(QStringLiteral("mode"), modeName(mode_));
            entry.metadata.insert(QStringLiteral("mode_passed"), modePassed_);
            entry.metadata.insert(QStringLiteral("evidence_passed"), evidencePassed_);
            entry.metadata.insert(QStringLiteral("threshold_passed"), thresholdPassed_);
            entry.metadata.insert(QStringLiteral("ui_passed"), uiPassed_);
            entry.metadata.insert(QStringLiteral("ui_heartbeat_ticks"),
                                  static_cast<qulonglong>(runHeartbeatTicks_));
            entry.metadata.insert(QStringLiteral("ui_max_late_ms"),
                                  maximumHeartbeatLateMs_);
            if (baseline_) {
                entry.metadata.insert(QStringLiteral("threshold_before"),
                                      thresholdText(*baseline_));
            }
            if (finalThresholds_) {
                entry.metadata.insert(QStringLiteral("threshold_final"),
                                      thresholdText(*finalThresholds_));
            }
            if (!failureReason_.isEmpty()) {
                entry.metadata.insert(QStringLiteral("first_error"), failureReason_);
            }
            for (auto iterator = validationMetadata_.cbegin();
                 iterator != validationMetadata_.cend(); ++iterator) {
                entry.metadata.insert(iterator.key(), iterator.value());
            }
            sessionLog_.append(std::move(entry));
            const auto ended = sessionLog_.endSession();
            logEnded_ = ended.succeeded;
        }
        const bool passed = beforeLogEnd && logEnded_;
        QFile logFile(logPath_);
        qint64 lineCount = 0;
        if (logFile.open(QIODevice::ReadOnly)) {
            while (!logFile.atEnd()) {
                (void)logFile.readLine();
                ++lineCount;
            }
        }
        const QFileInfo logInfo(logPath_);
        const QString logSha = sha256(logPath_);
        QTextStream(stdout) << "MODE_RESULT=" << (modePassed_ ? "PASS" : "FAIL") << Qt::endl
                            << "EVIDENCE=" << (evidencePassed_ ? "PASS" : "FAIL") << Qt::endl
                            << "THRESHOLD_RESTORE=" << (thresholdPassed_ ? "PASS" : "FAIL") << Qt::endl
                            << "FINAL_STATE=" << (statePassed ? "PASS" : "FAIL") << Qt::endl
                            << "LOG=" << logPath_ << Qt::endl
                            << "LOG_BYTES=" << logInfo.size() << Qt::endl
                            << "LOG_LINES=" << lineCount << Qt::endl
                            << "LOG_SHA256=" << logSha << Qt::endl
                            << "RESULT=" << (passed ? "PASS" : "FAIL") << Qt::endl;
        if (!failureReason_.isEmpty()) {
            QTextStream(stderr) << "FIRST_ERROR=" << failureReason_ << Qt::endl;
        }
        QCoreApplication::exit(passed ? 0 : 2);
    }

    QString portName_;
    QString suitePath_;
    QString sourceRevision_;
    ValidationMode mode_ = ValidationMode::Preflight;
    int timeoutMs_ = 500;
    int abortAfterMs_ = 2000;
    communication::QSerialPortModbusClient client_;
    monitor::QtMonitorScheduler monitorScheduler_;
    monitor::MonitorService monitorService_;
    app::AppStateController appState_;
    ui::QtSerialPortCatalog ports_;
    ui::MonitoringViewModel monitoring_;
    configuration::ConfigurationService configuration_;
    diagnostics::CommunicationDiagnosticsModel diagnostics_;
    logging::SessionLogService sessionLog_;
    testing::TestResultManager results_;
    testing::TestEngine engine_;
    testing::TestAutomationController automation_;
    MainWindow window_;
    QTimer heartbeat_;
    QTimer watchdog_;
    QTimer abortTimer_;
    QElapsedTimer elapsed_;
    Stage stage_ = Stage::Starting;
    QString suiteSha256_;
    QString binarySha256_;
    QString logPath_;
    QString failureReason_;
    std::optional<device::AlarmThresholds> baseline_;
    std::optional<device::AlarmThresholds> finalThresholds_;
    QVariantMap validationMetadata_;
    qint64 lastHeartbeatMs_ = 0;
    qint64 maximumHeartbeatLateMs_ = 0;
    quint64 runHeartbeatTicks_ = 0;
    bool abortArmed_ = false;
    bool failed_ = false;
    bool modePassed_ = false;
    bool evidencePassed_ = false;
    bool thresholdPassed_ = false;
    bool uiPassed_ = false;
    bool logEnded_ = false;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName(QStringLiteral("TASK-020 RS485 Validation"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Phase 6 正式 RS485 套件预检、中止专项与完整验收"));
    parser.addHelpOption();
    QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")},
                                  QStringLiteral("运行时枚举确认的 USB-RS485 串口"),
                                  QStringLiteral("port"));
    QCommandLineOption suiteOption(QStringLiteral("suite"),
                                   QStringLiteral("Phase 6 正式 JSON 主套件"),
                                   QStringLiteral("path"));
    QCommandLineOption revisionOption(QStringLiteral("source-revision"),
                                      QStringLiteral("可追溯源码修订标识"),
                                      QStringLiteral("revision"));
    QCommandLineOption modeOption(QStringLiteral("mode"),
                                  QStringLiteral("preflight、abort 或 full"),
                                  QStringLiteral("mode"));
    QCommandLineOption timeoutOption(QStringLiteral("timeout-ms"),
                                     QStringLiteral("响应超时，毫秒"),
                                     QStringLiteral("ms"), QStringLiteral("500"));
    QCommandLineOption abortOption(QStringLiteral("abort-after-ms"),
                                   QStringLiteral("abort 模式在首请求开始后的中止延迟"),
                                   QStringLiteral("ms"), QStringLiteral("2000"));
    parser.addOption(portOption);
    parser.addOption(suiteOption);
    parser.addOption(revisionOption);
    parser.addOption(modeOption);
    parser.addOption(timeoutOption);
    parser.addOption(abortOption);
    parser.process(application);
    if (!parser.isSet(portOption) || !parser.isSet(suiteOption)
        || !parser.isSet(revisionOption) || !parser.isSet(modeOption)) {
        QTextStream(stderr) << "必须指定 --port、--suite、--source-revision 和 --mode"
                            << Qt::endl;
        return 1;
    }
    if (parser.value(portOption).trimmed().isEmpty()
        || parser.value(suiteOption).trimmed().isEmpty()
        || parser.value(revisionOption).trimmed().isEmpty()) {
        QTextStream(stderr) << "--port、--suite 和 --source-revision 不得为空"
                            << Qt::endl;
        return 1;
    }
    const auto mode = parseMode(parser.value(modeOption));
    if (!mode) {
        QTextStream(stderr) << "--mode 必须为 preflight、abort 或 full" << Qt::endl;
        return 1;
    }
    bool timeoutOk = false;
    bool abortOk = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    const int abortAfterMs = parser.value(abortOption).toInt(&abortOk);
    if (!timeoutOk || timeoutMs < 1 || timeoutMs > 60000) {
        QTextStream(stderr) << "--timeout-ms 必须为 1..60000" << Qt::endl;
        return 1;
    }
    if (!abortOk || abortAfterMs < 100 || abortAfterMs > 30000) {
        QTextStream(stderr) << "--abort-after-ms 必须为 100..30000" << Qt::endl;
        return 1;
    }
    if (!QFileInfo::exists(parser.value(suiteOption))) {
        QTextStream(stderr) << "--suite 文件不存在" << Qt::endl;
        return 1;
    }
    ValidationRunner runner(parser.value(portOption), parser.value(suiteOption),
                            parser.value(revisionOption), *mode, timeoutMs, abortAfterMs);
    QTimer::singleShot(0, &runner, [&runner] { runner.start(); });
    return application.exec();
}
