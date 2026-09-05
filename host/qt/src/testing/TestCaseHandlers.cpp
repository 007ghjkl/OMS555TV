#include "testing/TestCaseHandlers.h"

#include <algorithm>
#include <limits>
#include <variant>

namespace oms555tv::testing {
namespace {

TestError invariant(QString diagnostic)
{
    return {TestErrorCode::InvariantViolation, std::move(diagnostic), std::nullopt};
}

TestHandlerDecision finishError(TestError error)
{
    TestHandlerDecision decision;
    decision.finished = true;
    decision.status = TestStatus::Error;
    decision.error = std::move(error);
    return decision;
}

TestHandlerDecision finishAssertion(const ExpectedAssertion &expected, ActualResult actual)
{
    TestHandlerDecision decision;
    decision.finished = true;
    decision.actual = actual;
    decision.assertion = evaluateAssertion(expected, actual);
    decision.status = decision.assertion->status;
    return decision;
}

TestHandlerDecision continueWith(TestHandlerStep step)
{
    TestHandlerDecision decision;
    decision.nextStep = std::move(step);
    return decision;
}

TestHandlerDecision waitFor(std::chrono::nanoseconds delay)
{
    TestHandlerDecision decision;
    decision.delay = std::max(delay, std::chrono::nanoseconds::zero());
    return decision;
}

std::optional<ActualResult> actualFromResult(
    const communication::ModbusRequestResult &result)
{
    if (result.success) {
        if (const auto *read = std::get_if<communication::ReadHoldingRegistersResult>(
                &*result.success)) {
            return ActualResult::registers(read->values);
        }
        if (const auto *write = std::get_if<communication::WriteSingleRegisterResult>(
                &*result.success)) {
            return ActualResult::registers({write->rawValue});
        }
    }
    if (result.error
        && result.error->category == communication::ErrorCategory::RemoteException
        && result.error->exceptionCode) {
        return ActualResult::exception(*result.error->exceptionCode);
    }
    return std::nullopt;
}

TestHandlerStep requestStep(const TestRequest &request, TestStepPurpose purpose)
{
    TestHandlerStep step;
    step.kind = request.function == ModbusFunction::ReadHoldingRegisters
        ? TestStepKind::Read : TestStepKind::Write;
    step.purpose = purpose;
    step.address = request.address;
    step.count = request.count;
    step.rawValue = request.rawValue;
    return step;
}

bool isExpectedResponseTimeout(const TestError &error)
{
    return error.code == TestErrorCode::RequestFailed && error.communicationError
        && error.communicationError->code == communication::ErrorCode::ResponseTimeout;
}

class SingleRequestHandler final : public ITestCaseHandler
{
public:
    SingleRequestHandler(TestCase testCase, TestHandlerStep step)
        : testCase_(std::move(testCase)), step_(std::move(step)) {}

    TestHandlerDecision start(const TestHandlerContext &) override
    {
        return continueWith(step_);
    }

    TestHandlerDecision handleResult(const communication::ModbusRequestResult &result,
                                     const TestHandlerContext &) override
    {
        if (const auto actual = actualFromResult(result)) {
            return finishAssertion(testCase_.expected, *actual);
        }
        return finishError(invariant(QStringLiteral("处理器收到不可判定的请求结果")));
    }

    TestHandlerDecision handleCommunicationError(TestError error, bool,
                                                  const TestHandlerContext &) override
    {
        return finishError(std::move(error));
    }

private:
    TestCase testCase_;
    TestHandlerStep step_;
};

class ExpectedTimeoutHandler final : public ITestCaseHandler
{
public:
    explicit ExpectedTimeoutHandler(TestCase testCase)
        : testCase_(std::move(testCase))
        , step_(requestStep(testCase_.request, TestStepPurpose::Read)) {}

    TestHandlerDecision start(const TestHandlerContext &) override
    {
        return continueWith(step_);
    }

