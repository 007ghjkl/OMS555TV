#include "testing/TestAutomationController.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <utility>

namespace oms555tv::testing {
namespace {

constexpr qint64 maximumSuiteFileBytes = 4 * 1024 * 1024;

QString appFailureText(const app::AppCommandResult &result, const QString &action)
{
    QString diagnostic = QStringLiteral("%1失败").arg(action);
    if (result.error && !result.error->diagnostic.trimmed().isEmpty()) {
        diagnostic += QStringLiteral("：%1").arg(result.error->diagnostic.trimmed());
    } else if (result.error && result.error->communicationError
               && !result.error->communicationError->diagnostic.trimmed().isEmpty()) {
        diagnostic += QStringLiteral("：%1")
            .arg(result.error->communicationError->diagnostic.trimmed());
    }
    return diagnostic;
}

} // namespace

TestAutomationController::TestAutomationController(
    app::AppStateController &appState,
    TestEngine &engine,
    TestResultManager &results,
    QObject *parent)
    : QObject(parent)
    , appState_(appState)
    , engine_(engine)
    , results_(results)
{
    connect(&appState_, &app::AppStateController::commandCompleted,
            this, &TestAutomationController::handleAppCommandCompleted);
    connect(&engine_, &TestEngine::caseStarted, this, [this](const QString &caseId) {
        currentCaseId_ = caseId;
        currentStep_.reset();
        currentRequestId_.reset();
        currentAttempt_ = 0;
        currentLogicalStepId_.clear();
        currentLogicalStepIndex_ = -1;
        currentRepetition_ = 0;
        emit currentStepChanged();
    });
    connect(&engine_, &TestEngine::stepStarted, this,
            [this](const QString &caseId, TestStepPurpose purpose,
                   communication::RequestId requestId, int attempt,
                   QString logicalStepId, qsizetype logicalStepIndex,
                   int repetition) {
        currentCaseId_ = caseId;
        currentStep_ = purpose;
        currentRequestId_ = requestId;
        currentAttempt_ = attempt;
        currentLogicalStepId_ = std::move(logicalStepId);
        currentLogicalStepIndex_ = logicalStepIndex;
        currentRepetition_ = repetition;
        emit currentStepChanged();
    });
    connect(&engine_, &TestEngine::caseFinished, this,
            [this](const QString &caseId, TestStatus) {
        if (currentCaseId_ == caseId) {
            currentStep_.reset();
            currentRequestId_.reset();
            currentAttempt_ = 0;
            currentLogicalStepId_.clear();
            currentLogicalStepIndex_ = -1;
            currentRepetition_ = 0;
            emit currentStepChanged();
        }
    });
    connect(&engine_, &TestEngine::runCompleted,
            this, [this](const TestSuiteResult &) { handleRunCompleted(); });
    connect(&results_, &TestResultManager::snapshotChanged,
            this, [this] { emit stateChanged(); });
}

TestAutomationState TestAutomationController::state() const noexcept
{
    return state_;
}

bool TestAutomationController::busy() const noexcept
{
    return state_ != TestAutomationState::Idle;
}

const std::optional<TestSuite> &TestAutomationController::suite() const noexcept
{
    return suite_;
}

const QString &TestAutomationController::suitePath() const noexcept
{
    return suitePath_;
}

const QVector<ConfigError> &TestAutomationController::loadErrors() const noexcept
{
    return loadErrors_;
}

const QString &TestAutomationController::lastError() const noexcept
{
    return lastError_;
}

std::shared_ptr<const TestSuiteResult>
TestAutomationController::resultSnapshot() const noexcept
{
    return resultVisible_ ? results_.snapshot() : nullptr;
}

bool TestAutomationController::resumeMonitoring() const noexcept
{
    return resumeMonitoring_;
}

QString TestAutomationController::currentCaseId() const
{
    return currentCaseId_;
}

std::optional<TestStepPurpose> TestAutomationController::currentStep() const noexcept
{
    return currentStep_;
}

std::optional<communication::RequestId>
TestAutomationController::currentRequestId() const noexcept
{
    return currentRequestId_;
}

int TestAutomationController::currentAttempt() const noexcept
{
    return currentAttempt_;
}

const QString &TestAutomationController::currentLogicalStepId() const noexcept
{
    return currentLogicalStepId_;
}

qsizetype TestAutomationController::currentLogicalStepIndex() const noexcept
{
    return currentLogicalStepIndex_;
}

int TestAutomationController::currentRepetition() const noexcept
{
    return currentRepetition_;
}

void TestAutomationController::setResumeMonitoring(bool enabled)
{
    if (busy() || resumeMonitoring_ == enabled) {
        return;
    }
    resumeMonitoring_ = enabled;
    emit stateChanged();
}

bool TestAutomationController::loadSuiteFile(const QString &path)
{
    const QString normalized = QFileInfo(path.trimmed()).absoluteFilePath();
    if (busy()) {
        setError(QStringLiteral("工作流活动时不能加载测试套件"));
        return false;
    }
    if (path.trimmed().isEmpty()) {
        setError(QStringLiteral("测试套件路径不能为空"));
        return false;
    }
    lastError_.clear();
    loadErrors_.clear();
    setState(TestAutomationState::Loading);
    const QPointer<TestAutomationController> guard(this);
    QThreadPool::globalInstance()->start([guard, normalized] {
        AsyncLoadResult result;
        result.path = normalized;
        QFile file(normalized);
        if (!file.open(QIODevice::ReadOnly)) {
            result.fileError = QStringLiteral("无法读取测试套件：%1").arg(file.errorString());
        } else if (file.size() > maximumSuiteFileBytes) {
            result.fileError = QStringLiteral("测试套件超过 4 MiB 限制");
        } else {
            result.loadResult = TestCaseLoader::load(file.readAll());
        }
        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, result = std::move(result)]() mutable {
                if (guard) {
                    guard->finishLoad(std::move(result));
                }
            }, Qt::QueuedConnection);
        }
    });
    return true;
}

