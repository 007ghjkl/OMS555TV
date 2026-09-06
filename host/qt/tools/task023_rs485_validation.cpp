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
#include <QSet>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <optional>

using namespace oms555tv;

namespace {

enum class ValidationMode { Preflight, Full };

QString modeName(ValidationMode mode)
{
    return mode == ValidationMode::Preflight
        ? QStringLiteral("preflight") : QStringLiteral("full");
}

std::optional<ValidationMode> parseMode(const QString &value)
{
    if (value == QStringLiteral("preflight")) return ValidationMode::Preflight;
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

const testing::TestCaseResult *caseResult(const testing::TestSuiteResult &result,
                                          const QString &caseId)
{
    const auto iterator = std::find_if(
        result.cases.cbegin(), result.cases.cend(), [&caseId](const auto &item) {
            return item.caseId == caseId;
        });
    return iterator == result.cases.cend() ? nullptr : &*iterator;
}

std::optional<quint16> singleReadValue(
    const communication::ModbusRequestResult &result)
{
    if (!result.success) return std::nullopt;
    const auto *read = std::get_if<communication::ReadHoldingRegistersResult>(
        &*result.success);
    if (!read || read->values.size() != 1) return std::nullopt;
    return read->values.front();
}

class ValidationRunner final : public QObject
{
public:
    ValidationRunner(QString portName, QString suitePath, QString sourceRevision,
                     ValidationMode mode, int timeoutMs, QObject *parent = nullptr)
        : QObject(parent)
        , portName_(std::move(portName))
        , suitePath_(QFileInfo(std::move(suitePath)).absoluteFilePath())
        , sourceRevision_(std::move(sourceRevision))
        , mode_(mode)
        , timeoutMs_(timeoutMs)
        , monitorService_(client_, scheduler_)
        , appState_(client_, monitorService_)
        , monitoring_(appState_, monitorService_, ports_)
        , configuration_(appState_, client_)
        , diagnostics_(client_, 256)
        , sessionLog_(diagnostics_, {}, 512)
        , engine_(appState_, client_, results_, &sessionLog_, scheduler_)
        , automation_(appState_, engine_, results_)
        , window_(monitoring_, configuration_, diagnostics_, sessionLog_, automation_)
    {
        window_.setWindowTitle(QStringLiteral("TASK-023 Phase 7 RS485 半自动验收 — %1")
                                   .arg(modeName(mode_)));
        connect(&automation_, &testing::TestAutomationController::suiteChanged,
                this, [this] { connectAfterSuiteLoad(); });
        connect(&automation_, &testing::TestAutomationController::workflowFinished,
                this, [this] { handleWorkflowFinished(); });
        connect(&appState_, &app::AppStateController::commandCompleted,
                this, [this](const app::AppCommandResult &result) {
            handleAppCommand(result);
        });
        connect(&monitorService_, &monitor::MonitorService::snapshotPublished,
                this, [this](const monitor::MonitoringSnapshot &snapshot) {
            handleSnapshot(snapshot);
        });

        heartbeat_.setInterval(100);
        heartbeat_.setTimerType(Qt::PreciseTimer);
        connect(&heartbeat_, &QTimer::timeout, this, [this] { recordHeartbeat(); });
        watchdog_.setSingleShot(true);
        connect(&watchdog_, &QTimer::timeout, this, [this] {
            fail(QStringLiteral("验收流程超过 %1 ms watchdog").arg(watchdog_.interval()));
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
            {QStringLiteral("task"), QStringLiteral("TASK-023")},
            {QStringLiteral("mode"), modeName(mode_)},
            {QStringLiteral("source_revision"), sourceRevision_},
            {QStringLiteral("host_version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("host_binary_sha256"), binarySha256_},
            {QStringLiteral("firmware_expected"), QStringLiteral("0.2")},
            {QStringLiteral("hardware"),
             QStringLiteral("NUCLEO-F411RE / USART1 / MAX13487EESA / DTECH USB-RS485 / 20cm")},
            {QStringLiteral("manual_scope"), QStringLiteral("only RS485 A/B")},
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
        sessionId_ = log.sessionId;
        logPath_ = log.filePath;
        QTextStream(stdout) << "MODE=" << modeName(mode_) << Qt::endl
                            << "SOURCE_REVISION=" << sourceRevision_ << Qt::endl
                            << "SUITE_PATH=" << suitePath_ << Qt::endl
                            << "SUITE_SHA256=" << suiteSha256_ << Qt::endl
                            << "HOST_BINARY_SHA256=" << binarySha256_ << Qt::endl
                            << "SESSION_ID=" << sessionId_ << Qt::endl
                            << "SESSION_LOG=" << logPath_ << Qt::endl;

        watchdog_.setInterval(mode_ == ValidationMode::Full ? 240000 : 30000);
        watchdog_.start();
        stage_ = Stage::Loading;
        if (!automation_.loadSuiteFile(suitePath_)) {
            fail(QStringLiteral("套件加载命令被拒绝"));
        }
    }

private:
    enum class Stage {
        Starting,
        Loading,
        Connecting,
        StartingPreflight,
        PreflightMonitoring,
        StoppingPreflight,
        Running,
        StartingFinalRead,
        FinalMonitoring,
        StoppingFinalRead,
        Disconnecting,
        Finished,
    };

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

    bool validateSuite(const testing::TestSuite &suite) const
    {
        if (suite.schemaVersion != testing::testSuiteSchemaVersionV3
            || suite.id != QStringLiteral("phase7-rs485-disconnect-recovery")
            || suite.cases.size() != 1) return false;
        const auto &testCase = suite.cases.front();
        if (testCase.id != QStringLiteral("TC-R001") || !testCase.enabled
            || testCase.environment != testing::ExecutionEnvironment::RealRs485
            || testCase.type != testing::TestCaseType::GuidedRecovery
            || testCase.guidedRecovery.steps.size() != testing::guidedRecoveryStepCount) {
            return false;
        }
        const auto &steps = testCase.guidedRecovery.steps;
        if (steps[0].type != testing::GuidedStepType::OperatorPrompt
            || steps[0].operatorStep->purpose
                != testing::GuidedPromptPurpose::DisconnectRs485
            || steps[1].type != testing::GuidedStepType::ObserveOutage
            || steps[2].type != testing::GuidedStepType::OperatorPrompt
            || steps[2].operatorStep->purpose
                != testing::GuidedPromptPurpose::ReconnectRs485
            || steps[3].type != testing::GuidedStepType::ObserveRecovery) return false;
        for (const int index : {1, 3}) {
            const auto &observation = *steps[index].observationStep;
            if (observation.probe.function
                    != testing::ModbusFunction::ReadHoldingRegisters
                || observation.probe.address.value() != 40
                || observation.probe.count != 1
                || observation.consecutiveMatches != 3) return false;
        }
        const auto &recovery = *steps[3].observationStep;
        return recovery.deadline < std::chrono::seconds(5)
            && recovery.businessAssertion
            && recovery.businessAssertion->type == testing::AssertionType::Equals
            && recovery.businessAssertion->value == 2;
    }

    void connectAfterSuiteLoad()
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
            fail(QStringLiteral("套件不符合 TASK-023 正式目录和安全门禁"));
            return;
        }
        QTextStream(stdout) << "SUITE=" << automation_.suite()->id
                            << " CASES=1 CATALOG=PASS" << Qt::endl;
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
        if (!result.succeeded) {
            fail(QStringLiteral("应用命令 %1 失败").arg(static_cast<int>(result.command)));
            return;
        }
        switch (result.command) {
        case app::AppCommand::Connect:
            startMonitoring(false);
            break;
        case app::AppCommand::StartMonitoring:
            if (stage_ == Stage::StartingPreflight) stage_ = Stage::PreflightMonitoring;
            else if (stage_ == Stage::StartingFinalRead) stage_ = Stage::FinalMonitoring;
            break;
        case app::AppCommand::StopMonitoring:
            if (stage_ == Stage::StoppingPreflight) {
                if (mode_ == ValidationMode::Preflight) disconnectDevice();
                else startGuidedRun();
            } else if (stage_ == Stage::StoppingFinalRead) {
                disconnectDevice();
            }
            break;
        case app::AppCommand::Disconnect:
            finish();
            break;
        case app::AppCommand::StartTesting:
        case app::AppCommand::StopTesting:
        case app::AppCommand::Recover:
            break;
        }
    }

    void startMonitoring(bool finalRead)
    {
        stage_ = finalRead ? Stage::StartingFinalRead : Stage::StartingPreflight;
        monitor::MonitorConfig config;
        config.targetPeriod = std::chrono::milliseconds(1000);
        config.requestTimeout = std::chrono::milliseconds(timeoutMs_);
        if (!appState_.startMonitoring(config).accepted()) {
            fail(QStringLiteral("%1监控读取命令未被接受")
                     .arg(finalRead ? QStringLiteral("最终") : QStringLiteral("预检")));
        }
    }

    void handleSnapshot(const monitor::MonitoringSnapshot &snapshot)
    {
        if (stage_ != Stage::PreflightMonitoring && stage_ != Stage::FinalMonitoring) return;
        const bool identity = snapshot.snapshot.firmwareVersion.major == 0
            && snapshot.snapshot.firmwareVersion.minor == 2
            && snapshot.requestIds.size() == device::deviceSnapshotReadBlocks.size();
        if (!identity) {
            fail(QStringLiteral("监控快照未确认 Firmware 0.2 或五个完整读块"));
            return;
        }
        const bool finalRead = stage_ == Stage::FinalMonitoring;
        if (finalRead) finalSnapshot_ = snapshot;
        else preflightSnapshot_ = snapshot;
        QTextStream(stdout) << (finalRead ? "FINAL_ONLINE" : "PREFLIGHT_ONLINE")
                            << "=PASS FIRMWARE=0.2 REQUESTS="
                            << snapshot.requestIds.size() << Qt::endl;
        stage_ = finalRead ? Stage::StoppingFinalRead : Stage::StoppingPreflight;
        if (!appState_.stopMonitoring().accepted()) {
            fail(QStringLiteral("停止%1监控命令未被接受")
                     .arg(finalRead ? QStringLiteral("最终") : QStringLiteral("预检")));
        }
    }

    void startGuidedRun()
    {
        stage_ = Stage::Running;
        runHeartbeatTicks_ = 0;
        maximumHeartbeatLateMs_ = 0;
        lastHeartbeatMs_ = elapsed_.elapsed();
        if (auto *tabs = window_.findChild<QTabWidget *>()) tabs->setCurrentIndex(3);
        monitor::MonitorConfig config;
        config.requestTimeout = std::chrono::milliseconds(timeoutMs_);
        if (!automation_.runAll(config)) {
            fail(QStringLiteral("引导式运行未被接受：%1").arg(automation_.lastError()));
            return;
        }
        QTextStream(stdout)
            << "MANUAL_STEP=等待窗口提示；只断开 RS485 A/B 后点击确认。"
            << Qt::endl;
    }

    void handleWorkflowFinished()
    {
        if (stage_ != Stage::Running) return;
        const auto result = automation_.resultSnapshot();
        if (!result) {
            fail(QStringLiteral("引导式运行结束但没有结果快照"));
            return;
        }
        guidedPassed_ = auditGuided(*result);
        evidencePassed_ = auditEvidence(*result);
        uiPassed_ = runHeartbeatTicks_ > 0 && maximumHeartbeatLateMs_ < 1000;
        if (!guidedPassed_ || !evidencePassed_ || !uiPassed_) {
            failed_ = true;
            if (failureReason_.isEmpty()) {
                failureReason_ = QStringLiteral("正式引导结果、证据或 UI 心跳审计失败");
            }
        }
        if (appState_.state() != app::AppState::ConnectedIdle
            || client_.activeOwner() != communication::CommunicationOwner::None) {
            fail(QStringLiteral("引导结束后未返回 CONNECTED_IDLE 或 owner 未释放"));
            return;
        }
        startMonitoring(true);
    }

    bool auditGuided(const testing::TestSuiteResult &result)
    {
        const auto *item = caseResult(result, QStringLiteral("TC-R001"));
        if (!item || result.status != testing::TestStatus::Pass || result.aborted
            || !result.auxiliaryErrors.isEmpty() || result.sessionId != sessionId_
            || item->status != testing::TestStatus::Pass || !item->guidedRecovery
            || item->sessionId != sessionId_) return false;
        const auto &guided = *item->guidedRecovery;
        if (guided.finalState != testing::GuidedRunState::Finished
            || guided.terminalReason != testing::GuidedTerminalReason::Pass
            || !guided.physicalLinkRestored || guided.recoveryInstructionRequired
            || !guided.recoveryInstruction.isEmpty() || !guided.recoveryTiming
            || guided.recoveryTiming->toStableRecovery >= std::chrono::seconds(5)
            || guided.operatorActions.size() != 2 || guided.observations.size() != 2) {
            return false;
        }
        for (const auto &action : guided.operatorActions) {
            if (!action.action || *action.action != testing::GuidedOperatorAction::Confirm
                || action.oneTimeToken.isEmpty() || !action.promptShownUtc.isValid()
                || !action.actionUtc || !action.actionUtc->isValid()
                || action.waitDuration < std::chrono::milliseconds::zero()) return false;
        }
        const auto &outage = guided.observations[0];
        const auto &recovery = guided.observations[1];
        if (outage.target
                != testing::GuidedObservationTarget::ConsecutiveResponseTimeouts
            || recovery.target
                != testing::GuidedObservationTarget::ConsecutiveValidResponses
            || outage.outcome != testing::GuidedObservationOutcome::Matched
            || recovery.outcome != testing::GuidedObservationOutcome::Matched
            || outage.requiredConsecutiveMatches != 3
            || recovery.requiredConsecutiveMatches != 3
            || outage.achievedConsecutiveMatches < 3
            || recovery.achievedConsecutiveMatches < 3
            || outage.probes.size() < 3 || recovery.probes.size() < 3) return false;

        for (const auto &attempt : outage.probes) {
            const auto &request = attempt.requestResult;
            if (request.success || !request.error
                || request.error->code != communication::ErrorCode::ResponseTimeout
                || request.evidence.txAdu.isEmpty() || !request.evidence.rtt) return false;
        }
        for (const auto &attempt : recovery.probes) {
            const auto &request = attempt.requestResult;
            if (request.state != communication::RequestState::Succeeded
                || singleReadValue(request) != std::optional<quint16>(2)
                || request.evidence.txAdu.isEmpty() || request.evidence.rxAdu.isEmpty()
                || request.evidence.txCrcStatus != communication::CrcStatus::Valid
                || request.evidence.rxCrcStatus != communication::CrcStatus::Valid
                || !request.evidence.rtt) return false;
        }
        stableRecoveryMs_ = guided.recoveryTiming->toStableRecovery.count();
        firstRecoveryMs_ = guided.recoveryTiming->toFirstSuccessfulResponse.count();
        outageProbeCount_ = outage.probes.size();
        recoveryProbeCount_ = recovery.probes.size();
        QTextStream(stdout) << "GUIDED_RESULT=PASS OUTAGE_PROBES=" << outageProbeCount_
                            << " RECOVERY_PROBES=" << recoveryProbeCount_
                            << " FIRST_RECOVERY_MS=" << firstRecoveryMs_
                            << " STABLE_RECOVERY_MS=" << stableRecoveryMs_ << Qt::endl;
        return true;
    }

    bool auditEvidence(const testing::TestSuiteResult &result)
    {
        const auto *item = caseResult(result, QStringLiteral("TC-R001"));
        if (!item) return false;
        QSet<quint64> resultIds;
        for (const auto &attempt : item->attempts) {
            if (attempt.requestId.value == 0 || resultIds.contains(attempt.requestId.value)) {
                return false;
            }
            resultIds.insert(attempt.requestId.value);
        }
        QSet<quint64> diagnosticIds;
        for (const auto &record : diagnostics_.records()) {
            if (record.result.evidence.owner == communication::CommunicationOwner::Testing) {
                diagnosticIds.insert(record.result.requestId.value);
            }
        }
        QSet<quint64> communicationLogIds;
        QSet<quint64> testLogIds;
        QSet<QString> guidedEvents;
        for (const auto &entry : sessionLog_.entries()) {
            if (entry.module == QStringLiteral("communication")
                && entry.event == QStringLiteral("request_completed") && entry.requestId
                && entry.metadata.value(QStringLiteral("owner")).toString()
                    == QStringLiteral("Testing")) {
                communicationLogIds.insert(*entry.requestId);
            }
            if (entry.module == QStringLiteral("testing")) {
                guidedEvents.insert(entry.event);
                if (entry.event == QStringLiteral("request_attempt") && entry.requestId) {
                    testLogIds.insert(*entry.requestId);
                }
            }
        }
        const QSet<QString> requiredEvents{
            QStringLiteral("prompt_shown"), QStringLiteral("operator_confirmed"),
            QStringLiteral("observation_started"), QStringLiteral("observation_finished"),
            QStringLiteral("guided_case_finished")};
        QSet<QString> missingEvents = requiredEvents;
        missingEvents.subtract(guidedEvents);
        const bool passed = !resultIds.isEmpty() && resultIds == diagnosticIds
            && resultIds == communicationLogIds && resultIds == testLogIds
            && missingEvents.isEmpty();
        QTextStream(stdout) << "REQUEST_IDS=" << resultIds.size()
                            << " DIAGNOSTIC=" << diagnosticIds.size()
                            << " COMM_LOG=" << communicationLogIds.size()
                            << " TEST_LOG=" << testLogIds.size()
                            << " EVIDENCE_AUDIT=" << (passed ? "PASS" : "FAIL")
                            << Qt::endl;
        return passed;
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

    void disconnectDevice()
    {
        if (appState_.state() == app::AppState::Disconnected) {
            finish();
            return;
        }
        if (appState_.state() != app::AppState::ConnectedIdle
            || client_.activeOwner() != communication::CommunicationOwner::None) {
            fail(QStringLiteral("清理前应用非 CONNECTED_IDLE 或 owner 未释放"));
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
        if (appState_.state() == app::AppState::Monitoring) {
            if (stage_ == Stage::FinalMonitoring || stage_ == Stage::StartingFinalRead) {
                stage_ = Stage::StoppingFinalRead;
            } else {
                stage_ = Stage::StoppingPreflight;
            }
            (void)appState_.stopMonitoring();
            return;
        }
        if (appState_.state() == app::AppState::ConnectedIdle
            && client_.activeOwner() == communication::CommunicationOwner::None) {
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
        const bool statePassed = client_.activeOwner() == communication::CommunicationOwner::None
            && appState_.state() == app::AppState::Disconnected;
        const bool preflightPassed = preflightSnapshot_.has_value();
        const bool modePassed = mode_ == ValidationMode::Preflight
            ? preflightPassed
            : preflightPassed && guidedPassed_ && evidencePassed_ && uiPassed_
                && finalSnapshot_.has_value();
        const bool beforeLogEnd = !failed_ && modePassed && statePassed;
        if (sessionLog_.active()) {
            logging::LogEntry entry{
                QDateTime::currentDateTimeUtc(),
                beforeLogEnd ? logging::LogLevel::Test : logging::LogLevel::Error,
                QStringLiteral("validation"),
                beforeLogEnd ? QStringLiteral("TASK-023 RS485 验收模式通过")
                             : QStringLiteral("TASK-023 RS485 验收模式失败")};
            entry.event = QStringLiteral("validation_complete");
            entry.metadata.insert(QStringLiteral("mode"), modeName(mode_));
            entry.metadata.insert(QStringLiteral("preflight_passed"), preflightPassed);
            entry.metadata.insert(QStringLiteral("guided_passed"), guidedPassed_);
            entry.metadata.insert(QStringLiteral("evidence_passed"), evidencePassed_);
            entry.metadata.insert(QStringLiteral("final_online_passed"),
                                  finalSnapshot_.has_value());
            entry.metadata.insert(QStringLiteral("ui_passed"), uiPassed_);
            entry.metadata.insert(QStringLiteral("ui_heartbeat_ticks"),
                                  static_cast<qulonglong>(runHeartbeatTicks_));
            entry.metadata.insert(QStringLiteral("ui_max_late_ms"),
                                  maximumHeartbeatLateMs_);
            entry.metadata.insert(QStringLiteral("outage_probes"), outageProbeCount_);
            entry.metadata.insert(QStringLiteral("recovery_probes"), recoveryProbeCount_);
            entry.metadata.insert(QStringLiteral("first_recovery_ms"), firstRecoveryMs_);
            entry.metadata.insert(QStringLiteral("stable_recovery_ms"), stableRecoveryMs_);
            if (!failureReason_.isEmpty()) {
                entry.metadata.insert(QStringLiteral("first_error"), failureReason_);
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
        QTextStream(stdout) << "MODE_RESULT=" << (modePassed ? "PASS" : "FAIL") << Qt::endl
                            << "PREFLIGHT=" << (preflightPassed ? "PASS" : "FAIL") << Qt::endl
                            << "GUIDED=" << (guidedPassed_ ? "PASS" : "N/A_OR_FAIL") << Qt::endl
                            << "EVIDENCE=" << (evidencePassed_ ? "PASS" : "N/A_OR_FAIL") << Qt::endl
                            << "FINAL_ONLINE=" << (finalSnapshot_ ? "PASS" : "N/A_OR_FAIL") << Qt::endl
                            << "FINAL_STATE=" << (statePassed ? "PASS" : "FAIL") << Qt::endl
                            << "LOG=" << logPath_ << Qt::endl
                            << "LOG_BYTES=" << logInfo.size() << Qt::endl
                            << "LOG_LINES=" << lineCount << Qt::endl
                            << "LOG_SHA256=" << sha256(logPath_) << Qt::endl
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
    communication::QSerialPortModbusClient client_;
    monitor::QtMonitorScheduler scheduler_;
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
    QElapsedTimer elapsed_;
    Stage stage_ = Stage::Starting;
    QString suiteSha256_;
    QString binarySha256_;
    QString sessionId_;
    QString logPath_;
    QString failureReason_;
    std::optional<monitor::MonitoringSnapshot> preflightSnapshot_;
    std::optional<monitor::MonitoringSnapshot> finalSnapshot_;
    qint64 lastHeartbeatMs_ = 0;
    qint64 maximumHeartbeatLateMs_ = 0;
    quint64 runHeartbeatTicks_ = 0;
    qint64 firstRecoveryMs_ = -1;
    qint64 stableRecoveryMs_ = -1;
    int outageProbeCount_ = 0;
    int recoveryProbeCount_ = 0;
    bool failed_ = false;
    bool guidedPassed_ = false;
    bool evidencePassed_ = false;
    bool uiPassed_ = false;
    bool logEnded_ = false;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName(QStringLiteral("TASK-023 RS485 Validation"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Phase 7 RS485 物理断线恢复预检与半自动正式验收"));
    parser.addHelpOption();
    QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")},
                                  QStringLiteral("运行时枚举确认的 USB-RS485 串口"),
                                  QStringLiteral("port"));
    QCommandLineOption suiteOption(QStringLiteral("suite"),
                                   QStringLiteral("Phase 7 正式 Schema v3 套件"),
                                   QStringLiteral("path"));
    QCommandLineOption revisionOption(QStringLiteral("source-revision"),
                                      QStringLiteral("可追溯源码修订标识"),
                                      QStringLiteral("revision"));
    QCommandLineOption modeOption(QStringLiteral("mode"),
                                  QStringLiteral("preflight 或 full"),
                                  QStringLiteral("mode"));
    QCommandLineOption timeoutOption(QStringLiteral("timeout-ms"),
                                     QStringLiteral("响应超时，毫秒"),
                                     QStringLiteral("ms"), QStringLiteral("500"));
    parser.addOption(portOption);
    parser.addOption(suiteOption);
    parser.addOption(revisionOption);
    parser.addOption(modeOption);
    parser.addOption(timeoutOption);
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
        QTextStream(stderr) << "--mode 必须为 preflight 或 full" << Qt::endl;
        return 1;
    }
    bool timeoutOk = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    if (!timeoutOk || timeoutMs < 1 || timeoutMs > 60000) {
        QTextStream(stderr) << "--timeout-ms 必须为 1..60000" << Qt::endl;
        return 1;
    }
    if (!QFileInfo::exists(parser.value(suiteOption))) {
        QTextStream(stderr) << "--suite 文件不存在" << Qt::endl;
        return 1;
    }
    ValidationRunner runner(parser.value(portOption), parser.value(suiteOption),
                            parser.value(revisionOption), *mode, timeoutMs);
    QTimer::singleShot(0, &runner, [&runner] { runner.start(); });
    return application.exec();
}