    TestHandlerDecision handleResult(const communication::ModbusRequestResult &result,
                                     const TestHandlerContext &) override
    {
        if (const auto actual = actualFromResult(result)) {
            return finishAssertion(testCase_.expected, *actual);
        }
        return finishError(invariant(QStringLiteral("预期超时处理器收到不可判定结果")));
    }

    TestHandlerDecision handleCommunicationError(TestError error, bool,
                                                  const TestHandlerContext &) override
    {
        if (isExpectedResponseTimeout(error)) {
            return finishAssertion(testCase_.expected, ActualResult::responseTimeout());
        }
        return finishError(std::move(error));
    }

private:
    TestCase testCase_;
    TestHandlerStep step_;
};

class SequenceHandler final : public ITestCaseHandler
{
public:
    explicit SequenceHandler(TestCase testCase) : testCase_(std::move(testCase)) {}

    TestHandlerDecision start(const TestHandlerContext &context) override
    {
        return prepareCurrent(context, true);
    }

    TestHandlerDecision resume(const TestHandlerContext &context) override
    {
        return prepareCurrent(context, false);
    }

    TestHandlerDecision handleResult(const communication::ModbusRequestResult &result,
                                     const TestHandlerContext &context) override
    {
        const auto actual = actualFromResult(result);
        if (!actual) {
            return finishError(invariant(QStringLiteral("sequence 收到不可判定结果")));
        }
        const auto assertion = evaluateAssertion(current().expected, *actual);
        return completeStep(assertion.status, *actual, assertion, std::nullopt, context);
    }

    TestHandlerDecision handleCommunicationError(TestError error,
                                                  bool,
                                                  const TestHandlerContext &context) override
    {
        if (current().type == TestCaseType::ExpectTimeout
            && isExpectedResponseTimeout(error)) {
            const ActualResult actual = ActualResult::responseTimeout();
            const auto assertion = evaluateAssertion(current().expected, actual);
            return completeStep(assertion.status, actual, assertion, std::nullopt, context);
        }
        return completeStep(TestStatus::Error, std::nullopt, std::nullopt,
                            std::move(error), context);
    }

private:
    const SequenceStep &current() const
    {
        return testCase_.sequence.steps[stepIndex_];
    }

    TestHandlerDecision prepareCurrent(const TestHandlerContext &, bool allowDelay)
    {
        if (stepIndex_ < 0 || stepIndex_ >= testCase_.sequence.steps.size()
            || repetition_ < 0 || repetition_ >= testCase_.sequence.repeatCount) {
            return finishError(invariant(QStringLiteral("sequence 游标超出规范化模型")));
        }
        if (allowDelay && current().delayBefore > std::chrono::milliseconds::zero()) {
            return waitFor(current().delayBefore);
        }
        TestHandlerStep step = requestStep(current().request,
                                           TestStepPurpose::SequenceStep);
        step.logicalStepId = current().id;
        step.logicalStepIndex = stepIndex_;
        step.repetition = repetition_;
        step.retryOverride = current().retry;
        return continueWith(std::move(step));
    }

    TestCompositeStepResult makeOutcome(TestStatus status,
                                        std::optional<ActualResult> actual,
                                        std::optional<AssertionResult> assertion,
                                        std::optional<TestError> error) const
    {
        TestCompositeStepResult outcome;
        outcome.stepId = current().id;
        outcome.stepIndex = stepIndex_;
        outcome.repetition = repetition_;
        outcome.status = status;
        outcome.expected = current().expected;
        outcome.actual = std::move(actual);
        outcome.assertion = std::move(assertion);
        outcome.error = std::move(error);
        return outcome;
    }

    void mergeStatus(TestStatus status)
    {
        if (status == TestStatus::Error) {
            aggregateStatus_ = TestStatus::Error;
        } else if (status == TestStatus::Fail
                   && aggregateStatus_ != TestStatus::Error) {
            aggregateStatus_ = TestStatus::Fail;
        }
    }

