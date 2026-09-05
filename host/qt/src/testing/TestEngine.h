#pragma once

#include "app/AppStateController.h"
#include "logging/SessionLogService.h"
#include "monitor/MonitorScheduler.h"
#include "testing/TestCaseHandlers.h"
#include "testing/TestResultManager.h"

#include <QObject>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

namespace oms555tv::testing {

class TestEngine final : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;

    TestEngine(app::AppStateController &appState,
               communication::IModbusClient &client,
               TestResultManager &results,
               logging::SessionLogService *sessionLog = nullptr,
               Clock clock = {},
               TestHandlerRegistry registry = TestHandlerRegistry::withBasicHandlers(),
               QObject *parent = nullptr);
    TestEngine(app::AppStateController &appState,
               communication::IModbusClient &client,
               TestResultManager &results,
               logging::SessionLogService *sessionLog,
               monitor::IMonitorScheduler &scheduler,
               TestHandlerRegistry registry = TestHandlerRegistry::withBasicHandlers(),
               QObject *parent = nullptr);
    ~TestEngine() override;

    [[nodiscard]] TestEngineState state() const noexcept;
    [[nodiscard]] std::optional<TestRunId> activeRunId() const noexcept;

    TestRunSubmission runSuite(const TestSuite &suite,
                               const QSet<QString> &selectedCaseIds = {});
    TestRunSubmission runCase(const TestSuite &suite, const QString &caseId);
    [[nodiscard]] bool skipCase(const QString &caseId);
    [[nodiscard]] bool abort();

signals:
    void stateChanged(oms555tv::testing::TestEngineState state);
    void caseStarted(QString caseId);
    void stepStarted(QString caseId,
                     oms555tv::testing::TestStepPurpose purpose,
                     oms555tv::communication::RequestId requestId,
                     int attempt);
    void caseFinished(QString caseId, oms555tv::testing::TestStatus status);
    void runCompleted(const oms555tv::testing::TestSuiteResult &result);

private:
    [[nodiscard]] QDateTime nowUtc() const;
    [[nodiscard]] std::optional<TestRunId> allocateRunId() noexcept;
    [[nodiscard]] TestRunSubmission reject(TestErrorCode code, QString diagnostic) const;
    void setState(TestEngineState state);
    void startNextCase();
    void submitStep(TestHandlerStep step, bool retry = false, QString retryReason = {});
    void scheduleDelay(std::chrono::nanoseconds delay);
    void cancelDelay() noexcept;
    void handleRequestCompleted(const communication::ModbusRequestResult &result);
    void handleAppCommandCompleted(const app::AppCommandResult &result);
    void applyDecision(TestHandlerDecision decision);
    void completeCurrentCase(TestHandlerDecision decision);
    void markRemainingAborted();
    void beginRelease();
    void finalizeRun();
    [[nodiscard]] bool shouldRetry(const communication::CommunicationError &error) const;
    [[nodiscard]] std::chrono::milliseconds caseElapsed() const;
    [[nodiscard]] TestHandlerContext handlerContext() const;
    void recordCompletedStep(TestCompositeStepResult step);
    void retainAttempt(TestRequestAttemptResult attempt,
                       const communication::ModbusRequestResult &requestResult);
    void refreshStabilityRetention();
    [[nodiscard]] TestError requestError(TestErrorCode code,
                                         const communication::ModbusRequestResult &result,
                                         QString diagnostic) const;
    void publish(bool terminal = false);
    void appendLog(QString event, QString message,
                   const TestCaseResult *testCase = nullptr,
                   const TestRequestAttemptResult *attempt = nullptr);

    app::AppStateController &appState_;
    communication::IModbusClient &client_;
    TestResultManager &results_;
    logging::SessionLogService *sessionLog_ = nullptr;
    Clock clock_;
    TestHandlerRegistry registry_;
    std::unique_ptr<monitor::QtMonitorScheduler> ownedScheduler_;
    monitor::IMonitorScheduler *scheduler_ = nullptr;
    TestEngineState state_ = TestEngineState::Idle;
    quint64 nextRunId_ = 1;
    quint64 nextAttemptSequence_ = 1;
    std::optional<TestSuiteResult> run_;
    QSet<QString> selected_;
    qsizetype currentCaseIndex_ = -1;
    std::unique_ptr<ITestCaseHandler> handler_;
    std::optional<TestHandlerStep> currentStep_;
    std::optional<monitor::IMonitorScheduler::TaskId> scheduledDelay_;
    quint64 delayGeneration_ = 0;
    std::optional<communication::RequestId> currentRequestId_;
    QDateTime currentAttemptStarted_;
    QString currentRetryReason_;
    int stepAttempt_ = 0;
    std::chrono::nanoseconds runStartedMonotonic_{0};
    std::chrono::nanoseconds caseStartedMonotonic_{0};
    std::chrono::nanoseconds currentLogicalStepStartedMonotonic_{0};
    QDateTime currentLogicalStepStartedUtc_;
    QVector<quint64> currentLogicalAttemptSequences_;
    struct StabilityRetentionState {
        int limit = 0;
        quint64 maximumStarts = 0;
        quint64 samplingStride = 1;
        int failureCapacity = 0;
        int sampleCapacity = 0;
        quint64 totalFailures = 0;
        QVector<TestRequestAttemptResult> head;
        QVector<TestRequestAttemptResult> failures;
        QVector<TestRequestAttemptResult> samples;
        QVector<TestRequestAttemptResult> tail;
    } stabilityRetention_;
    std::optional<app::AppOperationId> stopOperationId_;
    bool loggingErrorRecorded_ = false;
};

} // namespace oms555tv::testing
