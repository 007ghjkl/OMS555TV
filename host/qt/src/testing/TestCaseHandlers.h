#pragma once

#include "testing/TestEngineTypes.h"

#include <QHash>

#include <functional>
#include <memory>
#include <optional>

namespace oms555tv::testing {

struct TestHandlerStep {
    TestStepKind kind = TestStepKind::Read;
    TestStepPurpose purpose = TestStepPurpose::Read;
    device::PduAddress address{0};
    quint16 count = 1;
    quint16 rawValue = 0;
    bool cleanup = false;
    QString logicalStepId;
    qsizetype logicalStepIndex = -1;
    int repetition = 0;
    std::optional<RetryPolicy> retryOverride;
};

struct TestHandlerContext {
    std::chrono::nanoseconds caseElapsed{0};
    std::chrono::nanoseconds monotonicNow{0};
    QDateTime utcNow;
};

struct TestHandlerDecision {
    std::optional<TestHandlerStep> nextStep;
    std::optional<std::chrono::nanoseconds> delay;
    std::optional<TestCompositeStepResult> completedStep;
    bool finished = false;
    TestStatus status = TestStatus::Running;
    std::optional<ActualResult> actual;
    std::optional<AssertionResult> assertion;
    std::optional<TestError> error;
    std::optional<TestError> cleanupError;
};

class ITestCaseHandler
{
public:
    virtual ~ITestCaseHandler() = default;
    [[nodiscard]] virtual TestHandlerDecision start(const TestHandlerContext &context) = 0;
    [[nodiscard]] virtual TestHandlerDecision resume(const TestHandlerContext &context);
    virtual void requestAccepted(const TestHandlerStep &, const TestHandlerContext &) {}
    [[nodiscard]] virtual TestHandlerDecision handleResult(
        const communication::ModbusRequestResult &result,
        const TestHandlerContext &context) = 0;
    [[nodiscard]] virtual TestHandlerDecision handleCommunicationError(
        TestError error, bool requestWasAccepted,
        const TestHandlerContext &context) = 0;
};

class TestHandlerRegistry final
{
public:
    using Factory = std::function<std::unique_ptr<ITestCaseHandler>(const TestCase &)>;

    void registerFactory(TestCaseType type, Factory factory);
    [[nodiscard]] std::unique_ptr<ITestCaseHandler> create(const TestCase &testCase) const;
    [[nodiscard]] static TestHandlerRegistry withBasicHandlers();

private:
    QHash<TestCaseType, Factory> factories_;
};

} // namespace oms555tv::testing
