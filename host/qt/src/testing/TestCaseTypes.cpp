#include "testing/TestCaseTypes.h"

namespace oms555tv::testing {

QString testStatusName(const TestStatus status)
{
    switch (status) {
    case TestStatus::NotRun: return QStringLiteral("NOT_RUN");
    case TestStatus::Running: return QStringLiteral("RUNNING");
    case TestStatus::Pass: return QStringLiteral("PASS");
    case TestStatus::Fail: return QStringLiteral("FAIL");
    case TestStatus::Skipped: return QStringLiteral("SKIPPED");
    case TestStatus::Error: return QStringLiteral("ERROR");
    }
    return QStringLiteral("ERROR");
}

QString testCaseTypeName(const TestCaseType type)
{
    switch (type) {
    case TestCaseType::ReadRegister: return QStringLiteral("read_register");
    case TestCaseType::ReadRegisters: return QStringLiteral("read_registers");
    case TestCaseType::WriteRegister: return QStringLiteral("write_register");
    case TestCaseType::WriteAndVerify: return QStringLiteral("write_and_verify");
    case TestCaseType::ExpectException: return QStringLiteral("expect_exception");
    case TestCaseType::Sequence: return QStringLiteral("sequence");
    case TestCaseType::ExpectTimeout: return QStringLiteral("expect_timeout");
    case TestCaseType::Consistency: return QStringLiteral("consistency");
    case TestCaseType::Stability: return QStringLiteral("stability");
    case TestCaseType::GuidedRecovery: return QStringLiteral("guided_recovery");
    }
    return {};
}

QString assertionTypeName(const AssertionType type)
{
    switch (type) {
    case AssertionType::Equals: return QStringLiteral("equals");
    case AssertionType::Range: return QStringLiteral("range");
    case AssertionType::RegisterSequence: return QStringLiteral("register_sequence");
    case AssertionType::BitMask: return QStringLiteral("bitmask");
    case AssertionType::ModbusException: return QStringLiteral("modbus_exception");
    case AssertionType::Elements: return QStringLiteral("elements");
    case AssertionType::ResponseTimeout: return QStringLiteral("response_timeout");
    case AssertionType::UInt32: return QStringLiteral("uint32");
    case AssertionType::StabilitySummary: return QStringLiteral("stability_summary");
    }
    return {};
}

QString configErrorCodeName(const ConfigErrorCode code)
{
    switch (code) {
    case ConfigErrorCode::InvalidUtf8: return QStringLiteral("InvalidUtf8");
    case ConfigErrorCode::JsonSyntax: return QStringLiteral("JsonSyntax");
    case ConfigErrorCode::RootNotObject: return QStringLiteral("RootNotObject");
    case ConfigErrorCode::UnknownSchemaVersion: return QStringLiteral("UnknownSchemaVersion");
    case ConfigErrorCode::MissingField: return QStringLiteral("MissingField");
    case ConfigErrorCode::UnknownField: return QStringLiteral("UnknownField");
    case ConfigErrorCode::WrongType: return QStringLiteral("WrongType");
    case ConfigErrorCode::EmptyString: return QStringLiteral("EmptyString");
    case ConfigErrorCode::InvalidIdentifier: return QStringLiteral("InvalidIdentifier");
    case ConfigErrorCode::OutOfRange: return QStringLiteral("OutOfRange");
    case ConfigErrorCode::UnknownCaseType: return QStringLiteral("UnknownCaseType");
    case ConfigErrorCode::UnknownStepType: return QStringLiteral("UnknownStepType");
    case ConfigErrorCode::UnknownAction: return QStringLiteral("UnknownAction");
    case ConfigErrorCode::UnknownAssertionType: return QStringLiteral("UnknownAssertionType");
    case ConfigErrorCode::DuplicateId: return QStringLiteral("DuplicateId");
    case ConfigErrorCode::DuplicateTag: return QStringLiteral("DuplicateTag");
    case ConfigErrorCode::AddressRangeOverflow: return QStringLiteral("AddressRangeOverflow");
    case ConfigErrorCode::InvalidCombination: return QStringLiteral("InvalidCombination");
    case ConfigErrorCode::DuplicateIndex: return QStringLiteral("DuplicateIndex");
    case ConfigErrorCode::BudgetOverflow: return QStringLiteral("BudgetOverflow");
    case ConfigErrorCode::StatisticsConflict: return QStringLiteral("StatisticsConflict");
    }
    return QStringLiteral("JsonSyntax");
}

QString executionEnvironmentName(const ExecutionEnvironment environment)
{
    switch (environment) {
    case ExecutionEnvironment::Both: return QStringLiteral("both");
    case ExecutionEnvironment::RealRs485: return QStringLiteral("real_rs485");
    case ExecutionEnvironment::Fake: return QStringLiteral("fake");
    }
    return {};
}

QString guidedStepTypeName(const GuidedStepType type)
{
    switch (type) {
    case GuidedStepType::OperatorPrompt: return QStringLiteral("operator_prompt");
    case GuidedStepType::ObserveOutage: return QStringLiteral("observe_outage");
    case GuidedStepType::ObserveRecovery: return QStringLiteral("observe_recovery");
    }
    return {};
}

QString guidedPromptPurposeName(const GuidedPromptPurpose purpose)
{
    switch (purpose) {
    case GuidedPromptPurpose::DisconnectRs485: return QStringLiteral("disconnect_rs485");
    case GuidedPromptPurpose::ReconnectRs485: return QStringLiteral("reconnect_rs485");
    }
    return {};
}

QString guidedOperatorActionName(const GuidedOperatorAction action)
{
    switch (action) {
    case GuidedOperatorAction::Confirm: return QStringLiteral("confirm");
    case GuidedOperatorAction::Cancel: return QStringLiteral("cancel");
    }
    return {};
}

QString guidedObservationTargetName(const GuidedObservationTarget target)
{
    switch (target) {
    case GuidedObservationTarget::ConsecutiveResponseTimeouts:
        return QStringLiteral("consecutive_response_timeouts");
    case GuidedObservationTarget::ConsecutiveValidResponses:
        return QStringLiteral("consecutive_valid_responses");
    }
    return {};
}

} // namespace oms555tv::testing