    TestHandlerDecision completeStep(TestStatus status,
                                     std::optional<ActualResult> actual,
                                     std::optional<AssertionResult> assertion,
                                     std::optional<TestError> error,
                                     const TestHandlerContext &context)
    {
        TestHandlerDecision decision;
        decision.completedStep = makeOutcome(status, actual, assertion, error);
        mergeStatus(status);

        const bool fatal = error && (error->code == TestErrorCode::Aborted
                                     || error->code == TestErrorCode::CaseTimeout
                                     || error->code == TestErrorCode::RequestRejected
                                     || error->code == TestErrorCode::InvariantViolation);
        if (fatal || (status != TestStatus::Pass
                      && testCase_.sequence.failurePolicy
                             == SequenceFailurePolicy::StopOnFailure)) {
            decision.finished = true;
            decision.status = aggregateStatus_;
            decision.error = std::move(error);
            return decision;
        }

        ++stepIndex_;
        if (stepIndex_ >= testCase_.sequence.steps.size()) {
            stepIndex_ = 0;
            ++repetition_;
        }
        if (repetition_ >= testCase_.sequence.repeatCount) {
            decision.finished = true;
            decision.status = aggregateStatus_;
            return decision;
        }

        TestHandlerDecision next = prepareCurrent(context, true);
        next.completedStep = std::move(decision.completedStep);
        return next;
    }

    TestCase testCase_;
    qsizetype stepIndex_ = 0;
    int repetition_ = 0;
    TestStatus aggregateStatus_ = TestStatus::Pass;
};

class ConsistencyHandler final : public ITestCaseHandler
{
public:
    explicit ConsistencyHandler(TestCase testCase) : testCase_(std::move(testCase)) {}

    TestHandlerDecision start(const TestHandlerContext &) override
    {
        return continueWith(sampleStep());
    }

    TestHandlerDecision resume(const TestHandlerContext &) override
    {
        return continueWith(sampleStep());
    }

    TestHandlerDecision handleResult(const communication::ModbusRequestResult &result,
                                     const TestHandlerContext &) override
    {
        const auto actual = actualFromResult(result);
        if (!actual) {
            return finishError(invariant(QStringLiteral("consistency 收到不可判定结果")));
        }
        if (actual->type != ActualResultType::RegisterValues) {
            return finishAssertion(testCase_.expected, *actual);
        }
        samples_.append(actual->registerValues);
        if (samples_.size() >= testCase_.consistency.sampleCount) {
            return finishAssertion(testCase_.expected, ActualResult::samples(samples_));
        }
        return testCase_.consistency.interval > std::chrono::milliseconds::zero()
            ? waitFor(testCase_.consistency.interval)
            : continueWith(sampleStep());
    }

    TestHandlerDecision handleCommunicationError(TestError error, bool,
                                                  const TestHandlerContext &) override
    {
        return finishError(std::move(error));
    }

private:
    TestHandlerStep sampleStep() const
    {
        TestHandlerStep step = requestStep(testCase_.request,
                                           TestStepPurpose::ConsistencySample);
        step.logicalStepId = QStringLiteral("sample-%1").arg(samples_.size());
        step.logicalStepIndex = samples_.size();
        return step;
    }

    TestCase testCase_;
    QVector<QVector<quint16>> samples_;
};

class StabilityHandler final : public ITestCaseHandler
{
public:
    explicit StabilityHandler(TestCase testCase) : testCase_(std::move(testCase)) {}

    TestHandlerDecision start(const TestHandlerContext &) override
    {
        return continueWith(iterationStep());
    }

    TestHandlerDecision resume(const TestHandlerContext &context) override
    {
        if (context.caseElapsed >= testCase_.stability.duration) {
            return finishNatural();
        }
        return continueWith(iterationStep());
    }

    void requestAccepted(const TestHandlerStep &,
                         const TestHandlerContext &context) override
    {
        lastStart_ = context.caseElapsed;
    }

    TestHandlerDecision handleResult(const communication::ModbusRequestResult &result,
                                     const TestHandlerContext &context) override
    {
        ++stats_.total;
        if (result.success) {
            ++stats_.successes;
        } else {
            ++stats_.failures;
        }
        if (!recordRtt(result)) {
            return finishFatal(invariant(QStringLiteral("稳定性 RTT 累计溢出")));
        }
        return nextOrFinish(context);
    }

