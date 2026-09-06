#include "report/ReportTypes.h"

namespace oms555tv::report {

QString metadataSourceName(const MetadataSource source)
{
    switch (source) {
    case MetadataSource::SystemObserved: return QStringLiteral("system-observed");
    case MetadataSource::SuiteConfigured: return QStringLiteral("suite-configured");
    case MetadataSource::OperatorEntered: return QStringLiteral("operator-entered");
    case MetadataSource::Unavailable: return QStringLiteral("unavailable");
    }
    return QStringLiteral("unavailable");
}

QString reportErrorCodeName(const ReportErrorCode code)
{
    switch (code) {
    case ReportErrorCode::MissingSuite: return QStringLiteral("MissingSuite");
    case ReportErrorCode::InvalidRunId: return QStringLiteral("InvalidRunId");
    case ReportErrorCode::NonTerminalStatus: return QStringLiteral("NonTerminalStatus");
    case ReportErrorCode::SuiteResultCountMismatch:
        return QStringLiteral("SuiteResultCountMismatch");
    case ReportErrorCode::CaseIdentityMismatch:
        return QStringLiteral("CaseIdentityMismatch");
    case ReportErrorCode::SuiteStatusMismatch: return QStringLiteral("SuiteStatusMismatch");
    case ReportErrorCode::InvalidTimestamp: return QStringLiteral("InvalidTimestamp");
    case ReportErrorCode::InvalidDuration: return QStringLiteral("InvalidDuration");
    case ReportErrorCode::InvalidTimeZone: return QStringLiteral("InvalidTimeZone");
    case ReportErrorCode::InvalidMetadata: return QStringLiteral("InvalidMetadata");
    case ReportErrorCode::SummaryOverflow: return QStringLiteral("SummaryOverflow");
    case ReportErrorCode::SummaryInvariantViolation:
        return QStringLiteral("SummaryInvariantViolation");
    case ReportErrorCode::EvidenceInvariantViolation:
        return QStringLiteral("EvidenceInvariantViolation");
    case ReportErrorCode::InvalidArtifact: return QStringLiteral("InvalidArtifact");
    case ReportErrorCode::InconsistentFirmwareVersion:
        return QStringLiteral("InconsistentFirmwareVersion");
    case ReportErrorCode::InvalidGenerationTime:
        return QStringLiteral("InvalidGenerationTime");
    case ReportErrorCode::InvalidOutputPath: return QStringLiteral("InvalidOutputPath");
    case ReportErrorCode::TargetAlreadyExists:
        return QStringLiteral("TargetAlreadyExists");
    case ReportErrorCode::OutputOpenFailed: return QStringLiteral("OutputOpenFailed");
    case ReportErrorCode::OutputWriteFailed: return QStringLiteral("OutputWriteFailed");
    case ReportErrorCode::OutputCommitFailed: return QStringLiteral("OutputCommitFailed");
    case ReportErrorCode::NoCompletedResult: return QStringLiteral("NoCompletedResult");
    case ReportErrorCode::TestRunInProgress: return QStringLiteral("TestRunInProgress");
    case ReportErrorCode::MissingRequiredMetadata:
        return QStringLiteral("MissingRequiredMetadata");
    case ReportErrorCode::ExportInProgress: return QStringLiteral("ExportInProgress");
    case ReportErrorCode::InvalidSessionLogArtifact:
        return QStringLiteral("InvalidSessionLogArtifact");
    }
    return QStringLiteral("SummaryInvariantViolation");
}

} // namespace oms555tv::report
