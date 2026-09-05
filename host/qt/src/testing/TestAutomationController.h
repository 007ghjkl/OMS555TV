#pragma once

#include "app/AppStateController.h"
#include "testing/TestCaseLoader.h"
#include "testing/TestEngine.h"

#include <QObject>
#include <QSet>

#include <memory>
#include <optional>

namespace oms555tv::testing {

enum class TestAutomationState {
    Idle,
    Loading,
    StoppingMonitoring,
    AcquiringTesting,
    Running,
    ResumingMonitoring,
};

class TestAutomationController final : public QObject
{
    Q_OBJECT

public:
    TestAutomationController(app::AppStateController &appState,
                             TestEngine &engine,
                             TestResultManager &results,
                             QObject *parent = nullptr);

    [[nodiscard]] TestAutomationState state() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] const std::optional<TestSuite> &suite() const noexcept;
    [[nodiscard]] const QString &suitePath() const noexcept;
    [[nodiscard]] const QVector<ConfigError> &loadErrors() const noexcept;
    [[nodiscard]] const QString &lastError() const noexcept;
    [[nodiscard]] std::shared_ptr<const TestSuiteResult> resultSnapshot() const noexcept;
    [[nodiscard]] bool resumeMonitoring() const noexcept;
    [[nodiscard]] QString currentCaseId() const;
    [[nodiscard]] std::optional<TestStepPurpose> currentStep() const noexcept;
    [[nodiscard]] std::optional<communication::RequestId> currentRequestId() const noexcept;
    [[nodiscard]] int currentAttempt() const noexcept;

    void setResumeMonitoring(bool enabled);
    [[nodiscard]] bool loadSuiteFile(const QString &path);
    [[nodiscard]] bool runAll(const monitor::MonitorConfig &monitorConfig);
    [[nodiscard]] bool runSelected(const QSet<QString> &caseIds,
                                   const monitor::MonitorConfig &monitorConfig);
    [[nodiscard]] bool skipCase(const QString &caseId);
    [[nodiscard]] bool abort();

signals:
    void stateChanged();
    void suiteChanged();
    void currentStepChanged();
    void workflowFinished();

private:
    enum class PendingAction {
        None,
        StopMonitoring,
        StartTesting,
        CleanupTesting,
        ResumeMonitoring,
    };

    struct AsyncLoadResult {
        QString path;
        LoadResult loadResult;
        QString fileError;
    };

    void setState(TestAutomationState state);
    void setError(QString error);
    void finishLoad(AsyncLoadResult result);
    [[nodiscard]] bool beginRun(QSet<QString> selectedCaseIds,
                                const monitor::MonitorConfig &monitorConfig,
                                bool requireSelection);
    void acquireTesting();
    void startEngine();
    void handleAppCommandCompleted(const app::AppCommandResult &result);
    void handleRunCompleted();
    void failWorkflow(QString diagnostic);
    void finishWorkflow();

    app::AppStateController &appState_;
    TestEngine &engine_;
    TestResultManager &results_;
    TestAutomationState state_ = TestAutomationState::Idle;
    PendingAction pendingAction_ = PendingAction::None;
    std::optional<app::AppOperationId> pendingOperationId_;
    std::optional<TestSuite> suite_;
    QString suitePath_;
    QVector<ConfigError> loadErrors_;
    QString lastError_;
    bool resumeMonitoring_ = false;
    bool wasMonitoring_ = false;
    monitor::MonitorConfig savedMonitorConfig_;
    QSet<QString> selectedCaseIds_;
    QString currentCaseId_;
    std::optional<TestStepPurpose> currentStep_;
    std::optional<communication::RequestId> currentRequestId_;
    int currentAttempt_ = 0;
    bool resultVisible_ = false;
};

[[nodiscard]] QString testAutomationStateName(TestAutomationState state);

} // namespace oms555tv::testing
