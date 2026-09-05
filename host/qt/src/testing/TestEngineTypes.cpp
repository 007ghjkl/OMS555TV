#include "testing/TestEngineTypes.h"

namespace oms555tv::testing {

QString testEngineStateName(TestEngineState state)
{
    switch (state) {
    case TestEngineState::Idle: return QStringLiteral("IDLE");
    case TestEngineState::Running: return QStringLiteral("RUNNING");
    case TestEngineState::Aborting: return QStringLiteral("ABORTING");
    case TestEngineState::Releasing: return QStringLiteral("RELEASING");
    }
    return {};
}

QString testStepPurposeName(TestStepPurpose purpose)
{
    switch (purpose) {
    case TestStepPurpose::Read: return QStringLiteral("read");
    case TestStepPurpose::Write: return QStringLiteral("write");
    case TestStepPurpose::ReadBeforeWrite: return QStringLiteral("read_before_write");
    case TestStepPurpose::VerifyReadback: return QStringLiteral("verify_readback");
    case TestStepPurpose::RestoreOriginal: return QStringLiteral("restore_original");
    case TestStepPurpose::SequenceStep: return QStringLiteral("sequence_step");
    case TestStepPurpose::ConsistencySample: return QStringLiteral("consistency_sample");
    case TestStepPurpose::StabilityIteration: return QStringLiteral("stability_iteration");
    }
    return {};
}

QString testEvidenceRetentionPolicyName(const TestEvidenceRetentionPolicy policy)
{
    switch (policy) {
    case TestEvidenceRetentionPolicy::Complete: return QStringLiteral("complete");
    case TestEvidenceRetentionPolicy::BoundedRepresentative:
        return QStringLiteral("bounded_representative");
    }
    return {};
}

QString testSkipReasonName(TestSkipReason reason)
{
    switch (reason) {
    case TestSkipReason::Disabled: return QStringLiteral("disabled");
    case TestSkipReason::NotSelected: return QStringLiteral("not_selected");
    case TestSkipReason::UserSelected: return QStringLiteral("user_selected");
    case TestSkipReason::Aborted: return QStringLiteral("aborted");
    }
    return {};
}

QString testErrorCodeName(TestErrorCode code)
{
    switch (code) {
    case TestErrorCode::InvalidState: return QStringLiteral("InvalidState");
    case TestErrorCode::AlreadyRunning: return QStringLiteral("AlreadyRunning");
    case TestErrorCode::UnknownCaseId: return QStringLiteral("UnknownCaseId");
    case TestErrorCode::HandlerNotFound: return QStringLiteral("HandlerNotFound");
    case TestErrorCode::RequestRejected: return QStringLiteral("RequestRejected");
    case TestErrorCode::RequestFailed: return QStringLiteral("RequestFailed");
    case TestErrorCode::CaseTimeout: return QStringLiteral("CaseTimeout");
    case TestErrorCode::Aborted: return QStringLiteral("Aborted");
    case TestErrorCode::CleanupFailed: return QStringLiteral("CleanupFailed");
    case TestErrorCode::StopTestingRejected: return QStringLiteral("StopTestingRejected");
    case TestErrorCode::StateMismatch: return QStringLiteral("StateMismatch");
    case TestErrorCode::LoggingFailed: return QStringLiteral("LoggingFailed");
    case TestErrorCode::InvariantViolation: return QStringLiteral("InvariantViolation");
    }
    return {};
}

} // namespace oms555tv::testing
