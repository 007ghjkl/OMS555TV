#include "testing/TestCaseHandlers.h"

#include <variant>

namespace oms555tv::testing {
namespace {

TestHandlerDecision finishAssertion(const TestCase &testCase, ActualResult actual)
{
    TestHandlerDecision decision;
    decision.finished = true;
    decision.actual = actual;
    decision.assertion = evaluateAssertion(testCase.expected, actual);
    decision.status = decision.assertion->status;
    return decision;
}

TestHandlerDecision finishError(TestError error)
{
    TestHandlerDecision decision;
    decision.finished = true;
    decision.status = TestStatus::Error;
    decision.error = std::move(error);
    return decision;
}

TestError invariant(QString diagnostic)
{
    return {TestErrorCode::InvariantViolation, std::move(diagnostic), std::nullopt};
}

TestHandlerDecision continueWith(TestHandlerStep step)
{
    TestHandlerDecision decision;
    decision.nextStep = step;
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

class SingleRequestHandler final : public ITestCaseHandler
{
public:
    SingleRequestHandler(TestCase testCase, TestHandlerStep step)
        : testCase_(std::move(testCase)), step_(step) {}

    TestHandlerDecision start() override { return continueWith(step_); }

    TestHandlerDecision handleResult(
        const communication::ModbusRequestResult &result) override
    {
        if (const auto actual = actualFromResult(result)) {
            return finishAssertion(testCase_, *actual);
        }
        return finishError(invariant(QStringLiteral("处理器收到不可判定的请求结果")));
    }

    TestHandlerDecision handleCommunicationError(TestError error, bool) override
    {
        return finishError(std::move(error));
    }

private:
    TestCase testCase_;
    TestHandlerStep step_;
};

class WriteAndVerifyHandler final : public ITestCaseHandler
{
public:
    explicit WriteAndVerifyHandler(TestCase testCase) : testCase_(std::move(testCase)) {}

    TestHandlerDecision start() override
    {
        if (testCase_.request.readBeforeWrite) {
            stage_ = Stage::ReadBefore;
            return continueWith(readStep(TestStepPurpose::ReadBeforeWrite));
        }
        stage_ = Stage::Write;
        return continueWith(writeStep(testCase_.request.rawValue,
                                      TestStepPurpose::Write, false));
    }

    void requestAccepted(const TestHandlerStep &step) override
    {
        if (stage_ == Stage::Write && step.purpose == TestStepPurpose::Write) {
            writeAccepted_ = true;
        }
    }

    TestHandlerDecision handleResult(
        const communication::ModbusRequestResult &result) override
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
                                                  bool requestWasAccepted) override
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
        return {TestStepKind::Read, purpose, testCase_.request.address, 1, 0, false};
    }

    TestHandlerStep writeStep(quint16 value, TestStepPurpose purpose, bool cleanup) const
    {
        return {TestStepKind::Write, purpose, testCase_.request.address, 1, value, cleanup};
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
        TestHandlerStep step{TestStepKind::Read, TestStepPurpose::Read,
                             testCase.request.address, testCase.request.count};
        return std::make_unique<SingleRequestHandler>(testCase, step);
    });
    registry.registerFactory(TestCaseType::WriteRegister, [](const TestCase &testCase) {
        TestHandlerStep step{TestStepKind::Write, TestStepPurpose::Write,
                             testCase.request.address, 1, testCase.request.rawValue};
        return std::make_unique<SingleRequestHandler>(testCase, step);
    });
    registry.registerFactory(TestCaseType::ExpectException, [](const TestCase &testCase) {
        const bool read = testCase.request.function == ModbusFunction::ReadHoldingRegisters;
        TestHandlerStep step{read ? TestStepKind::Read : TestStepKind::Write,
                             read ? TestStepPurpose::Read : TestStepPurpose::Write,
                             testCase.request.address, testCase.request.count,
                             testCase.request.rawValue};
        return std::make_unique<SingleRequestHandler>(testCase, step);
    });
    registry.registerFactory(TestCaseType::WriteAndVerify, [](const TestCase &testCase) {
        return std::make_unique<WriteAndVerifyHandler>(testCase);
    });
    return registry;
}

} // namespace oms555tv::testing