    TestHandlerDecision handleCommunicationError(TestError error,
                                                  bool requestWasAccepted,
                                                  const TestHandlerContext &context) override
    {
        if (!requestWasAccepted || error.code == TestErrorCode::Aborted
            || error.code == TestErrorCode::CaseTimeout) {
            return finishFatal(std::move(error));
        }
        const auto category = error.communicationError
            ? std::optional<communication::ErrorCategory>(error.communicationError->category)
            : std::nullopt;
        const bool responseTimeout = isExpectedResponseTimeout(error);
        const bool recoverableFailure = category
            && (*category == communication::ErrorCategory::Crc
                || *category == communication::ErrorCategory::Protocol);
        if (!responseTimeout && !recoverableFailure) {
            return finishFatal(std::move(error));
        }

        ++stats_.total;
        if (responseTimeout) {
            ++stats_.timeouts;
        } else {
            ++stats_.failures;
        }
        if (error.communicationError && error.communicationError->evidence) {
            communication::ModbusRequestResult partial;
            partial.evidence = *error.communicationError->evidence;
            if (!recordRtt(partial)) {
                return finishFatal(invariant(QStringLiteral("稳定性 RTT 累计溢出")));
            }
        } else {
            ++stats_.missingRttSamples;
        }
        return nextOrFinish(context);
    }

private:
    TestHandlerStep iterationStep() const
    {
        TestHandlerStep step = requestStep(testCase_.request,
                                           TestStepPurpose::StabilityIteration);
        step.logicalStepId = QStringLiteral("iteration-%1").arg(stats_.total);
        step.logicalStepIndex = static_cast<qsizetype>(stats_.total);
        return step;
    }

    bool recordRtt(const communication::ModbusRequestResult &result)
    {
        if (!result.evidence.rtt) {
            ++stats_.missingRttSamples;
            return true;
        }
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            *result.evidence.rtt).count();
        const quint64 value = static_cast<quint64>(std::max<qint64>(0, milliseconds));
        if (value > std::numeric_limits<quint64>::max() - rttTotalMs_) {
            return false;
        }
        rttTotalMs_ += value;
        ++stats_.validRttSamples;
        const qint64 signedValue = static_cast<qint64>(value);
        stats_.minimumRttMs = stats_.minimumRttMs
            ? std::min(*stats_.minimumRttMs, signedValue) : signedValue;
        stats_.maximumRttMs = stats_.maximumRttMs
            ? std::max(*stats_.maximumRttMs, signedValue) : signedValue;
        stats_.averageRttMs = static_cast<qint64>(rttTotalMs_ / stats_.validRttSamples);
        return true;
    }

    TestHandlerDecision nextOrFinish(const TestHandlerContext &context)
    {
        if (context.caseElapsed >= testCase_.stability.duration) {
            return finishNatural();
        }
        const auto nextStart = lastStart_ + testCase_.stability.interval;
        const auto delay = nextStart > context.caseElapsed
            ? nextStart - context.caseElapsed : std::chrono::nanoseconds::zero();
        if (delay == std::chrono::nanoseconds::zero()) {
            return continueWith(iterationStep());
        }
        const auto untilDuration = testCase_.stability.duration - context.caseElapsed;
        return waitFor(std::min(delay, untilDuration));
    }

    TestHandlerDecision finishNatural()
    {
        return finishAssertion(testCase_.expected,
                               ActualResult::stabilitySummary(stats_));
    }

    TestHandlerDecision finishFatal(TestError error)
    {
        TestHandlerDecision decision;
        decision.finished = true;
        decision.status = TestStatus::Error;
        decision.actual = ActualResult::stabilitySummary(stats_);
        decision.assertion = evaluateAssertion(testCase_.expected, *decision.actual);
        decision.error = std::move(error);
        return decision;
    }

