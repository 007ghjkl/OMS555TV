#include "report/ReportModelBuilder.h"

#include "device/RegisterMap.h"

#include <algorithm>
#include <limits>
#include <variant>

namespace oms555tv::report {
namespace {

using communication::ErrorCategory;
using communication::ErrorCode;
using testing::ActualResult;
using testing::AssertionDifference;
using testing::AssertionDifferenceCode;
using testing::AssertionResult;
using testing::ExpectedAssertion;
using testing::TestCase;
using testing::TestCaseResult;
using testing::TestStatus;

constexpr qsizetype maximumSuiteTextLength = 512;
constexpr qsizetype maximumDeviceLength = 256;
constexpr qsizetype maximumEnvironmentLength = 512;
constexpr qsizetype maximumVersionLength = 128;
constexpr qsizetype maximumFirmwareLength = 64;
constexpr qsizetype maximumTesterLength = 128;
constexpr qsizetype maximumSessionLength = 128;

bool terminal(const TestStatus status) noexcept
{
    return status == TestStatus::Pass || status == TestStatus::Fail
        || status == TestStatus::Error || status == TestStatus::Skipped;
}

void appendError(QVector<ReportError> &errors, const ReportErrorCode code,
                 QString path, QString diagnostic)
{
    errors.append({code, std::move(path), std::move(diagnostic)});
}

bool textValid(const QString &value, const qsizetype maximumLength) noexcept
{
    return value.size() <= maximumLength && !value.contains(QChar(u'\0'));
}

bool requiredTextValid(const QString &value, const qsizetype maximumLength) noexcept
{
    return !value.trimmed().isEmpty() && textValid(value, maximumLength);
}

ReportMetadataValue availableValue(QString value, const MetadataSource source)
{
    return {std::move(value), source, true, {}};
}

ReportMetadataValue unavailableValue(QString missingDisplay)
{
    return {{}, MetadataSource::Unavailable, false, std::move(missingDisplay)};
}

QString categoryName(const ErrorCategory category)
{
    switch (category) {
    case ErrorCategory::InvalidParameter: return QStringLiteral("InvalidParameter");
    case ErrorCategory::Connection: return QStringLiteral("Connection");
    case ErrorCategory::Serial: return QStringLiteral("Serial");
    case ErrorCategory::Queue: return QStringLiteral("Queue");
    case ErrorCategory::Ownership: return QStringLiteral("Ownership");
    case ErrorCategory::Timeout: return QStringLiteral("Timeout");
    case ErrorCategory::Cancelled: return QStringLiteral("Cancelled");
    case ErrorCategory::Crc: return QStringLiteral("Crc");
    case ErrorCategory::Protocol: return QStringLiteral("Protocol");
    case ErrorCategory::RemoteException: return QStringLiteral("RemoteException");
    case ErrorCategory::InternalState: return QStringLiteral("InternalState");
    }
    return QStringLiteral("InternalState");
}

QString communicationCodeName(const ErrorCode code)
{
    switch (code) {
    case ErrorCode::EmptyPortName: return QStringLiteral("EmptyPortName");
    case ErrorCode::UnsupportedConfiguration: return QStringLiteral("UnsupportedConfiguration");
    case ErrorCode::InvalidServerAddress: return QStringLiteral("InvalidServerAddress");
    case ErrorCode::InvalidTimeout: return QStringLiteral("InvalidTimeout");
    case ErrorCode::InvalidQuantity: return QStringLiteral("InvalidQuantity");
    case ErrorCode::AddressRangeOverflow: return QStringLiteral("AddressRangeOverflow");
    case ErrorCode::NotConnected: return QStringLiteral("NotConnected");
    case ErrorCode::AlreadyOpen: return QStringLiteral("AlreadyOpen");
    case ErrorCode::OpenFailed: return QStringLiteral("OpenFailed");
    case ErrorCode::ClosedByPeer: return QStringLiteral("ClosedByPeer");
    case ErrorCode::Closing: return QStringLiteral("Closing");
    case ErrorCode::SerialConfigurationRejected:
        return QStringLiteral("SerialConfigurationRejected");
    case ErrorCode::SerialReadFailed: return QStringLiteral("SerialReadFailed");
    case ErrorCode::SerialWriteFailed: return QStringLiteral("SerialWriteFailed");
    case ErrorCode::PartialWrite: return QStringLiteral("PartialWrite");
    case ErrorCode::ResourceError: return QStringLiteral("ResourceError");
    case ErrorCode::PermissionError: return QStringLiteral("PermissionError");
    case ErrorCode::QueueFull: return QStringLiteral("QueueFull");
    case ErrorCode::NotAcceptingRequests: return QStringLiteral("NotAcceptingRequests");
    case ErrorCode::OwnerMismatch: return QStringLiteral("OwnerMismatch");
    case ErrorCode::OwnershipTransitionInProgress:
        return QStringLiteral("OwnershipTransitionInProgress");
    case ErrorCode::ResponseTimeout: return QStringLiteral("ResponseTimeout");
    case ErrorCode::CancelledByCaller: return QStringLiteral("CancelledByCaller");
    case ErrorCode::CancelledByHandoff: return QStringLiteral("CancelledByHandoff");
    case ErrorCode::CancelledByClose: return QStringLiteral("CancelledByClose");
    case ErrorCode::ResponseCrcMismatch: return QStringLiteral("ResponseCrcMismatch");
    case ErrorCode::ResponseCrcMissing: return QStringLiteral("ResponseCrcMissing");
    case ErrorCode::WrongServerAddress: return QStringLiteral("WrongServerAddress");
    case ErrorCode::UnexpectedFunction: return QStringLiteral("UnexpectedFunction");
    case ErrorCode::InvalidByteCount: return QStringLiteral("InvalidByteCount");
    case ErrorCode::MalformedResponse: return QStringLiteral("MalformedResponse");
    case ErrorCode::WriteEchoMismatch: return QStringLiteral("WriteEchoMismatch");
    case ErrorCode::UnexpectedTrailingBytes:
        return QStringLiteral("UnexpectedTrailingBytes");
    case ErrorCode::UnsolicitedData: return QStringLiteral("UnsolicitedData");
    case ErrorCode::ModbusIllegalFunction: return QStringLiteral("ModbusIllegalFunction");
    case ErrorCode::ModbusIllegalDataAddress:
        return QStringLiteral("ModbusIllegalDataAddress");
    case ErrorCode::ModbusIllegalDataValue:
        return QStringLiteral("ModbusIllegalDataValue");
    case ErrorCode::ModbusUnknownException: return QStringLiteral("ModbusUnknownException");
    case ErrorCode::InvalidState: return QStringLiteral("InvalidState");
    case ErrorCode::IdExhausted: return QStringLiteral("IdExhausted");
    case ErrorCode::InvariantViolation: return QStringLiteral("InvariantViolation");
    case ErrorCode::WorkerUnavailable: return QStringLiteral("WorkerUnavailable");
    case ErrorCode::RequestNotFoundOrCompleted:
        return QStringLiteral("RequestNotFoundOrCompleted");
    case ErrorCode::FakeScriptMismatch: return QStringLiteral("FakeScriptMismatch");
    }
    return QStringLiteral("InvariantViolation");
}

QString requestStateName(const communication::RequestState state)
{
    switch (state) {
    case communication::RequestState::Queued: return QStringLiteral("Queued");
    case communication::RequestState::InFlight: return QStringLiteral("InFlight");
    case communication::RequestState::Succeeded: return QStringLiteral("Succeeded");
    case communication::RequestState::Failed: return QStringLiteral("Failed");
    case communication::RequestState::Cancelled: return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Failed");
}

QString crcName(const communication::CrcStatus status)
{
    switch (status) {
    case communication::CrcStatus::NotAvailable: return QStringLiteral("N/A");
    case communication::CrcStatus::Valid: return QStringLiteral("Valid");
    case communication::CrcStatus::Invalid: return QStringLiteral("Invalid");
    }
    return QStringLiteral("N/A");
}

QString assertionDifferenceName(const AssertionDifferenceCode code)
{
    switch (code) {
    case AssertionDifferenceCode::ActualTypeMismatch: return QStringLiteral("ActualTypeMismatch");
    case AssertionDifferenceCode::ValueMismatch: return QStringLiteral("ValueMismatch");
    case AssertionDifferenceCode::OutOfRange: return QStringLiteral("OutOfRange");
    case AssertionDifferenceCode::SequenceLengthMismatch:
        return QStringLiteral("SequenceLengthMismatch");
    case AssertionDifferenceCode::SequenceValueMismatch:
        return QStringLiteral("SequenceValueMismatch");
    case AssertionDifferenceCode::BitMaskMismatch: return QStringLiteral("BitMaskMismatch");
    case AssertionDifferenceCode::ExceptionCodeMismatch:
        return QStringLiteral("ExceptionCodeMismatch");
    case AssertionDifferenceCode::ElementCountMismatch:
        return QStringLiteral("ElementCountMismatch");
    case AssertionDifferenceCode::UInt32WordCountMismatch:
        return QStringLiteral("UInt32WordCountMismatch");
    case AssertionDifferenceCode::UInt32OutOfRange: return QStringLiteral("UInt32OutOfRange");
    case AssertionDifferenceCode::UInt32MonotonicityMismatch:
        return QStringLiteral("UInt32MonotonicityMismatch");
    case AssertionDifferenceCode::StabilityInvariantMismatch:
        return QStringLiteral("StabilityInvariantMismatch");
    case AssertionDifferenceCode::StabilityCountMismatch:
        return QStringLiteral("StabilityCountMismatch");
    case AssertionDifferenceCode::FailureRateExceeded:
        return QStringLiteral("FailureRateExceeded");
    case AssertionDifferenceCode::RttMissing: return QStringLiteral("RttMissing");
    case AssertionDifferenceCode::RttOutOfRange: return QStringLiteral("RttOutOfRange");
    }
    return QStringLiteral("ActualTypeMismatch");
}

QString representationName(const testing::ValueRepresentation value)
{
    return value == testing::ValueRepresentation::Int16
        ? QStringLiteral("int16") : QStringLiteral("uint16");
}

QString uint32ComparisonName(const testing::UInt32Comparison value)
{
    switch (value) {
    case testing::UInt32Comparison::Range: return QStringLiteral("range");
    case testing::UInt32Comparison::NonDecreasing: return QStringLiteral("non_decreasing");
    case testing::UInt32Comparison::StrictlyIncreasing:
        return QStringLiteral("strictly_increasing");
    }
    return QStringLiteral("range");
}

ReportExpectedElement mapExpectedElement(const testing::ElementAssertion &source)
{
    return {source.index, testing::assertionTypeName(source.type),
            representationName(source.representation), source.value, source.minimum,
            source.maximum, source.mask, source.decimalPlaces, source.unit};
}

ReportUInt32Expectation mapUInt32(const testing::UInt32Assertion &source)
{
    return {QStringLiteral("low_word_first"), uint32ComparisonName(source.comparison),
            source.minimum, source.maximum, source.decimalPlaces, source.unit};
}

ReportStabilityExpectation mapStabilityExpectation(
    const testing::StabilityExpectation &source)
{
    return {source.minimumTotal, source.minimumSuccesses, source.maximumFailures,
            source.maximumTimeouts, source.maximumFailureRatePpm,
            source.requireValidRttSamples, source.minimumRttMs,
            source.maximumAverageRttMs, source.maximumRttMs};
}

ReportRequestDefinition mapRequest(const testing::TestRequest &source)
{
    return {static_cast<quint8>(source.function), source.address.value(), source.count,
            source.rawValue, source.readBeforeWrite, source.restoreOriginal};
}

QString actualSummary(const ActualResult &actual)
{
    switch (actual.type) {
    case testing::ActualResultType::RegisterValues: {
        QStringList values;
        for (const quint16 value : actual.registerValues) values.append(QString::number(value));
        return values.join(QStringLiteral(", "));
    }
    case testing::ActualResultType::RegisterSamples:
        return QStringLiteral("%1 组寄存器样本").arg(actual.registerSamples.size());
    case testing::ActualResultType::ModbusException:
        return actual.exceptionCode
            ? QStringLiteral("Modbus exception 0x%1")
                  .arg(*actual.exceptionCode, 2, 16, QLatin1Char('0')).toUpper()
            : QStringLiteral("Modbus exception（异常码缺失）");
    case testing::ActualResultType::ResponseTimeout: return QStringLiteral("ResponseTimeout");
    case testing::ActualResultType::CommunicationError:
        return QStringLiteral("CommunicationError");
    case testing::ActualResultType::StabilitySummary:
        if (actual.stability) {
            return QStringLiteral("total=%1, success=%2, failure=%3, timeout=%4")
                .arg(actual.stability->total).arg(actual.stability->successes)
                .arg(actual.stability->failures).arg(actual.stability->timeouts);
        }
        return QStringLiteral("稳定性统计缺失");
    }
    return QStringLiteral("实际结果缺失");
}

QString expectedSummary(const ExpectedAssertion &expected)
{
    switch (expected.type) {
    case testing::AssertionType::Equals:
        return QStringLiteral("等于 %1%2").arg(expected.value)
            .arg(expected.unit.isEmpty() ? QString{} : QStringLiteral(" ") + expected.unit);
    case testing::AssertionType::Range:
        return QStringLiteral("范围 [%1, %2]%3").arg(expected.minimum).arg(expected.maximum)
            .arg(expected.unit.isEmpty() ? QString{} : QStringLiteral(" ") + expected.unit);
    case testing::AssertionType::RegisterSequence: {
        QStringList values;
        for (const qint64 value : expected.values) values.append(QString::number(value));
        return QStringLiteral("寄存器序列 [%1]").arg(values.join(QStringLiteral(", ")));
    }
    case testing::AssertionType::BitMask:
        return QStringLiteral("位掩码 0x%1").arg(expected.mask, 4, 16, QLatin1Char('0')).toUpper();
    case testing::AssertionType::ModbusException:
        return QStringLiteral("Modbus exception 0x%1")
            .arg(expected.exceptionCode, 2, 16, QLatin1Char('0')).toUpper();
    case testing::AssertionType::Elements:
        return QStringLiteral("%1 个逐元素断言").arg(expected.elements.size());
    case testing::AssertionType::ResponseTimeout: return QStringLiteral("ResponseTimeout");
    case testing::AssertionType::UInt32: return QStringLiteral("uint32 一致性断言");
    case testing::AssertionType::StabilitySummary: return QStringLiteral("稳定性汇总断言");
    }
    return {};
}

ReportAssertionDifference mapDifference(const AssertionDifference &source)
{
    return {assertionDifferenceName(source.code), source.index, source.expected,
            source.expectedMinimum, source.expectedMaximum, source.actual,
            source.actualRaw, source.scaledDenominator, source.unit, source.reason};
}

ReportAssertion mapAssertion(const ExpectedAssertion &expected,
                             const std::optional<ActualResult> &actual,
                             const std::optional<AssertionResult> &assertion,
                             const TestStatus fallbackStatus)
{
    ReportAssertion result;
    result.type = testing::assertionTypeName(expected.type);
    result.expectedSummary = expectedSummary(expected);
    result.actualSummary = actual ? actualSummary(*actual) : QStringLiteral("未产生实际结果");
    result.status = fallbackStatus;
    result.expectedValues = expected.values;
    result.unit = expected.unit;
    for (const auto &element : expected.elements) {
        result.expectedElements.append(mapExpectedElement(element));
    }
    if (expected.type == testing::AssertionType::UInt32) {
        result.expectedUInt32 = mapUInt32(expected.uint32);
    }
    if (expected.type == testing::AssertionType::StabilitySummary) {
        result.expectedStability = mapStabilityExpectation(expected.stability);
    }
    if (expected.type == testing::AssertionType::Equals) {
        result.expectedValues = {expected.value};
    }
    if (expected.type == testing::AssertionType::ModbusException) {
        result.expectedExceptionCode = expected.exceptionCode;
    }
    if (actual) {
        result.actualRawValues = actual->registerValues;
        result.actualRawSamples = actual->registerSamples;
        result.actualExceptionCode = actual->exceptionCode;
    }
    if (!assertion) return result;
    result.type = testing::assertionTypeName(assertion->type);
    result.expectedSummary = assertion->expectedSummary.isEmpty()
        ? result.expectedSummary : assertion->expectedSummary;
    result.actualSummary = assertion->actualSummary.isEmpty()
        ? result.actualSummary : assertion->actualSummary;
    result.status = assertion->status;
    if (!assertion->expectedValues.isEmpty()) result.expectedValues = assertion->expectedValues;
    if (!assertion->expectedElements.isEmpty()) {
        result.expectedElements.clear();
        for (const auto &element : assertion->expectedElements) {
            result.expectedElements.append(mapExpectedElement(element));
        }
    }
    if (assertion->expectedUInt32) result.expectedUInt32 = mapUInt32(*assertion->expectedUInt32);
    if (assertion->expectedStability) {
        result.expectedStability = mapStabilityExpectation(*assertion->expectedStability);
    }
    if (!assertion->actualValues.isEmpty()) result.actualValues = assertion->actualValues;
    if (!assertion->actualRawValues.isEmpty()) result.actualRawValues = assertion->actualRawValues;
    if (!assertion->actualRawSamples.isEmpty()) result.actualRawSamples = assertion->actualRawSamples;
    for (const auto &scaled : assertion->actualScaledValues) {
        result.actualScaledValues.append({scaled.numerator, scaled.denominator});
    }
    if (assertion->expectedExceptionCode) {
        result.expectedExceptionCode = assertion->expectedExceptionCode;
    }
    if (assertion->actualExceptionCode) result.actualExceptionCode = assertion->actualExceptionCode;
    if (!assertion->unit.isEmpty()) result.unit = assertion->unit;
    result.differences.reserve(assertion->differences.size());
    for (const auto &difference : assertion->differences) {
        result.differences.append(mapDifference(difference));
    }
    return result;
}

ReportExecutionError mapCommunicationError(const communication::CommunicationError &source)
{
    ReportExecutionError result;
    result.code = communicationCodeName(source.code);
    result.diagnostic = source.diagnostic;
    result.communicationCategory = categoryName(source.category);
    result.communicationCode = communicationCodeName(source.code);
    result.exceptionCode = source.exceptionCode;
    return result;
}

ReportExecutionError mapTestError(const testing::TestError &source)
{
    ReportExecutionError result;
    result.code = testing::testErrorCodeName(source.code);
    result.diagnostic = source.diagnostic;
    if (source.communicationError) {
        result.communicationCategory = categoryName(source.communicationError->category);
        result.communicationCode = communicationCodeName(source.communicationError->code);
        result.exceptionCode = source.communicationError->exceptionCode;
    }
    return result;
}

std::optional<ReportTimestamp> timestampIfValid(const QDateTime &source,
                                                const QTimeZone &zone)
{
    if (!source.isValid()) return std::nullopt;
    const QDateTime utc = source.toUTC();
    const QDateTime local = utc.toTimeZone(zone);
    return ReportTimestamp{utc, utc.toString(Qt::ISODateWithMs),
                           local.toString(Qt::ISODateWithMs), zone.id()};
}

ReportFrame mapFrame(const QByteArray &bytes)
{
    ReportFrame result;
    result.bytes = bytes;
    result.presence = bytes.isEmpty() ? ReportFramePresence::Empty
                                      : ReportFramePresence::Value;
    result.uppercaseHex = QString::fromLatin1(bytes.toHex(' ').toUpper());
    return result;
}

bool validateAttempt(const testing::TestRequestAttemptResult &attempt,
                     const QString &path, QVector<ReportError> &errors)
{
    if (attempt.sequence == 0 || attempt.requestId.value == 0
        || attempt.requestResult.requestId != attempt.requestId
        || attempt.requestResult.evidence.requestId != attempt.requestId) {
        appendError(errors, ReportErrorCode::EvidenceInvariantViolation, path,
                    QStringLiteral("attempt sequence/RequestId 必须非零且跨层一致"));
        return false;
    }
    if (attempt.requestResult.state == communication::RequestState::Queued
        || attempt.requestResult.state == communication::RequestState::InFlight) {
        appendError(errors, ReportErrorCode::NonTerminalStatus,
                    path + QStringLiteral(".requestResult.state"),
                    QStringLiteral("通信请求尚未进入终态"));
        return false;
    }
    if (attempt.duration.count() < 0 || !attempt.startedUtc.isValid()
        || !attempt.finishedUtc.isValid()
        || attempt.finishedUtc.toUTC() < attempt.startedUtc.toUTC()) {
        appendError(errors, ReportErrorCode::EvidenceInvariantViolation, path,
                    QStringLiteral("attempt 时间或持续时间无效"));
        return false;
    }
    if (attempt.requestResult.evidence.rtt
        && attempt.requestResult.evidence.rtt->count() < 0) {
        appendError(errors, ReportErrorCode::EvidenceInvariantViolation,
                    path + QStringLiteral(".rtt"), QStringLiteral("RTT 不能为负数"));
        return false;
    }
    return true;
}

ReportTransactionEvidence mapAttempt(const testing::TestRequestAttemptResult &source,
                                     const QTimeZone &zone)
{
    const auto &evidence = source.requestResult.evidence;
    ReportTransactionEvidence result;
    result.attemptSequence = source.sequence;
    result.purpose = testing::testStepPurposeName(source.purpose);
    result.stepAttempt = source.stepAttempt;
    result.retry = source.retry;
    result.retryReason = source.retryReason;
    result.logicalStepId = source.logicalStepId;
    result.logicalStepIndex = source.logicalStepIndex;
    result.repetition = source.repetition;
    result.requestId = source.requestId.value;
    result.serverAddress = evidence.serverAddress;
    result.functionCode = evidence.functionCode;
    if (const auto *read = std::get_if<communication::ReadRequestDescriptor>(
            &source.requestResult.descriptor)) {
        result.descriptorKind = QStringLiteral("read_holding_registers");
        result.address = read->startAddress.value();
        result.count = read->count;
    } else {
        const auto &write = std::get<communication::WriteRequestDescriptor>(
            source.requestResult.descriptor);
        result.descriptorKind = QStringLiteral("write_single_register");
        result.address = write.address.value();
        result.rawValue = write.rawValue;
    }
    result.requestState = requestStateName(source.requestResult.state);
    result.started = timestampIfValid(source.startedUtc, zone);
    result.finished = timestampIfValid(source.finishedUtc, zone);
    result.duration = source.duration;
    result.tx = mapFrame(evidence.txAdu);
    result.rx = mapFrame(evidence.rxAdu);
    result.txCrcStatus = crcName(evidence.txCrcStatus);
    result.rxCrcStatus = crcName(evidence.rxCrcStatus);
    if (evidence.rtt) {
        result.rttNanoseconds = evidence.rtt->count();
        const double milliseconds = std::chrono::duration<double, std::milli>(*evidence.rtt).count();
        result.rttMilliseconds = QString::number(milliseconds, 'f', 3);
    }
    if (source.requestResult.error) result.error = mapCommunicationError(*source.requestResult.error);
    return result;
}

ReportStabilityStatistics mapStability(const testing::StabilityStatistics &source)
{
    return {source.total, source.successes, source.failures, source.timeouts,
            source.validRttSamples, source.missingRttSamples, source.minimumRttMs,
            source.averageRttMs, source.maximumRttMs};
}

QString observationOutcomeName(const testing::GuidedObservationOutcome outcome)
{
    switch (outcome) {
    case testing::GuidedObservationOutcome::Pending: return QStringLiteral("pending");
    case testing::GuidedObservationOutcome::Matched: return QStringLiteral("matched");
    case testing::GuidedObservationOutcome::DeadlineExpired:
        return QStringLiteral("deadline_expired");
    case testing::GuidedObservationOutcome::FatalCommunicationError:
        return QStringLiteral("fatal_communication_error");
    case testing::GuidedObservationOutcome::Aborted: return QStringLiteral("aborted");
    }
    return QStringLiteral("pending");
}

ReportGuidedRecovery mapGuided(const TestCase &configured,
                               const testing::GuidedRecoveryCaseResult &source,
                               const QTimeZone &zone)
{
    ReportGuidedRecovery result;
    result.finalState = testing::guidedRunStateName(source.finalState);
    result.terminalReason = testing::guidedTerminalReasonName(source.terminalReason);
    result.physicalLinkRestored = source.physicalLinkRestored;
    result.recoveryInstructionRequired = source.recoveryInstructionRequired;
    result.recoveryInstruction = source.recoveryInstruction;
    if (source.recoveryTiming) {
        result.toFirstSuccessfulResponse = source.recoveryTiming->toFirstSuccessfulResponse;
        result.toStableRecovery = source.recoveryTiming->toStableRecovery;
    }
    result.operatorActions.reserve(source.operatorActions.size());
    for (const auto &action : source.operatorActions) {
        ReportGuidedOperatorAction mapped;
        mapped.runId = action.runId.value;
        mapped.caseId = action.caseId;
        mapped.stepId = action.stepId;
        mapped.oneTimeToken = action.oneTimeToken;
        const auto configuredStep = std::find_if(
            configured.guidedRecovery.steps.cbegin(), configured.guidedRecovery.steps.cend(),
            [&action](const testing::GuidedStep &step) { return step.id == action.stepId; });
        if (configuredStep != configured.guidedRecovery.steps.cend()
            && configuredStep->operatorStep) {
            const auto &prompt = *configuredStep->operatorStep;
            mapped.promptPurpose = testing::guidedPromptPurposeName(prompt.purpose);
            mapped.title = prompt.title;
            mapped.instruction = prompt.instruction;
            mapped.safetyNotice = prompt.safetyNotice;
            for (const auto allowed : prompt.allowedActions) {
                mapped.allowedActions.append(testing::guidedOperatorActionName(allowed));
            }
            mapped.cancelRecoveryInstruction = prompt.cancelRecoveryInstruction;
        }
        if (action.action) mapped.action = testing::guidedOperatorActionName(*action.action);
        mapped.promptShown = timestampIfValid(action.promptShownUtc, zone);
        if (action.actionUtc) mapped.actionTime = timestampIfValid(*action.actionUtc, zone);
        mapped.waitDuration = action.waitDuration;
        mapped.note = action.note;
        result.operatorActions.append(std::move(mapped));
    }
    result.observations.reserve(source.observations.size());
    for (const auto &observation : source.observations) {
        ReportGuidedObservation mapped;
        mapped.runId = observation.runId.value;
        mapped.caseId = observation.caseId;
        mapped.stepId = observation.stepId;
        mapped.target = testing::guidedObservationTargetName(observation.target);
        mapped.outcome = observationOutcomeName(observation.outcome);
        const auto configuredStep = std::find_if(
            configured.guidedRecovery.steps.cbegin(), configured.guidedRecovery.steps.cend(),
            [&observation](const testing::GuidedStep &step) {
                return step.id == observation.stepId;
            });
        if (configuredStep != configured.guidedRecovery.steps.cend()
            && configuredStep->observationStep) {
            const auto &configuredObservation = *configuredStep->observationStep;
            mapped.probe = mapRequest(configuredObservation.probe);
            mapped.interval = configuredObservation.interval;
            mapped.deadline = configuredObservation.deadline;
            if (configuredObservation.businessAssertion) {
                mapped.businessExpectedSummary = expectedSummary(
                    *configuredObservation.businessAssertion);
            }
        }
        mapped.started = timestampIfValid(observation.startedUtc, zone);
        mapped.finished = timestampIfValid(observation.finishedUtc, zone);
        mapped.duration = observation.duration;
        mapped.requiredConsecutiveMatches = observation.requiredConsecutiveMatches;
        mapped.achievedConsecutiveMatches = observation.achievedConsecutiveMatches;
        if (observation.firstMatchingRequestId) {
            mapped.firstMatchingRequestId = observation.firstMatchingRequestId->value;
        }
        if (observation.stableMatchingRequestId) {
            mapped.stableMatchingRequestId = observation.stableMatchingRequestId->value;
        }
        for (const auto &probe : observation.probes) mapped.attemptSequences.append(probe.sequence);
        if (observation.error) mapped.error = mapTestError(*observation.error);
        result.observations.append(std::move(mapped));
    }
    return result;
}

bool addChecked(quint64 &target, const quint64 value) noexcept
{
    if (value > std::numeric_limits<quint64>::max() - target) return false;
    target += value;
    return true;
}

TestStatus expectedSuiteStatus(const testing::TestSuiteResult &run)
{
    for (const auto &error : run.auxiliaryErrors) {
        if (error.code != testing::TestErrorCode::LoggingFailed) return TestStatus::Error;
    }
    bool anyFail = false;
    bool anyPass = false;
    bool allSkipped = !run.cases.isEmpty();
    for (const auto &testCase : run.cases) {
        if (testCase.status == TestStatus::Error) return TestStatus::Error;
        anyFail = anyFail || testCase.status == TestStatus::Fail;
        anyPass = anyPass || testCase.status == TestStatus::Pass;
        allSkipped = allSkipped && testCase.status == TestStatus::Skipped;
    }
    if (anyFail) return TestStatus::Fail;
    if (run.suite && run.suite->schemaVersion == testing::testSuiteSchemaVersionV3
        && allSkipped && !anyPass) {
        return TestStatus::Skipped;
    }
    return TestStatus::Pass;
}

bool buildSummary(const testing::TestSuiteResult &run, ReportSummary &summary,
                  QVector<ReportError> &errors)
{
    for (const auto &testCase : run.cases) {
        quint64 *counter = nullptr;
        switch (testCase.status) {
        case TestStatus::Pass: counter = &summary.passed; break;
        case TestStatus::Fail: counter = &summary.failed; break;
        case TestStatus::Error: counter = &summary.errors; break;
        case TestStatus::Skipped: counter = &summary.skipped; break;
        case TestStatus::NotRun:
        case TestStatus::Running: break;
        }
        if (!counter || !addChecked(*counter, 1)) {
            appendError(errors, ReportErrorCode::SummaryOverflow, QStringLiteral("summary"),
                        QStringLiteral("汇总计数溢出或包含非终态"));
            return false;
        }
    }
    if (!addChecked(summary.total, summary.passed)
        || !addChecked(summary.total, summary.failed)
        || !addChecked(summary.total, summary.errors)
        || !addChecked(summary.total, summary.skipped)
        || !addChecked(summary.executed, summary.passed)
        || !addChecked(summary.executed, summary.failed)
        || !addChecked(summary.executed, summary.errors)) {
        appendError(errors, ReportErrorCode::SummaryOverflow, QStringLiteral("summary"),
                    QStringLiteral("汇总加法溢出"));
        return false;
    }
    if (summary.total != static_cast<quint64>(run.cases.size())) {
        appendError(errors, ReportErrorCode::SummaryInvariantViolation,
                    QStringLiteral("summary.total"), QStringLiteral("终态计数之和不等于总数"));
        return false;
    }
    summary.passRateNumerator = summary.passed;
    summary.passRateDenominator = summary.executed;
    if (summary.executed > 0) {
        if (summary.passed > (std::numeric_limits<quint64>::max()
                              - summary.executed / 2) / reportPassRateScalePpm) {
            appendError(errors, ReportErrorCode::SummaryOverflow,
                        QStringLiteral("summary.pass_rate"), QStringLiteral("通过率计算溢出"));
            return false;
        }
        summary.passRatePpm = static_cast<quint32>(
            (summary.passed * reportPassRateScalePpm + summary.executed / 2)
            / summary.executed);
    }
    summary.duration = run.duration;
    summary.status = run.status;
    return true;
}

bool validSha256(const QString &value) noexcept
{
    if (value.size() != 64) return false;
    return std::all_of(value.cbegin(), value.cend(), [](const QChar ch) {
        const ushort u = ch.unicode();
        return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f')
            || (u >= 'A' && u <= 'F');
    });
}

std::optional<QString> configuredValue(const testing::TestSuite &suite,
                                       const QStringList &keys)
{
    for (const auto &key : keys) {
        const auto it = suite.metadata.constFind(key);
        if (it != suite.metadata.cend() && !it.value().trimmed().isEmpty()) return it.value();
    }
    return std::nullopt;
}

bool validateMetadataInput(const ReportInput &input, QVector<ReportError> &errors)
{
    const auto validate = [&errors](const QString &value, const qsizetype maximum,
                                    const QString &path) {
        if (!textValid(value, maximum)) {
            appendError(errors, ReportErrorCode::InvalidMetadata, path,
                        QStringLiteral("元数据过长或包含 U+0000"));
            return false;
        }
        return true;
    };
    bool valid = validate(input.applicationVersion, maximumVersionLength,
                          QStringLiteral("applicationVersion"));
    valid = validate(input.sourceRevision, maximumVersionLength,
                     QStringLiteral("sourceRevision")) && valid;
    valid = validate(input.operatorMetadata.tester, maximumTesterLength,
                     QStringLiteral("operatorMetadata.tester")) && valid;
    valid = validate(input.operatorMetadata.deviceModel, maximumDeviceLength,
                     QStringLiteral("operatorMetadata.deviceModel")) && valid;
    valid = validate(input.operatorMetadata.testBench, maximumDeviceLength,
                     QStringLiteral("operatorMetadata.testBench")) && valid;
    valid = validate(input.operatorMetadata.environmentDescription,
                     maximumEnvironmentLength,
                     QStringLiteral("operatorMetadata.environmentDescription")) && valid;
    if (input.connectionConfig) {
        if (const auto error = communication::validateConfig(*input.connectionConfig)) {
            appendError(errors, ReportErrorCode::InvalidMetadata,
                        QStringLiteral("connectionConfig"),
                        QStringLiteral("连接配置快照无效：%1")
                            .arg(communicationCodeName(error->code)));
            valid = false;
        }
    }
    if (input.suiteResult.suite) {
        const auto &suite = *input.suiteResult.suite;
        for (auto it = suite.metadata.cbegin(); it != suite.metadata.cend(); ++it) {
            if (!requiredTextValid(it.key(), 64) || !textValid(it.value(), 256)) {
                appendError(errors, ReportErrorCode::InvalidMetadata,
                            QStringLiteral("suiteResult.suite.metadata.%1").arg(it.key()),
                            QStringLiteral("套件 metadata key/value 无效"));
                valid = false;
            }
        }
        for (qsizetype index = 0; index < suite.tags.size(); ++index) {
            if (!requiredTextValid(suite.tags.at(index), 64)) {
                appendError(errors, ReportErrorCode::InvalidMetadata,
                            QStringLiteral("suiteResult.suite.tags[%1]").arg(index),
                            QStringLiteral("套件标签无效"));
                valid = false;
            }
        }
    }
    return valid;
}

struct FirmwareCandidate { quint16 major = 0; quint16 minor = 0; };

bool sameFirmware(const FirmwareCandidate &left, const FirmwareCandidate &right) noexcept
{
    return left.major == right.major && left.minor == right.minor;
}

void considerFirmware(const testing::TestRequest &request, const TestStatus status,
                      const std::optional<AssertionResult> &assertion,
                      const std::optional<ActualResult> &actual,
                      QVector<FirmwareCandidate> &candidates)
{
    if (status != TestStatus::Pass || !assertion || !assertion->passed() || !actual
        || request.function != testing::ModbusFunction::ReadHoldingRegisters) return;
    const quint32 start = request.address.value();
    const quint32 end = start + request.count - 1U;
    if (start > static_cast<quint32>(device::HoldingRegister::FirmwareMajor)
        || end < static_cast<quint32>(device::HoldingRegister::FirmwareMinor)) return;
    const QVector<quint16> *values = nullptr;
    if (actual->type == testing::ActualResultType::RegisterValues) {
        values = &actual->registerValues;
    } else if (actual->type == testing::ActualResultType::RegisterSamples
               && !actual->registerSamples.isEmpty()) {
        values = &actual->registerSamples.back();
    }
    if (!values) return;
    const qsizetype majorIndex = static_cast<qsizetype>(
        static_cast<quint32>(device::HoldingRegister::FirmwareMajor) - start);
    const qsizetype minorIndex = static_cast<qsizetype>(
        static_cast<quint32>(device::HoldingRegister::FirmwareMinor) - start);
    if (majorIndex < 0 || minorIndex < 0 || majorIndex >= values->size()
        || minorIndex >= values->size()) return;
    candidates.append({values->at(majorIndex), values->at(minorIndex)});
}

std::optional<QString> firmwareVersion(const testing::TestSuite &suite,
                                       const testing::TestSuiteResult &run,
                                       QVector<ReportError> &errors)
{
    QVector<FirmwareCandidate> candidates;
    for (qsizetype index = 0; index < suite.cases.size(); ++index) {
        const auto &testCase = suite.cases.at(index);
        const auto &caseResult = run.cases.at(index);
        considerFirmware(testCase.request, caseResult.status, caseResult.assertion,
                         caseResult.actual, candidates);
        if (testCase.type != testing::TestCaseType::Sequence) continue;
        for (const auto &stepResult : caseResult.steps) {
            if (stepResult.stepIndex < 0
                || stepResult.stepIndex >= testCase.sequence.steps.size()) continue;
            const auto &step = testCase.sequence.steps.at(stepResult.stepIndex);
            considerFirmware(step.request, stepResult.status, stepResult.assertion,
                             stepResult.actual, candidates);
        }
    }
    if (candidates.isEmpty()) return std::nullopt;
    const FirmwareCandidate selected = candidates.front();
    for (const auto &candidate : candidates) {
        if (!sameFirmware(selected, candidate)) {
            appendError(errors, ReportErrorCode::InconsistentFirmwareVersion,
                        QStringLiteral("metadata.firmwareVersion"),
                        QStringLiteral("本次通过断言的 Firmware 实际读取值不一致"));
            return std::nullopt;
        }
    }
    const QString version = QStringLiteral("%1.%2").arg(selected.major).arg(selected.minor);
    if (!textValid(version, maximumFirmwareLength)) {
        appendError(errors, ReportErrorCode::InvalidMetadata,
                    QStringLiteral("metadata.firmwareVersion"),
                    QStringLiteral("Firmware 版本展示值无效"));
        return std::nullopt;
    }
    return version;
}

ReportConnectionMetadata mapConnection(
    const std::optional<communication::ModbusConnectionConfig> &source)
{
    if (!source) return {};
    ReportConnectionMetadata result;
    result.available = true;
    result.missingDisplay.clear();
    result.source = MetadataSource::SystemObserved;
    result.portName = source->serial.portName;
    result.baudRate = source->serial.baudRate;
    result.dataBits = source->serial.dataBits;
    switch (source->serial.parity) {
    case communication::SerialParity::None: result.parity = QStringLiteral("none"); break;
    case communication::SerialParity::Even: result.parity = QStringLiteral("even"); break;
    case communication::SerialParity::Odd: result.parity = QStringLiteral("odd"); break;
    }
    switch (source->serial.stopBits) {
    case communication::SerialStopBits::One: result.stopBits = QStringLiteral("1"); break;
    case communication::SerialStopBits::Two: result.stopBits = QStringLiteral("2"); break;
    }
    result.flowControl = QStringLiteral("none");
    result.serverAddress = source->serverAddress;
    result.defaultResponseTimeoutMs = source->defaultResponseTimeout.count();
    result.maxPendingRequests = source->maxPendingRequests;
    return result;
}

ReportMetadataValue configuredThenOperator(
    const std::optional<QString> &configured, const QString &operatorValue)
{
    if (configured) return availableValue(*configured, MetadataSource::SuiteConfigured);
    if (!operatorValue.trimmed().isEmpty()) {
        return availableValue(operatorValue, MetadataSource::OperatorEntered);
    }
    return unavailableValue(QStringLiteral("未填写"));
}

ReportMetadata buildMetadata(const ReportInput &input,
                             const std::optional<QString> &firmware)
{
    const auto &suite = *input.suiteResult.suite;
    ReportMetadata result;
    result.projectName = availableValue(QStringLiteral("OMS555TV"), MetadataSource::SystemObserved);
    result.testObject = availableValue(suite.name, MetadataSource::SuiteConfigured);
    result.deviceModel = configuredThenOperator(
        configuredValue(suite, {QStringLiteral("device_model"), QStringLiteral("hardware")}),
        input.operatorMetadata.deviceModel);
    result.testBench = configuredThenOperator(
        configuredValue(suite, {QStringLiteral("test_bench"), QStringLiteral("bench")}),
        input.operatorMetadata.testBench);
    result.environmentDescription = configuredThenOperator(
        configuredValue(suite, {QStringLiteral("environment_description"),
                                QStringLiteral("hardware_scope")}),
        input.operatorMetadata.environmentDescription);
    result.firmwareVersion = firmware
        ? availableValue(*firmware, MetadataSource::SystemObserved)
        : unavailableValue(QStringLiteral("未采集"));
    result.hostVersion = input.applicationVersion.trimmed().isEmpty()
        ? unavailableValue(QStringLiteral("未采集"))
        : availableValue(input.applicationVersion, MetadataSource::SystemObserved);
    result.sourceRevision = input.sourceRevision.trimmed().isEmpty()
        ? unavailableValue(QStringLiteral("未采集"))
        : availableValue(input.sourceRevision, MetadataSource::SystemObserved);
    result.tester = input.operatorMetadata.tester.trimmed().isEmpty()
        ? unavailableValue(QStringLiteral("未填写"))
        : availableValue(input.operatorMetadata.tester, MetadataSource::OperatorEntered);
    result.sessionId = input.suiteResult.sessionId.trimmed().isEmpty()
        ? unavailableValue(QStringLiteral("未采集"))
        : availableValue(input.suiteResult.sessionId, MetadataSource::SystemObserved);
    result.connection = mapConnection(input.connectionConfig);
    for (auto it = suite.metadata.cbegin(); it != suite.metadata.cend(); ++it) {
        result.suiteEntries.insert(it.key(),
            availableValue(it.value(), MetadataSource::SuiteConfigured));
    }
    return result;
}

bool validateRetention(const TestCaseResult &source, const QString &path,
                       QVector<ReportError> &errors)
{
    const auto &retention = source.evidenceRetention;
    quint64 retainedAndDropped = retention.retainedAttempts;
    if (!addChecked(retainedAndDropped, retention.droppedAttempts)
        || retainedAndDropped != retention.totalAttempts
        || retention.retainedAttempts != static_cast<quint64>(source.attempts.size())
        || retention.totalFailures > retention.totalAttempts
        || retention.retainedFailures > retention.totalFailures
        || retention.configuredLimit < 0) {
        appendError(errors, ReportErrorCode::EvidenceInvariantViolation,
                    path + QStringLiteral(".evidenceRetention"),
                    QStringLiteral("证据保留计数不变量不成立"));
        return false;
    }
    return true;
}

bool validateStability(const TestCase &configured, const TestCaseResult &source,
                       const QString &path, QVector<ReportError> &errors)
{
    if (configured.type == testing::TestCaseType::Stability
        && source.status == TestStatus::Pass && !source.stability) {
        appendError(errors, ReportErrorCode::EvidenceInvariantViolation,
                    path + QStringLiteral(".stability"),
                    QStringLiteral("PASS 稳定性用例缺少统计"));
        return false;
    }
    if (!source.stability) return true;
    const auto &stats = *source.stability;
    quint64 terminalTotal = stats.successes;
    quint64 rttTotal = stats.validRttSamples;
    if (!addChecked(terminalTotal, stats.failures)
        || !addChecked(terminalTotal, stats.timeouts)
        || !addChecked(rttTotal, stats.missingRttSamples)
        || terminalTotal != stats.total || rttTotal != stats.total) {
        appendError(errors, ReportErrorCode::EvidenceInvariantViolation,
                    path + QStringLiteral(".stability"),
                    QStringLiteral("稳定性统计计数不变量不成立"));
        return false;
    }
    if (stats.validRttSamples > 0
        && (!stats.minimumRttMs || !stats.averageRttMs || !stats.maximumRttMs
            || *stats.minimumRttMs > *stats.averageRttMs
            || *stats.averageRttMs > *stats.maximumRttMs)) {
        appendError(errors, ReportErrorCode::EvidenceInvariantViolation,
                    path + QStringLiteral(".stability.rtt"),
                    QStringLiteral("稳定性 RTT 统计缺失或顺序无效"));
        return false;
    }
    return true;
}

bool validateOptionalRange(const QDateTime &started, const QDateTime &finished,
                           const std::chrono::milliseconds duration,
                           const bool timeRequired, const QString &path,
                           QVector<ReportError> &errors)
{
    if (duration.count() < 0) {
        appendError(errors, ReportErrorCode::InvalidDuration, path + QStringLiteral(".duration"),
                    QStringLiteral("持续时间不能为负数"));
        return false;
    }
    if (!started.isValid() && !finished.isValid() && !timeRequired) return true;
    if (!started.isValid() || !finished.isValid() || finished.toUTC() < started.toUTC()) {
        appendError(errors, ReportErrorCode::InvalidTimestamp, path,
                    QStringLiteral("开始/结束时间缺失或顺序无效"));
        return false;
    }
    return true;
}

ReportEvidenceRetention mapRetention(const testing::TestEvidenceRetentionSummary &source)
{
    return {testing::testEvidenceRetentionPolicyName(source.policy), source.totalAttempts,
            source.retainedAttempts, source.droppedAttempts, source.totalFailures,
            source.retainedFailures, source.configuredLimit, source.description};
}

ReportCase mapCase(const TestCase &configured, const TestCaseResult &source,
                   const QTimeZone &zone)
{
    ReportCase result;
    result.id = configured.id;
    result.name = configured.name;
    result.category = configured.category;
    result.description = configured.description;
    result.environment = testing::executionEnvironmentName(configured.environment);
    result.tags = configured.tags;
    result.declaredType = testing::testCaseTypeName(configured.declaredType);
    result.normalizedType = testing::testCaseTypeName(configured.type);
    if (configured.type != testing::TestCaseType::Sequence
        && configured.type != testing::TestCaseType::GuidedRecovery) {
        result.request = mapRequest(configured.request);
    }
    result.status = source.status;
    if (source.skipReason) result.skipReason = testing::testSkipReasonName(*source.skipReason);
    result.assertion = mapAssertion(source.expected, source.actual, source.assertion, source.status);
    if (source.error) result.error = mapTestError(*source.error);
    if (source.cleanupError) result.cleanupError = mapTestError(*source.cleanupError);
    result.started = timestampIfValid(source.startedUtc, zone);
    result.finished = timestampIfValid(source.finishedUtc, zone);
    result.duration = source.duration;
    result.steps.reserve(source.steps.size());
    for (const auto &step : source.steps) {
        ReportCompositeStep mapped;
        mapped.stepId = step.stepId;
        mapped.stepIndex = step.stepIndex;
        mapped.repetition = step.repetition;
        if (configured.type == testing::TestCaseType::Sequence
            && step.stepIndex >= 0 && step.stepIndex < configured.sequence.steps.size()) {
            const auto &configuredStep = configured.sequence.steps.at(step.stepIndex);
            mapped.declaredType = testing::testCaseTypeName(configuredStep.declaredType);
            mapped.normalizedType = testing::testCaseTypeName(configuredStep.type);
            mapped.delayBefore = configuredStep.delayBefore;
            mapped.request = mapRequest(configuredStep.request);
        }
        mapped.status = step.status;
        mapped.assertion = mapAssertion(step.expected, step.actual, step.assertion, step.status);
        if (step.error) mapped.error = mapTestError(*step.error);
        mapped.started = timestampIfValid(step.startedUtc, zone);
        mapped.finished = timestampIfValid(step.finishedUtc, zone);
        mapped.duration = step.duration;
        mapped.attemptSequences = step.attemptSequences;
        result.steps.append(std::move(mapped));
    }
    result.attempts.reserve(source.attempts.size());
    for (const auto &attempt : source.attempts) result.attempts.append(mapAttempt(attempt, zone));
    if (source.stability) result.stability = mapStability(*source.stability);
    if (source.guidedRecovery) {
        result.guidedRecovery = mapGuided(configured, *source.guidedRecovery, zone);
    }
    result.evidenceRetention = mapRetention(source.evidenceRetention);
    result.sessionId = source.sessionId;
    return result;
}

ReportSessionLogArtifact buildArtifact(const ReportInput &input,
                                       QVector<ReportError> &errors)
{
    ReportSessionLogArtifact result;
    result.sessionId = input.suiteResult.sessionId;
    if (!input.sessionLogArtifact || !input.sessionLogArtifact->available) return result;
    const auto &source = *input.sessionLogArtifact;
    if (source.sessionId.trimmed().isEmpty() || source.path.trimmed().isEmpty()
        || source.sizeBytes < 0 || !validSha256(source.sha256)
        || source.sessionId != input.suiteResult.sessionId) {
        appendError(errors, ReportErrorCode::InvalidArtifact,
                    QStringLiteral("sessionLogArtifact"),
                    QStringLiteral("可用日志工件缺少字段、哈希无效或 Session ID 不匹配"));
        return result;
    }
    result.available = true;
    result.missingDisplay.clear();
    result.sessionId = source.sessionId;
    result.path = source.path;
    result.sizeBytes = source.sizeBytes;
    result.sha256 = source.sha256.toUpper();
    return result;
}

} // namespace

ReportBuildResult ReportModelBuilder::build(const ReportInput &input)
{
    ReportBuildResult output;
    const auto &run = input.suiteResult;
    if (!run.suite) {
        appendError(output.errors, ReportErrorCode::MissingSuite,
                    QStringLiteral("suiteResult.suite"), QStringLiteral("测试结果未关联已执行套件"));
        return output;
    }
    if (run.runId.value == 0) {
        appendError(output.errors, ReportErrorCode::InvalidRunId,
                    QStringLiteral("suiteResult.runId"), QStringLiteral("run ID 必须非零"));
    }
    if (!input.displayTimeZone.isValid()) {
        appendError(output.errors, ReportErrorCode::InvalidTimeZone,
                    QStringLiteral("displayTimeZone"), QStringLiteral("展示时区无效"));
    }
    if (!terminal(run.status)) {
        appendError(output.errors, ReportErrorCode::NonTerminalStatus,
                    QStringLiteral("suiteResult.status"), QStringLiteral("套件尚未进入终态"));
    }
    if (run.duration.count() < 0) {
        appendError(output.errors, ReportErrorCode::InvalidDuration,
                    QStringLiteral("suiteResult.duration"), QStringLiteral("套件持续时间不能为负数"));
    }
    if (!run.startedUtc.isValid() || !run.finishedUtc.isValid()
        || run.finishedUtc.toUTC() < run.startedUtc.toUTC()) {
        appendError(output.errors, ReportErrorCode::InvalidTimestamp,
                    QStringLiteral("suiteResult"), QStringLiteral("套件 UTC 时间缺失或顺序无效"));
    }
    if (!requiredTextValid(run.suite->id, maximumSuiteTextLength)
        || !requiredTextValid(run.suite->name, maximumSuiteTextLength)
        || !textValid(run.suite->description, maximumSuiteTextLength)) {
        appendError(output.errors, ReportErrorCode::InvalidMetadata,
                    QStringLiteral("suiteResult.suite"), QStringLiteral("套件基本文本无效"));
    }
    if (run.cases.size() != run.suite->cases.size()) {
        appendError(output.errors, ReportErrorCode::SuiteResultCountMismatch,
                    QStringLiteral("suiteResult.cases"), QStringLiteral("结果用例数量与套件不一致"));
    }
    validateMetadataInput(input, output.errors);
    if (!output.errors.isEmpty()) return output;

    for (qsizetype index = 0; index < run.cases.size(); ++index) {
        const QString path = QStringLiteral("suiteResult.cases[%1]").arg(index);
        const auto &result = run.cases.at(index);
        const auto &configured = run.suite->cases.at(index);
        if (result.caseId != configured.id) {
            appendError(output.errors, ReportErrorCode::CaseIdentityMismatch,
                        path + QStringLiteral(".caseId"),
                        QStringLiteral("结果用例 ID 与套件顺序不一致"));
        }
        if (result.type != configured.type || result.declaredType != configured.declaredType) {
            appendError(output.errors, ReportErrorCode::CaseIdentityMismatch,
                        path + QStringLiteral(".type"),
                        QStringLiteral("结果用例类型与套件不一致"));
        }
        if (!terminal(result.status)) {
            appendError(output.errors, ReportErrorCode::NonTerminalStatus,
                        path + QStringLiteral(".status"), QStringLiteral("用例尚未进入终态"));
        }
        validateOptionalRange(result.startedUtc, result.finishedUtc, result.duration,
                              result.status != TestStatus::Skipped, path, output.errors);
        validateRetention(result, path, output.errors);
        validateStability(configured, result, path, output.errors);
        if (configured.type == testing::TestCaseType::GuidedRecovery
            && result.status != TestStatus::Skipped && !result.guidedRecovery) {
            appendError(output.errors, ReportErrorCode::EvidenceInvariantViolation,
                        path + QStringLiteral(".guidedRecovery"),
                        QStringLiteral("已执行引导用例缺少 guided recovery 结果"));
        }
        if (configured.type != testing::TestCaseType::GuidedRecovery
            && result.guidedRecovery) {
            appendError(output.errors, ReportErrorCode::CaseIdentityMismatch,
                        path + QStringLiteral(".guidedRecovery"),
                        QStringLiteral("非引导用例包含 guided recovery 结果"));
        }
        if (!result.sessionId.isEmpty() && !run.sessionId.isEmpty()
            && result.sessionId != run.sessionId) {
            appendError(output.errors, ReportErrorCode::EvidenceInvariantViolation,
                        path + QStringLiteral(".sessionId"),
                        QStringLiteral("用例 Session ID 与套件结果不一致"));
        }
        QSet<quint64> attemptSequences;
        for (qsizetype attemptIndex = 0; attemptIndex < result.attempts.size(); ++attemptIndex) {
            const auto &attempt = result.attempts.at(attemptIndex);
            validateAttempt(attempt,
                            path + QStringLiteral(".attempts[%1]").arg(attemptIndex),
                            output.errors);
            if (attemptSequences.contains(attempt.sequence)) {
                appendError(output.errors, ReportErrorCode::EvidenceInvariantViolation,
                            path + QStringLiteral(".attempts[%1].sequence").arg(attemptIndex),
                            QStringLiteral("attempt sequence 重复"));
            }
            attemptSequences.insert(attempt.sequence);
        }
        for (qsizetype stepIndex = 0; stepIndex < result.steps.size(); ++stepIndex) {
            const auto &step = result.steps.at(stepIndex);
            if (!terminal(step.status)) {
                appendError(output.errors, ReportErrorCode::NonTerminalStatus,
                            path + QStringLiteral(".steps[%1].status").arg(stepIndex),
                            QStringLiteral("复合步骤尚未进入终态"));
            }
            validateOptionalRange(step.startedUtc, step.finishedUtc, step.duration,
                                  step.status != TestStatus::Skipped,
                                  path + QStringLiteral(".steps[%1]").arg(stepIndex),
                                  output.errors);
            if (configured.type == testing::TestCaseType::Sequence
                && (step.stepIndex < 0 || step.stepIndex >= configured.sequence.steps.size()
                    || configured.sequence.steps.at(step.stepIndex).id != step.stepId)) {
                appendError(output.errors, ReportErrorCode::CaseIdentityMismatch,
                            path + QStringLiteral(".steps[%1]").arg(stepIndex),
                            QStringLiteral("sequence 步骤 ID/index 与套件不一致"));
            }
            for (const quint64 sequence : step.attemptSequences) {
                if (!attemptSequences.contains(sequence)) {
                    appendError(output.errors, ReportErrorCode::EvidenceInvariantViolation,
                                path + QStringLiteral(".steps[%1].attemptSequences")
                                           .arg(stepIndex),
                                QStringLiteral("步骤引用了未保留的 attempt sequence"));
                }
            }
        }
        if (result.guidedRecovery) {
            for (qsizetype observationIndex = 0;
                 observationIndex < result.guidedRecovery->observations.size();
                 ++observationIndex) {
                const auto &observation = result.guidedRecovery->observations.at(observationIndex);
                const auto configuredObservation = std::find_if(
                    configured.guidedRecovery.steps.cbegin(),
                    configured.guidedRecovery.steps.cend(),
                    [&observation](const testing::GuidedStep &step) {
                        return step.id == observation.stepId && step.observationStep.has_value();
                    });
                if (configuredObservation == configured.guidedRecovery.steps.cend()) {
                    appendError(output.errors, ReportErrorCode::CaseIdentityMismatch,
                        path + QStringLiteral(".guidedRecovery.observations[%1].stepId")
                                   .arg(observationIndex),
                        QStringLiteral("引导观察步骤不在套件配置中"));
                }
                if (observation.runId.value != run.runId.value
                    || observation.caseId != result.caseId) {
                    appendError(output.errors, ReportErrorCode::CaseIdentityMismatch,
                        path + QStringLiteral(".guidedRecovery.observations[%1]")
                                   .arg(observationIndex),
                        QStringLiteral("引导观察的 run/case 关联不一致"));
                }
                if (observation.outcome == testing::GuidedObservationOutcome::Pending) {
                    appendError(output.errors, ReportErrorCode::NonTerminalStatus,
                        path + QStringLiteral(".guidedRecovery.observations[%1].outcome")
                                   .arg(observationIndex),
                        QStringLiteral("引导观察尚未进入终态"));
                }
                validateOptionalRange(observation.startedUtc, observation.finishedUtc,
                                      observation.duration, true,
                        path + QStringLiteral(".guidedRecovery.observations[%1]")
                                   .arg(observationIndex), output.errors);
                for (const auto &probe : observation.probes) {
                    if (!attemptSequences.contains(probe.sequence)) {
                        appendError(output.errors, ReportErrorCode::EvidenceInvariantViolation,
                            path + QStringLiteral(".guidedRecovery.observations[%1].probes")
                                       .arg(observationIndex),
                            QStringLiteral("观察引用了未保留的探测 attempt"));
                    }
                }
            }
            for (qsizetype actionIndex = 0;
                 actionIndex < result.guidedRecovery->operatorActions.size(); ++actionIndex) {
                const auto &action = result.guidedRecovery->operatorActions.at(actionIndex);
                const auto configuredAction = std::find_if(
                    configured.guidedRecovery.steps.cbegin(),
                    configured.guidedRecovery.steps.cend(),
                    [&action](const testing::GuidedStep &step) {
                        return step.id == action.stepId && step.operatorStep.has_value();
                    });
                if (configuredAction == configured.guidedRecovery.steps.cend()) {
                    appendError(output.errors, ReportErrorCode::CaseIdentityMismatch,
                        path + QStringLiteral(".guidedRecovery.operatorActions[%1].stepId")
                                   .arg(actionIndex),
                        QStringLiteral("人工动作步骤不在套件配置中"));
                }
                if (action.runId.value != run.runId.value || action.caseId != result.caseId) {
                    appendError(output.errors, ReportErrorCode::CaseIdentityMismatch,
                        path + QStringLiteral(".guidedRecovery.operatorActions[%1]")
                                   .arg(actionIndex),
                        QStringLiteral("人工动作的 run/case 关联不一致"));
                }
                if (action.waitDuration.count() < 0 || !action.promptShownUtc.isValid()) {
                    appendError(output.errors, ReportErrorCode::InvalidTimestamp,
                        path + QStringLiteral(".guidedRecovery.operatorActions[%1]")
                                   .arg(actionIndex),
                        QStringLiteral("人工动作提示时间或等待时长无效"));
                }
            }
        }
    }
    if (!output.errors.isEmpty()) return output;

    if (expectedSuiteStatus(run) != run.status) {
        appendError(output.errors, ReportErrorCode::SuiteStatusMismatch,
                    QStringLiteral("suiteResult.status"),
                    QStringLiteral("套件终态与 TestEngine 既有优先级不一致"));
        return output;
    }

    ReportSummary summary;
    if (!buildSummary(run, summary, output.errors)) return output;
    const auto firmware = firmwareVersion(*run.suite, run, output.errors);
    if (!output.errors.isEmpty()) return output;
    const auto artifact = buildArtifact(input, output.errors);
    if (!output.errors.isEmpty()) return output;

    ReportDocumentModel document;
    document.runId = run.runId.value;
    document.schemaVersion = run.suite->schemaVersion;
    document.suiteId = run.suite->id;
    document.suiteName = run.suite->name;
    document.suiteDescription = run.suite->description;
    document.suiteTags = run.suite->tags;
    document.status = run.status;
    document.aborted = run.aborted;
    document.started = *timestampIfValid(run.startedUtc, input.displayTimeZone);
    document.finished = *timestampIfValid(run.finishedUtc, input.displayTimeZone);
    document.duration = run.duration;
    document.metadata = buildMetadata(input, firmware);
    document.summary = summary;
    document.sessionLogArtifact = artifact;
    document.cases.reserve(run.cases.size());
    for (qsizetype index = 0; index < run.cases.size(); ++index) {
        document.cases.append(mapCase(run.suite->cases.at(index), run.cases.at(index),
                                      input.displayTimeZone));
    }
    document.auxiliaryErrors.reserve(run.auxiliaryErrors.size());
    for (const auto &error : run.auxiliaryErrors) {
        document.auxiliaryErrors.append(mapTestError(error));
    }
    output.document = std::move(document);
    return output;
}

} // namespace oms555tv::report