bool TestAutomationController::runAll(const monitor::MonitorConfig &monitorConfig)
{
    return beginRun({}, monitorConfig, false);
}

bool TestAutomationController::runSelected(const QSet<QString> &caseIds,
                                           const monitor::MonitorConfig &monitorConfig)
{
    return beginRun(caseIds, monitorConfig, true);
}

bool TestAutomationController::skipCase(const QString &caseId)
{
    if (state_ != TestAutomationState::Running || !engine_.skipCase(caseId)) {
        setError(QStringLiteral("只能跳过本次运行中尚未开始的用例"));
        return false;
    }
    lastError_.clear();
    emit stateChanged();
    return true;
}

bool TestAutomationController::abort()
{
    if (state_ != TestAutomationState::Running || !engine_.abort()) {
        setError(QStringLiteral("当前没有可中止的测试运行"));
        return false;
    }
    lastError_.clear();
    emit stateChanged();
    return true;
}

void TestAutomationController::setState(TestAutomationState state)
{
    if (state_ == state) {
        emit stateChanged();
        return;
    }
    state_ = state;
    emit stateChanged();
}

void TestAutomationController::setError(QString error)
{
    lastError_ = std::move(error);
    emit stateChanged();
}

void TestAutomationController::finishLoad(AsyncLoadResult result)
{
    suitePath_ = std::move(result.path);
    suite_.reset();
    loadErrors_ = std::move(result.loadResult.errors);
    lastError_ = std::move(result.fileError);
    if (result.loadResult.suite) {
        suite_ = std::move(result.loadResult.suite);
    }
    resultVisible_ = false;
    setState(TestAutomationState::Idle);
    emit suiteChanged();
}

bool TestAutomationController::beginRun(QSet<QString> selectedCaseIds,
                                        const monitor::MonitorConfig &monitorConfig,
                                        bool requireSelection)
{
    if (busy()) {
        setError(QStringLiteral("已有测试工作流正在进行"));
        return false;
    }
    if (!suite_) {
        setError(QStringLiteral("请先加载有效测试套件"));
        return false;
    }
    if (requireSelection && selectedCaseIds.isEmpty()) {
        setError(QStringLiteral("请至少选择一个用例"));
        return false;
    }
    if (appState_.state() != app::AppState::ConnectedIdle
        && appState_.state() != app::AppState::Monitoring) {
        setError(QStringLiteral("只有已连接空闲或监控状态可以开始测试"));
        return false;
    }
    lastError_.clear();
    selectedCaseIds_ = std::move(selectedCaseIds);
    savedMonitorConfig_ = monitorConfig;
    wasMonitoring_ = appState_.state() == app::AppState::Monitoring;
    currentCaseId_.clear();
    currentStep_.reset();
    currentRequestId_.reset();
    currentAttempt_ = 0;
    currentLogicalStepId_.clear();
    currentLogicalStepIndex_ = -1;
    currentRepetition_ = 0;
    emit currentStepChanged();
    if (wasMonitoring_) {
        const auto submission = appState_.stopMonitoring();
        if (!submission.accepted()) {
            failWorkflow(QStringLiteral("停止监控命令被拒绝"));
            return false;
        }
        pendingAction_ = PendingAction::StopMonitoring;
        pendingOperationId_ = submission.operationId;
        setState(TestAutomationState::StoppingMonitoring);
        return true;
    }
    acquireTesting();
    return state_ != TestAutomationState::Idle;
}

void TestAutomationController::acquireTesting()
{
    const auto submission = appState_.startTesting();
    if (!submission.accepted()) {
        failWorkflow(QStringLiteral("获取 Testing owner 的命令被拒绝"));
        return;
    }
    pendingAction_ = PendingAction::StartTesting;
    pendingOperationId_ = submission.operationId;
    setState(TestAutomationState::AcquiringTesting);
}