    TestCase testCase_;
    StabilityStatistics stats_;
    std::chrono::nanoseconds lastStart_{0};
    quint64 rttTotalMs_ = 0;
};

class WriteAndVerifyHandler final : public ITestCaseHandler
{
public:
    explicit WriteAndVerifyHandler(TestCase testCase) : testCase_(std::move(testCase)) {}

    TestHandlerDecision start(const TestHandlerContext &) override
    {
        if (testCase_.request.readBeforeWrite) {
            stage_ = Stage::ReadBefore;
            return continueWith(readStep(TestStepPurpose::ReadBeforeWrite));
        }
        stage_ = Stage::Write;
        return continueWith(writeStep(testCase_.request.rawValue,
                                      TestStepPurpose::Write, false));
    }

    void requestAccepted(const TestHandlerStep &step,
                         const TestHandlerContext &) override
    {
        if (stage_ == Stage::Write && step.purpose == TestStepPurpose::Write) {
            writeAccepted_ = true;
        }
    }

    TestHandlerDecision handleResult(const communication::ModbusRequestResult &result,
                                     const TestHandlerContext &) override
    {
        switch (stage_) {
        case Stage::ReadBefore: {
            const auto actual = actualFromResult(result);
            if (!actual || actual->type != ActualResultType::RegisterValues
                || actual->registerValues.size() != 1) {
                return finishError(invariant(QStringLiteral("写前读取结果不是单寄存器")));
            }
            original_ = actual->registerValues.front();
            stage_ = Stage::Write;
            return continueWith(writeStep(testCase_.request.rawValue,
                                          TestStepPurpose::Write, false));
        }
        case Stage::Write: {
            const auto actual = actualFromResult(result);
            if (!actual) {
                return mainErrorOrCleanup(invariant(QStringLiteral("写响应结果不可判定")));
            }
            if (actual->type == ActualResultType::ModbusException) {
                storeMainAssertion(*actual);
                return cleanupOrFinish();
            }
            stage_ = Stage::Verify;
            return continueWith(readStep(TestStepPurpose::VerifyReadback));
        }
        case Stage::Verify: {
            const auto actual = actualFromResult(result);
            if (!actual) {
                return mainErrorOrCleanup(invariant(QStringLiteral("回读结果不可判定")));
            }
            storeMainAssertion(*actual);
            return cleanupOrFinish();
        }
        case Stage::Restore: {
            const auto actual = actualFromResult(result);
            if (!actual || actual->type != ActualResultType::RegisterValues) {
                TestError error{TestErrorCode::CleanupFailed,
                                QStringLiteral("恢复原值未获得正常写响应"), result.error};
                return finishWithCleanupError(std::move(error));
            }
            return finishStored();
        }
        case Stage::Done:
            break;
        }
        return finishError(invariant(QStringLiteral("处理器状态与请求结果不匹配")));
    }

    TestHandlerDecision handleCommunicationError(TestError error,
                                                  bool requestWasAccepted,
                                                  const TestHandlerContext &) override
    {
        if (stage_ == Stage::Restore) {
            error.code = TestErrorCode::CleanupFailed;
            return finishWithCleanupError(std::move(error));
        }
        if (stage_ == Stage::Write && requestWasAccepted) {
            writeAccepted_ = true;
        }
        mainStatus_ = TestStatus::Error;
        mainError_ = std::move(error);
        return cleanupOrFinish();
    }

private:
    enum class Stage { ReadBefore, Write, Verify, Restore, Done };

    TestHandlerStep readStep(TestStepPurpose purpose) const
    {
        TestHandlerStep step;
        step.kind = TestStepKind::Read;
        step.purpose = purpose;
        step.address = testCase_.request.address;
        return step;
    }

    TestHandlerStep writeStep(quint16 value, TestStepPurpose purpose, bool cleanup) const
    {
        TestHandlerStep step;
        step.kind = TestStepKind::Write;
        step.purpose = purpose;
        step.address = testCase_.request.address;
        step.rawValue = value;
        step.cleanup = cleanup;
        return step;
    }

