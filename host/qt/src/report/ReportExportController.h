#pragma once

#include "communication/CommunicationTypes.h"
#include "report/ReportTypes.h"

#include <QObject>
#include <QTimeZone>

#include <functional>
#include <memory>
#include <optional>

namespace oms555tv::logging { class SessionLogService; }
namespace oms555tv::testing {
class TestAutomationController;
struct TestSuiteResult;
}

namespace oms555tv::report {

enum class ReportExportState { NoResult, Ready, Exporting, Succeeded, Failed };

struct ReportRuntimeContext {
    std::optional<communication::ModbusConnectionConfig> connectionConfig;
    QString applicationVersion;
    QString sourceRevision;
    QTimeZone displayTimeZone{QTimeZone::systemTimeZone()};
};

struct ReportPreview {
    quint64 runId = 0;
    QString suiteId;
    QString suiteName;
    QString sessionId;
    testing::TestStatus status = testing::TestStatus::NotRun;
    QString startedUtc;
    QString finishedUtc;
    qint64 durationMs = 0;
    quint64 total = 0;
    quint64 passed = 0;
    quint64 failed = 0;
    quint64 errors = 0;
    quint64 skipped = 0;
    QString evidenceRetention;
    QString configuredDeviceModel;
    QString configuredTestBench;
    QString configuredEnvironment;
    QString suggestedFileName;
};

struct ReportExportRequest {
    OperatorReportMetadata operatorMetadata;
    QString outputDirectory;
    QString fileName;
};

struct ReportExportOutcome {
    quint64 runId = 0;
    std::optional<QString> filePath;
    qint64 fileSizeBytes = 0;
    QVector<ReportError> errors;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return filePath.has_value() && errors.isEmpty();
    }
};

class ReportExportController final : public QObject
{
    Q_OBJECT

public:
    using RuntimeContextProvider = std::function<ReportRuntimeContext()>;

    ReportExportController(testing::TestAutomationController &automation,
                           logging::SessionLogService &sessionLog,
                           RuntimeContextProvider runtimeContextProvider,
                           QObject *parent = nullptr);

    [[nodiscard]] ReportExportState state() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool hasCompletedResult() const noexcept;
    [[nodiscard]] const std::optional<ReportPreview> &preview() const noexcept;
    [[nodiscard]] const ReportExportOutcome &lastOutcome() const noexcept;
    [[nodiscard]] bool canExport() const noexcept;

    [[nodiscard]] bool exportHtml(const ReportExportRequest &request);

    [[nodiscard]] static QString defaultOutputDirectory();

signals:
    void stateChanged();
    void exportFinished();

private:
    struct LatestResult {
        std::shared_ptr<const testing::TestSuiteResult> result;
        ReportRuntimeContext runtime;
        QString sessionId;
        QString sessionPath;
    };

    void captureCompletedResult();
    void reject(ReportErrorCode code, QString path, QString diagnostic);
    void completeExport(ReportExportOutcome outcome);

    testing::TestAutomationController &automation_;
    logging::SessionLogService &sessionLog_;
    RuntimeContextProvider runtimeContextProvider_;
    ReportExportState state_ = ReportExportState::NoResult;
    bool exportActive_ = false;
    std::optional<LatestResult> latest_;
    std::optional<ReportPreview> preview_;
    ReportExportOutcome lastOutcome_;
};

[[nodiscard]] QString reportExportStateName(ReportExportState state);

} // namespace oms555tv::report
