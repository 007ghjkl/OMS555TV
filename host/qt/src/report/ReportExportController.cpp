#include "report/ReportExportController.h"

#include "logging/LogEntry.h"
#include "logging/SessionLogService.h"
#include "report/HtmlReportGenerator.h"
#include "report/ReportModelBuilder.h"
#include "testing/TestAutomationController.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QThreadPool>

#include <utility>

namespace oms555tv::report {
namespace {

bool terminalStatus(const testing::TestStatus status)
{
    return status == testing::TestStatus::Pass
        || status == testing::TestStatus::Fail
        || status == testing::TestStatus::Error
        || status == testing::TestStatus::Skipped;
}

bool completeResult(const testing::TestSuiteResult &result)
{
    if (!result.suite || result.runId.value == 0 || !terminalStatus(result.status)
        || !result.startedUtc.isValid() || !result.finishedUtc.isValid()
        || result.finishedUtc < result.startedUtc
        || result.cases.size() != result.suite->cases.size()) {
        return false;
    }
    for (const auto &testCase : result.cases) {
        if (!terminalStatus(testCase.status)) return false;
    }
    return true;
}

ReportError error(const ReportErrorCode code, QString path, QString diagnostic)
{
    return {code, std::move(path), std::move(diagnostic)};
}

QString metadataValue(const testing::TestSuite &suite, const QStringList &keys)
{
    for (const auto &key : keys) {
        const auto found = suite.metadata.constFind(key);
        if (found != suite.metadata.cend() && !found.value().trimmed().isEmpty()) {
            return found.value();
        }
    }
    return {};
}

QString evidenceSummary(const ReportDocumentModel &document)
{
    QStringList policies;
    quint64 retained = 0;
    quint64 total = 0;
    for (const auto &testCase : document.cases) {
        if (!testCase.evidenceRetention.policy.isEmpty()
            && !policies.contains(testCase.evidenceRetention.policy)) {
            policies.append(testCase.evidenceRetention.policy);
        }
        retained += testCase.evidenceRetention.retainedAttempts;
        total += testCase.evidenceRetention.totalAttempts;
    }
    return QStringLiteral("%1；内存保留 %2/%3 次事务")
        .arg(policies.isEmpty() ? QStringLiteral("无事务证据")
                                : policies.join(QStringLiteral(", ")))
        .arg(retained).arg(total);
}

std::optional<SessionLogArtifactInput> sessionArtifact(
    const QString &expectedSessionId, const QString &path,
    QVector<ReportError> &errors)
{
    if (expectedSessionId.isEmpty() || path.isEmpty()) return std::nullopt;
    QFile file(path);
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !file.open(QIODevice::ReadOnly)) {
        errors.append(error(
            ReportErrorCode::InvalidSessionLogArtifact,
            QStringLiteral("sessionLogArtifact.path"),
            QStringLiteral("无法读取已结束的 SessionLog 工件：%1")
                .arg(file.errorString())));
        return std::nullopt;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        errors.append(error(
            ReportErrorCode::InvalidSessionLogArtifact,
            QStringLiteral("sessionLogArtifact.sha256"),
            QStringLiteral("无法计算 SessionLog 工件 SHA-256")));
        return std::nullopt;
    }
    return SessionLogArtifactInput{true, expectedSessionId,
                                   info.absoluteFilePath(), info.size(),
                                   QString::fromLatin1(hash.result().toHex()).toUpper()};
}

} // namespace

ReportExportController::ReportExportController(
    testing::TestAutomationController &automation,
    logging::SessionLogService &sessionLog,
    RuntimeContextProvider runtimeContextProvider,
    QObject *parent)
    : QObject(parent)
    , automation_(automation)
    , sessionLog_(sessionLog)
    , runtimeContextProvider_(std::move(runtimeContextProvider))
{
    connect(&automation_, &testing::TestAutomationController::workflowFinished,
            this, &ReportExportController::captureCompletedResult);
    connect(&automation_, &testing::TestAutomationController::stateChanged,
            this, [this] { emit stateChanged(); });
    QDir().mkpath(defaultOutputDirectory());
    captureCompletedResult();
}

ReportExportState ReportExportController::state() const noexcept { return state_; }
bool ReportExportController::busy() const noexcept
{
    return exportActive_;
}
bool ReportExportController::hasCompletedResult() const noexcept
{
    return latest_.has_value();
}
const std::optional<ReportPreview> &ReportExportController::preview() const noexcept
{
    return preview_;
}
const ReportExportOutcome &ReportExportController::lastOutcome() const noexcept
{
    return lastOutcome_;
}
bool ReportExportController::canExport() const noexcept
{
    return latest_.has_value() && !busy() && !automation_.busy();
}