    void storeMainAssertion(ActualResult actual)
    {
        mainActual_ = actual;
        mainAssertion_ = evaluateAssertion(testCase_.expected, actual);
        mainStatus_ = mainAssertion_->status;
    }

    TestHandlerDecision mainErrorOrCleanup(TestError error)
    {
        mainStatus_ = TestStatus::Error;
        mainError_ = std::move(error);
        return cleanupOrFinish();
    }

    TestHandlerDecision cleanupOrFinish()
    {
        if (testCase_.request.restoreOriginal && original_ && writeAccepted_) {
            stage_ = Stage::Restore;
            return continueWith(writeStep(*original_, TestStepPurpose::RestoreOriginal, true));
        }
        return finishStored();
    }

    TestHandlerDecision finishStored()
    {
        stage_ = Stage::Done;
        TestHandlerDecision decision;
        decision.finished = true;
        decision.status = mainStatus_;
        decision.actual = mainActual_;
        decision.assertion = mainAssertion_;
        decision.error = mainError_;
        return decision;
    }

    TestHandlerDecision finishWithCleanupError(TestError error)
    {
        auto decision = finishStored();
        decision.status = TestStatus::Error;
        decision.cleanupError = std::move(error);
        return decision;
    }

    TestCase testCase_;
    Stage stage_ = Stage::Done;
    std::optional<quint16> original_;
    bool writeAccepted_ = false;
    TestStatus mainStatus_ = TestStatus::Error;
    std::optional<ActualResult> mainActual_;
    std::optional<AssertionResult> mainAssertion_;
    std::optional<TestError> mainError_;
};

} // namespace

TestHandlerDecision ITestCaseHandler::resume(const TestHandlerContext &)
{
    return finishError(invariant(QStringLiteral("处理器不支持延迟恢复")));
}

void TestHandlerRegistry::registerFactory(TestCaseType type, Factory factory)
{
    factories_.insert(type, std::move(factory));
}

std::unique_ptr<ITestCaseHandler> TestHandlerRegistry::create(
    const TestCase &testCase) const
{
    const auto iterator = factories_.constFind(testCase.type);
    return iterator == factories_.cend() ? nullptr : (*iterator)(testCase);
}

TestHandlerRegistry TestHandlerRegistry::withBasicHandlers()
{
    TestHandlerRegistry registry;
    registry.registerFactory(TestCaseType::ReadRegisters, [](const TestCase &testCase) {
        return std::make_unique<SingleRequestHandler>(
            testCase, requestStep(testCase.request, TestStepPurpose::Read));
    });
    registry.registerFactory(TestCaseType::WriteRegister, [](const TestCase &testCase) {
        return std::make_unique<SingleRequestHandler>(
            testCase, requestStep(testCase.request, TestStepPurpose::Write));
    });
    registry.registerFactory(TestCaseType::ExpectException, [](const TestCase &testCase) {
        const auto purpose = testCase.request.function == ModbusFunction::ReadHoldingRegisters
            ? TestStepPurpose::Read : TestStepPurpose::Write;
        return std::make_unique<SingleRequestHandler>(
            testCase, requestStep(testCase.request, purpose));
    });
    registry.registerFactory(TestCaseType::WriteAndVerify, [](const TestCase &testCase) {
        return std::make_unique<WriteAndVerifyHandler>(testCase);
    });
    registry.registerFactory(TestCaseType::ExpectTimeout, [](const TestCase &testCase) {
        return std::make_unique<ExpectedTimeoutHandler>(testCase);
    });
    registry.registerFactory(TestCaseType::Sequence, [](const TestCase &testCase) {
        return std::make_unique<SequenceHandler>(testCase);
    });
    registry.registerFactory(TestCaseType::Consistency, [](const TestCase &testCase) {
        return std::make_unique<ConsistencyHandler>(testCase);
    });
    registry.registerFactory(TestCaseType::Stability, [](const TestCase &testCase) {
        return std::make_unique<StabilityHandler>(testCase);
    });
    return registry;
}

} // namespace oms555tv::testing
