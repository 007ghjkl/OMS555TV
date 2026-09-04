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
};

struct TestHandlerDecision {
    std::optional<TestHandlerStep> nextStep;
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
    [[nodiscard]] virtual TestHandlerDecision start() = 0;
    virtual void requestAccepted(const TestHandlerStep &) {}
    [[nodiscard]] virtual TestHandlerDecision handleResult(
        const communication::ModbusRequestResult &result) = 0;
    [[nodiscard]] virtual TestHandlerDecision handleCommunicationError(
        TestError error, bool requestWasAccepted) = 0;
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