QString ReportExportController::defaultOutputDirectory()
{
    return QFileInfo(QDir::current().filePath(QStringLiteral("output/reports")))
        .absoluteFilePath();
}

void ReportExportController::captureCompletedResult()
{
    const auto snapshot = automation_.resultSnapshot();
    if (!snapshot || !completeResult(*snapshot)) {
        emit stateChanged();
        return;
    }

    ReportRuntimeContext runtime = runtimeContextProvider_
        ? runtimeContextProvider_() : ReportRuntimeContext{};
    LatestResult latest{snapshot, std::move(runtime), sessionLog_.sessionId(),
                        sessionLog_.currentFilePath()};

    ReportInput input;
    input.suiteResult = *snapshot;
    input.connectionConfig = latest.runtime.connectionConfig;
    input.applicationVersion = latest.runtime.applicationVersion;
    input.sourceRevision = latest.runtime.sourceRevision;
    input.displayTimeZone = latest.runtime.displayTimeZone;
    const auto built = ReportModelBuilder::build(input);
    if (!built.succeeded()) {
        latest_ = std::move(latest);
        preview_.reset();
        lastOutcome_ = {snapshot->runId.value, std::nullopt, 0, built.errors};
        state_ = ReportExportState::Failed;
        emit stateChanged();
        return;
    }

    const auto &document = *built.document;
    const auto &suite = *snapshot->suite;
    ReportPreview preview;
    preview.runId = document.runId;
    preview.suiteId = document.suiteId;
    preview.suiteName = document.suiteName;
    preview.sessionId = document.metadata.sessionId.available
        ? document.metadata.sessionId.value : QString{};
    preview.status = document.status;
    preview.startedUtc = document.started.utcIso;
    preview.finishedUtc = document.finished.utcIso;
    preview.durationMs = document.duration.count();
    preview.total = document.summary.total;
    preview.passed = document.summary.passed;
    preview.failed = document.summary.failed;
    preview.errors = document.summary.errors;
    preview.skipped = document.summary.skipped;
    preview.evidenceRetention = evidenceSummary(document);
    preview.configuredDeviceModel = metadataValue(
        suite, {QStringLiteral("device_model"), QStringLiteral("hardware")});
    preview.configuredTestBench = metadataValue(
        suite, {QStringLiteral("test_bench"), QStringLiteral("bench")});
    preview.configuredEnvironment = metadataValue(
        suite, {QStringLiteral("environment_description"),
                QStringLiteral("hardware_scope")});
    preview.suggestedFileName = HtmlReportGenerator::suggestFileName(document);

    latest_ = std::move(latest);
    preview_ = std::move(preview);
    lastOutcome_ = {};
    if (!exportActive_) state_ = ReportExportState::Ready;
    emit stateChanged();
}

void ReportExportController::reject(const ReportErrorCode code, QString path,
                                    QString diagnostic)
{
    lastOutcome_ = {latest_ ? latest_->result->runId.value : 0, std::nullopt, 0,
                    {error(code, std::move(path), std::move(diagnostic))}};
    if (!exportActive_) state_ = ReportExportState::Failed;
    emit stateChanged();
    emit exportFinished();
}

