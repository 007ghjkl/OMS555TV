#pragma once

#include "communication/CommunicationTypes.h"
#include "testing/TestEngineTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <QVector>

#include <chrono>
#include <optional>

namespace oms555tv::report {

inline constexpr quint32 reportPassRateScalePpm = 1'000'000;

enum class MetadataSource {
    SystemObserved,
    SuiteConfigured,
    OperatorEntered,
    Unavailable,
};

enum class ReportFramePresence { Missing, Empty, Value };

enum class ReportErrorCode {
    MissingSuite,
    InvalidRunId,
    NonTerminalStatus,
    SuiteResultCountMismatch,
    CaseIdentityMismatch,
    SuiteStatusMismatch,
    InvalidTimestamp,
    InvalidDuration,
    InvalidTimeZone,
    InvalidMetadata,
    SummaryOverflow,
    SummaryInvariantViolation,
    EvidenceInvariantViolation,
    InvalidArtifact,
    InconsistentFirmwareVersion,
    InvalidGenerationTime,
    InvalidOutputPath,
    TargetAlreadyExists,
    OutputOpenFailed,
    OutputWriteFailed,
    OutputCommitFailed,
    NoCompletedResult,
    TestRunInProgress,
    MissingRequiredMetadata,
    ExportInProgress,
    InvalidSessionLogArtifact,
};

struct ReportError {
    ReportErrorCode code = ReportErrorCode::SummaryInvariantViolation;
    QString path;
    QString diagnostic;
};

struct ReportMetadataValue {
    QString value;
    MetadataSource source = MetadataSource::Unavailable;
    bool available = false;
    QString missingDisplay;
};

struct OperatorReportMetadata {
    QString tester;
    QString deviceModel;
    QString testBench;
    QString environmentDescription;
};

struct SessionLogArtifactInput {
    bool available = false;
    QString sessionId;
    QString path;
    qint64 sizeBytes = 0;
    QString sha256;
};

struct ReportInput {
    testing::TestSuiteResult suiteResult;
    std::optional<communication::ModbusConnectionConfig> connectionConfig;
    QString applicationVersion;
    QString sourceRevision;
    OperatorReportMetadata operatorMetadata;
    std::optional<SessionLogArtifactInput> sessionLogArtifact;
    QTimeZone displayTimeZone{QTimeZone::UTC};
};

struct ReportTimestamp {
    QDateTime utc;
    QString utcIso;
    QString localIso;
    QByteArray timeZoneId;
};

struct ReportConnectionMetadata {
    bool available = false;
    QString missingDisplay = QStringLiteral("未采集");
    QString portName;
    qint32 baudRate = 0;
    quint8 dataBits = 0;
    QString parity;
    QString stopBits;
    QString flowControl;
    quint8 serverAddress = 0;
    qint64 defaultResponseTimeoutMs = 0;
    qsizetype maxPendingRequests = 0;
    MetadataSource source = MetadataSource::Unavailable;
};

struct ReportMetadata {
    ReportMetadataValue projectName;
    ReportMetadataValue testObject;
    ReportMetadataValue deviceModel;
    ReportMetadataValue testBench;
    ReportMetadataValue environmentDescription;
    ReportMetadataValue firmwareVersion;
    ReportMetadataValue hostVersion;
    ReportMetadataValue sourceRevision;
    ReportMetadataValue tester;
    ReportMetadataValue sessionId;
    ReportConnectionMetadata connection;
    QMap<QString, ReportMetadataValue> suiteEntries;
};

struct ReportSummary {
    quint64 total = 0;
    quint64 executed = 0;
    quint64 passed = 0;
    quint64 failed = 0;
    quint64 errors = 0;
    quint64 skipped = 0;
    quint64 passRateNumerator = 0;
    quint64 passRateDenominator = 0;
    std::optional<quint32> passRatePpm;
    std::chrono::milliseconds duration{0};
    testing::TestStatus status = testing::TestStatus::NotRun;
};

struct ReportFrame {
    ReportFramePresence presence = ReportFramePresence::Missing;
    QByteArray bytes;
    QString uppercaseHex;
};

struct ReportExecutionError {
    QString code;
    QString diagnostic;
    std::optional<QString> communicationCategory;
    std::optional<QString> communicationCode;
    std::optional<quint8> exceptionCode;
};

struct ReportAssertionDifference {
    QString code;
    std::optional<qsizetype> index;
    std::optional<qint64> expected;
    std::optional<qint64> expectedMinimum;
    std::optional<qint64> expectedMaximum;
    std::optional<qint64> actual;
    std::optional<quint16> actualRaw;
    std::optional<qint64> scaledDenominator;
    QString unit;
    QString reason;
};

struct ReportExpectedElement {
    qsizetype index = 0;
    QString type;
    QString representation;
    qint64 value = 0;
    qint64 minimum = 0;
    qint64 maximum = 0;
    quint16 mask = 0;
    int decimalPlaces = 0;
    QString unit;
};

struct ReportUInt32Expectation {
    QString wordOrder;
    QString comparison;
    quint64 minimum = 0;
    quint64 maximum = 0;
    int decimalPlaces = 0;
    QString unit;
};

struct ReportStabilityExpectation {
    quint64 minimumTotal = 0;
    quint64 minimumSuccesses = 0;
    quint64 maximumFailures = 0;
    quint64 maximumTimeouts = 0;
    quint32 maximumFailureRatePpm = 0;
    bool requireValidRttSamples = false;
    qint64 minimumRttMs = 0;
    qint64 maximumAverageRttMs = 0;
    qint64 maximumRttMs = 0;
};

struct ReportScaledInteger {
    qint64 numerator = 0;
    qint64 denominator = 1;
};

struct ReportRequestDefinition {
    quint8 functionCode = 0;
    quint16 address = 0;
    quint16 count = 0;
    quint16 rawValue = 0;
    bool readBeforeWrite = false;
    bool restoreOriginal = false;
};

struct ReportAssertion {
    QString type;
    QString expectedSummary;
    QString actualSummary;
    testing::TestStatus status = testing::TestStatus::NotRun;
    QVector<qint64> expectedValues;
    QVector<ReportExpectedElement> expectedElements;
    std::optional<ReportUInt32Expectation> expectedUInt32;
    std::optional<ReportStabilityExpectation> expectedStability;
    QVector<qint64> actualValues;
    QVector<quint16> actualRawValues;
    QVector<QVector<quint16>> actualRawSamples;
    QVector<ReportScaledInteger> actualScaledValues;
    std::optional<quint8> expectedExceptionCode;
    std::optional<quint8> actualExceptionCode;
    QString unit;
    QVector<ReportAssertionDifference> differences;
};

struct ReportTransactionEvidence {
    quint64 attemptSequence = 0;
    QString purpose;
    int stepAttempt = 0;
    bool retry = false;
    QString retryReason;
    QString logicalStepId;
    qsizetype logicalStepIndex = -1;
    int repetition = 0;
    quint64 requestId = 0;
    quint8 serverAddress = 0;
    quint8 functionCode = 0;
    QString descriptorKind;
    quint16 address = 0;
    std::optional<quint16> count;
    std::optional<quint16> rawValue;
    QString requestState;
    std::optional<ReportTimestamp> started;
    std::optional<ReportTimestamp> finished;
    std::chrono::milliseconds duration{0};
    ReportFrame tx;
    ReportFrame rx;
    QString txCrcStatus;
    QString rxCrcStatus;
    std::optional<qint64> rttNanoseconds;
    std::optional<QString> rttMilliseconds;
    std::optional<ReportExecutionError> error;
};

struct ReportCompositeStep {
    QString stepId;
    qsizetype stepIndex = -1;
    int repetition = 0;
    QString declaredType;
    QString normalizedType;
    std::chrono::milliseconds delayBefore{0};
    std::optional<ReportRequestDefinition> request;
    testing::TestStatus status = testing::TestStatus::NotRun;
    ReportAssertion assertion;
    std::optional<ReportExecutionError> error;
    std::optional<ReportTimestamp> started;
    std::optional<ReportTimestamp> finished;
    std::chrono::milliseconds duration{0};
    QVector<quint64> attemptSequences;
};

struct ReportEvidenceRetention {
    QString policy;
    quint64 totalAttempts = 0;
    quint64 retainedAttempts = 0;
    quint64 droppedAttempts = 0;
    quint64 totalFailures = 0;
    quint64 retainedFailures = 0;
    int configuredLimit = 0;
    QString description;
};

struct ReportStabilityStatistics {
    quint64 total = 0;
    quint64 successes = 0;
    quint64 failures = 0;
    quint64 timeouts = 0;
    quint64 validRttSamples = 0;
    quint64 missingRttSamples = 0;
    std::optional<qint64> minimumRttMs;
    std::optional<qint64> averageRttMs;
    std::optional<qint64> maximumRttMs;
};

struct ReportGuidedOperatorAction {
    quint64 runId = 0;
    QString caseId;
    QString stepId;
    QString oneTimeToken;
    QString promptPurpose;
    QString title;
    QString instruction;
    QString safetyNotice;
    QStringList allowedActions;
    QString cancelRecoveryInstruction;
    std::optional<QString> action;
    std::optional<ReportTimestamp> promptShown;
    std::optional<ReportTimestamp> actionTime;
    std::chrono::milliseconds waitDuration{0};
    QString note;
};

struct ReportGuidedObservation {
    quint64 runId = 0;
    QString caseId;
    QString stepId;
    QString target;
    QString outcome;
    std::optional<ReportRequestDefinition> probe;
    std::chrono::milliseconds interval{0};
    std::chrono::milliseconds deadline{0};
    std::optional<QString> businessExpectedSummary;
    std::optional<ReportTimestamp> started;
    std::optional<ReportTimestamp> finished;
    std::chrono::milliseconds duration{0};
    int requiredConsecutiveMatches = 0;
    int achievedConsecutiveMatches = 0;
    std::optional<quint64> firstMatchingRequestId;
    std::optional<quint64> stableMatchingRequestId;
    QVector<quint64> attemptSequences;
    std::optional<ReportExecutionError> error;
};

struct ReportGuidedRecovery {
    QString finalState;
    QString terminalReason;
    QVector<ReportGuidedOperatorAction> operatorActions;
    QVector<ReportGuidedObservation> observations;
    std::optional<std::chrono::milliseconds> toFirstSuccessfulResponse;
    std::optional<std::chrono::milliseconds> toStableRecovery;
    bool physicalLinkRestored = false;
    bool recoveryInstructionRequired = false;
    QString recoveryInstruction;
};

struct ReportCase {
    QString id;
    QString name;
    QString category;
    QString description;
    QString environment;
    QStringList tags;
    QString declaredType;
    QString normalizedType;
    std::optional<ReportRequestDefinition> request;
    testing::TestStatus status = testing::TestStatus::NotRun;
    std::optional<QString> skipReason;
    ReportAssertion assertion;
    std::optional<ReportExecutionError> error;
    std::optional<ReportExecutionError> cleanupError;
    std::optional<ReportTimestamp> started;
    std::optional<ReportTimestamp> finished;
    std::chrono::milliseconds duration{0};
    QVector<ReportCompositeStep> steps;
    QVector<ReportTransactionEvidence> attempts;
    std::optional<ReportStabilityStatistics> stability;
    std::optional<ReportGuidedRecovery> guidedRecovery;
    ReportEvidenceRetention evidenceRetention;
    QString sessionId;
};

struct ReportSessionLogArtifact {
    bool available = false;
    QString sessionId;
    QString path;
    qint64 sizeBytes = 0;
    QString sha256;
    QString missingDisplay = QStringLiteral("未提供");
};

struct ReportDocumentModel {
    quint64 runId = 0;
    int schemaVersion = 0;
    QString suiteId;
    QString suiteName;
    QString suiteDescription;
    QStringList suiteTags;
    testing::TestStatus status = testing::TestStatus::NotRun;
    bool aborted = false;
    ReportTimestamp started;
    ReportTimestamp finished;
    std::chrono::milliseconds duration{0};
    ReportMetadata metadata;
    ReportSummary summary;
    QVector<ReportCase> cases;
    QVector<ReportExecutionError> auxiliaryErrors;
    ReportSessionLogArtifact sessionLogArtifact;
};

struct ReportBuildResult {
    std::optional<ReportDocumentModel> document;
    QVector<ReportError> errors;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return document.has_value() && errors.isEmpty();
    }
};

[[nodiscard]] QString metadataSourceName(MetadataSource source);
[[nodiscard]] QString reportErrorCodeName(ReportErrorCode code);

} // namespace oms555tv::report
