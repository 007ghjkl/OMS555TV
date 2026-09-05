#pragma once

#include "app/AppStateController.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorScheduler.h"
#include "testing/TestEngine.h"
#include "testing/TestResultManager.h"

#include <QObject>
#include <QSet>

#include <optional>

namespace oms555tv::testing {

enum class GuidedCoordinatorState { Idle, Running, Releasing };

struct GuidedExecutionView {
    bool visible = false;
    GuidedRunState state = GuidedRunState::Idle;
    GuidedTerminalReason terminalReason = GuidedTerminalReason::None;
    TestRunId runId;
    QString caseId;
    QString stepId;
    QString token;
    QString title;
    QString instruction;
    QString safetyNotice;
    QVector<GuidedOperatorAction> allowedActions;
    std::chrono::milliseconds remaining{0};
    int consecutiveMatches = 0;
    int requiredConsecutiveMatches = 0;
    std::optional<communication::RequestId> currentRequestId;
    std::optional<RecoveryTiming> recoveryTiming;
    QString restorationReminder;
};

class GuidedTestCoordinator final : public QObject
{
    Q_OBJECT

public:
    GuidedTestCoordinator(app::AppStateController &appState,
                          TestEngine &engine,
                          TestResultManager &results,
                          logging::SessionLogService *sessionLog,
                          monitor::IMonitorScheduler &scheduler,
                          QObject *parent = nullptr);
    ~GuidedTestCoordinator() override;

    [[nodiscard]] GuidedCoordinatorState coordinatorState() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] GuidedExecutionView view() const;
    [[nodiscard]] QString lastError() const;

    [[nodiscard]] bool runSuite(const TestSuite &suite,
                                const QSet<QString> &selectedCaseIds = {});
    [[nodiscard]] GuidedActionRejection submitAction(const GuidedActionCommand &command);
    [[nodiscard]] bool abort();

signals:
    void viewChanged();
    void runCompleted(const oms555tv::testing::TestSuiteResult &result);

private:
    void startNextCase();
    void showPrompt(qsizetype stepIndex);
    void beginObservation(qsizetype stepIndex);
    void submitProbe();
    void handleProbeCompleted(TestRunId runId, const QString &caseId,
                              const QString &stepId,
                              const communication::ModbusRequestResult &result);
    void handleAppCommandCompleted(const app::AppCommandResult &result);
    void scheduleStageDeadline(std::chrono::milliseconds duration);
    void scheduleNextProbe(std::chrono::milliseconds interval);
    void cancelScheduledTask() noexcept;
    void invalidateStage() noexcept;
    void finishObservation(GuidedObservationOutcome outcome,
                           GuidedTerminalReason failureReason = GuidedTerminalReason::None,
                           std::optional<TestError> error = std::nullopt);
    void finishCurrentCase(GuidedTerminalReason reason,
                           std::optional<TestError> error = std::nullopt);
    void beginRelease();
    void finalizeRun();
    void publish(bool terminal = false);
    void appendLog(QString event, QString message,
                   const QVariantMap &metadata = {},
                   std::optional<communication::RequestId> requestId = std::nullopt);
    [[nodiscard]] TestCaseResult *currentCaseResult();
    [[nodiscard]] const TestCase *currentCase() const;
    [[nodiscard]] std::chrono::milliseconds remainingTo(
        std::chrono::nanoseconds deadline) const noexcept;
    [[nodiscard]] std::chrono::milliseconds caseRemaining() const noexcept;
    [[nodiscard]] QString recoveryInstruction() const;

    app::AppStateController &appState_;
    TestEngine &engine_;
    TestResultManager &results_;
    logging::SessionLogService *sessionLog_ = nullptr;
    monitor::IMonitorScheduler &scheduler_;
    GuidedCoordinatorState coordinatorState_ = GuidedCoordinatorState::Idle;
    std::optional<TestSuiteResult> run_;
    QSet<QString> selected_;
    qsizetype currentCaseIndex_ = -1;
    qsizetype currentStepIndex_ = -1;
    GuidedExecutionView view_;
    GuidedActionContext actionContext_;
    std::optional<monitor::IMonitorScheduler::TaskId> deadlineTask_;
    std::optional<monitor::IMonitorScheduler::TaskId> nextProbeTask_;
    quint64 stageGeneration_ = 0;
    std::chrono::nanoseconds runStartedMonotonic_{0};
    std::chrono::nanoseconds caseStartedMonotonic_{0};
    std::chrono::nanoseconds stageStartedMonotonic_{0};
    std::chrono::nanoseconds stageDeadlineMonotonic_{0};
    std::optional<communication::RequestId> currentProbe_;
    quint64 nextRunId_ = 1;
    quint64 nextAttemptSequence_ = 1;
    std::optional<quint64> reconnectConfirmedMs_;
    std::optional<quint64> firstRecoveryMs_;
    std::optional<quint64> stableRecoveryMs_;
    std::optional<app::AppOperationId> releaseOperationId_;
    QString lastError_;
    bool loggingErrorRecorded_ = false;
};

[[nodiscard]] QString guidedCoordinatorStateName(GuidedCoordinatorState state);

} // namespace oms555tv::testing