bool ReportExportController::exportHtml(const ReportExportRequest &request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (busy()) {
        reject(ReportErrorCode::ExportInProgress, QStringLiteral("export"),
               QStringLiteral("已有报告导出作业正在进行"));
        return false;
    }
    if (automation_.busy()) {
        reject(ReportErrorCode::TestRunInProgress, QStringLiteral("suiteResult"),
               QStringLiteral("测试工作流运行期间不能导出报告"));
        return false;
    }
    if (!latest_ || !preview_) {
        reject(ReportErrorCode::NoCompletedResult, QStringLiteral("suiteResult"),
               QStringLiteral("尚无可导出的完整测试结果"));
        return false;
    }
    if (request.operatorMetadata.tester.trimmed().isEmpty()) {
        reject(ReportErrorCode::MissingRequiredMetadata,
               QStringLiteral("operatorMetadata.tester"),
               QStringLiteral("测试人员为必填项"));
        return false;
    }

    const QFileInfo nameInfo(request.fileName);
    if (request.outputDirectory.trimmed().isEmpty()
        || request.fileName.trimmed().isEmpty() || nameInfo.isAbsolute()
        || nameInfo.fileName() != request.fileName
        || !request.fileName.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)) {
        reject(ReportErrorCode::InvalidOutputPath, QStringLiteral("targetFilePath"),
               QStringLiteral("输出目录必须非空，文件名必须是单个 .html 文件名"));
        return false;
    }

    const LatestResult frozen = *latest_;
    ReportExportRequest frozenRequest = request;
    frozenRequest.outputDirectory = QFileInfo(request.outputDirectory).absoluteFilePath();
    const QString targetPath = QFileInfo(
        QDir(frozenRequest.outputDirectory).filePath(frozenRequest.fileName))
        .absoluteFilePath();

    QString artifactSessionId;
    QString artifactPath;
    if (!frozen.result->sessionId.isEmpty() && !sessionLog_.active()
        && sessionLog_.sessionId() == frozen.result->sessionId
        && frozen.sessionId == frozen.result->sessionId
        && sessionLog_.currentFilePath() == frozen.sessionPath) {
        artifactSessionId = frozen.result->sessionId;
        artifactPath = frozen.sessionPath;
    }

    state_ = ReportExportState::Exporting;
    exportActive_ = true;
    lastOutcome_ = {};
    emit stateChanged();

    QPointer<ReportExportController> guard(this);
    QThreadPool::globalInstance()->start(
        [guard, frozen, frozenRequest = std::move(frozenRequest), targetPath,
         artifactSessionId, artifactPath]() mutable {
            ReportExportOutcome outcome;
            outcome.runId = frozen.result->runId.value;
            ReportInput input;
            input.suiteResult = *frozen.result;
            input.connectionConfig = frozen.runtime.connectionConfig;
            input.applicationVersion = frozen.runtime.applicationVersion;
            input.sourceRevision = frozen.runtime.sourceRevision;
            input.operatorMetadata = std::move(frozenRequest.operatorMetadata);
            input.displayTimeZone = frozen.runtime.displayTimeZone;
            if (!artifactSessionId.isEmpty()) {
                input.sessionLogArtifact = sessionArtifact(
                    artifactSessionId, artifactPath, outcome.errors);
                if (!outcome.errors.isEmpty()) {
                    QMetaObject::invokeMethod(
                        guard, [guard, outcome = std::move(outcome)]() mutable {
                            if (guard) guard->completeExport(std::move(outcome));
                        });
                    return;
                }
            }

            const auto built = ReportModelBuilder::build(input);
            if (!built.succeeded()) {
                outcome.errors = built.errors;
            } else {
                const auto written = HtmlReportGenerator{}.write(*built.document, targetPath);
                outcome.errors = written.errors;
                if (written.succeeded()) {
                    outcome.filePath = QFileInfo(*written.filePath).absoluteFilePath();
                    outcome.fileSizeBytes = QFileInfo(*outcome.filePath).size();
                }
            }
            QMetaObject::invokeMethod(
                guard, [guard, outcome = std::move(outcome)]() mutable {
                    if (guard) guard->completeExport(std::move(outcome));
                });
        });
    return true;
}

void ReportExportController::completeExport(ReportExportOutcome outcome)
{
    Q_ASSERT(QThread::currentThread() == thread());
    exportActive_ = false;
    lastOutcome_ = std::move(outcome);
    state_ = lastOutcome_.succeeded() ? ReportExportState::Succeeded
                                     : ReportExportState::Failed;

    logging::LogEntry entry{QDateTime::currentDateTimeUtc(),
        lastOutcome_.succeeded() ? logging::LogLevel::Info : logging::LogLevel::Error,
        QStringLiteral("report"),
        lastOutcome_.succeeded()
            ? QStringLiteral("HTML 报告生成成功")
            : QStringLiteral("HTML 报告生成失败")};
    entry.event = QStringLiteral("report_export_completed");
    entry.metadata.insert(QStringLiteral("run_id"),
                          static_cast<qulonglong>(lastOutcome_.runId));
    entry.metadata.insert(QStringLiteral("succeeded"), lastOutcome_.succeeded());
    if (lastOutcome_.filePath) {
        entry.metadata.insert(QStringLiteral("file_path"), *lastOutcome_.filePath);
        entry.metadata.insert(QStringLiteral("file_size_bytes"), lastOutcome_.fileSizeBytes);
    }
    if (!lastOutcome_.errors.isEmpty()) {
        entry.metadata.insert(QStringLiteral("error_code"),
            reportErrorCodeName(lastOutcome_.errors.front().code));
    }
    sessionLog_.append(std::move(entry));
    emit stateChanged();
    emit exportFinished();
}

QString reportExportStateName(const ReportExportState state)
{
    switch (state) {
    case ReportExportState::NoResult: return QStringLiteral("NO_RESULT");
    case ReportExportState::Ready: return QStringLiteral("READY");
    case ReportExportState::Exporting: return QStringLiteral("EXPORTING");
    case ReportExportState::Succeeded: return QStringLiteral("SUCCEEDED");
    case ReportExportState::Failed: return QStringLiteral("FAILED");
    }
    return QStringLiteral("UNKNOWN");
}

} // namespace oms555tv::report