void TestAutomationController::startEngine()
{
    if (!suite_) {
        failWorkflow(QStringLiteral("准备执行时测试套件不存在"));
        return;
    }
    resultVisible_ = true;
    const auto submission = engine_.runSuite(*suite_, selectedCaseIds_);
    if (submission.accepted()) {
        pendingAction_ = PendingAction::None;
        pendingOperationId_.reset();
        setState(TestAutomationState::Running);
        return;
    }
    lastError_ = submission.rejection
        ? QStringLiteral("测试引擎拒绝运行：%1").arg(submission.rejection->diagnostic)
        : QStringLiteral("测试引擎拒绝运行");
    resultVisible_ = false;
    const auto cleanup = appState_.stopTesting();
    if (!cleanup.accepted()) {
        failWorkflow(lastError_ + QStringLiteral("；Testing owner 释放命令也被拒绝"));
        return;
    }
    pendingAction_ = PendingAction::CleanupTesting;
    pendingOperationId_ = cleanup.operationId;
    setState(TestAutomationState::AcquiringTesting);
}

void TestAutomationController::handleAppCommandCompleted(
    const app::AppCommandResult &result)
{
    if (!pendingOperationId_ || !(result.operationId == *pendingOperationId_)) {
        return;
    }
    const PendingAction action = pendingAction_;
    pendingOperationId_.reset();
    pendingAction_ = PendingAction::None;
    if (!result.succeeded) {
        switch (action) {
        case PendingAction::StopMonitoring:
            failWorkflow(appFailureText(result, QStringLiteral("停止监控")));
            break;
        case PendingAction::StartTesting:
            failWorkflow(appFailureText(result, QStringLiteral("获取 Testing owner")));
            break;
        case PendingAction::CleanupTesting:
            failWorkflow(appFailureText(result, QStringLiteral("释放 Testing owner")));
            break;
        case PendingAction::ResumeMonitoring:
            failWorkflow(appFailureText(result, QStringLiteral("恢复监控")));
            break;
        case PendingAction::None:
            break;
        }
        return;
    }
    switch (action) {
    case PendingAction::StopMonitoring:
        acquireTesting();
        break;
    case PendingAction::StartTesting:
        startEngine();
        break;
    case PendingAction::CleanupTesting:
        finishWorkflow();
        break;
    case PendingAction::ResumeMonitoring:
        finishWorkflow();
        break;
    case PendingAction::None:
        break;
    }
}

void TestAutomationController::handleRunCompleted()
{
    if (state_ != TestAutomationState::Running) {
        return;
    }
    currentStep_.reset();
    currentRequestId_.reset();
    currentAttempt_ = 0;
    currentLogicalStepId_.clear();
    currentLogicalStepIndex_ = -1;
    currentRepetition_ = 0;
    emit currentStepChanged();
    if (wasMonitoring_ && resumeMonitoring_) {
        const auto submission = appState_.startMonitoring(savedMonitorConfig_);
        if (!submission.accepted()) {
            failWorkflow(QStringLiteral("测试结束，但恢复监控命令被拒绝"));
            return;
        }
        pendingAction_ = PendingAction::ResumeMonitoring;
        pendingOperationId_ = submission.operationId;
        setState(TestAutomationState::ResumingMonitoring);
        return;
    }
    finishWorkflow();
}

void TestAutomationController::failWorkflow(QString diagnostic)
{
    pendingAction_ = PendingAction::None;
    pendingOperationId_.reset();
    selectedCaseIds_.clear();
    wasMonitoring_ = false;
    lastError_ = std::move(diagnostic);
    setState(TestAutomationState::Idle);
    emit workflowFinished();
}

void TestAutomationController::finishWorkflow()
{
    pendingAction_ = PendingAction::None;
    pendingOperationId_.reset();
    selectedCaseIds_.clear();
    wasMonitoring_ = false;
    setState(TestAutomationState::Idle);
    emit workflowFinished();
}

QString testAutomationStateName(TestAutomationState state)
{
    switch (state) {
    case TestAutomationState::Idle: return QStringLiteral("IDLE");
    case TestAutomationState::Loading: return QStringLiteral("LOADING");
    case TestAutomationState::StoppingMonitoring: return QStringLiteral("STOPPING_MONITORING");
    case TestAutomationState::AcquiringTesting: return QStringLiteral("ACQUIRING_TESTING");
    case TestAutomationState::Running: return QStringLiteral("RUNNING");
    case TestAutomationState::ResumingMonitoring: return QStringLiteral("RESUMING_MONITORING");
    }
    return QStringLiteral("UNKNOWN");
}

} // namespace oms555tv::testing
