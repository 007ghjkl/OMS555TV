#include "testing/GuidedTestModel.h"
#include "testing/TestCaseLoader.h"
#include "testing/TestEngineTypes.h"
#include "testing/TestResultManager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <limits>

using namespace oms555tv::testing;

namespace {

QByteArray readFile(const QString &relative)
{
    QFile file(QStringLiteral(OMS555TV_TESTCASES_DIR) + QLatin1Char('/') + relative);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

LoadResult loadFixture(const QString &relative)
{
    return TestCaseLoader::load(readFile(relative));
}

bool hasError(const LoadResult &result, ConfigErrorCode code, const QString &path = {})
{
    for (const auto &error : result.errors) {
        if (error.code == code && (path.isEmpty() || error.path == path)) return true;
    }
    return false;
}

} // namespace

class GuidedTestModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadsGuidedRecoveryIntoNormalizedModel()
    {
        const auto result = loadFixture(QStringLiteral("fixtures/v3/valid/guided-recovery.json"));
        if (!result.succeeded()) {
            for (const auto &error : result.errors) {
                qDebug() << configErrorCodeName(error.code) << error.path << error.diagnostic;
            }
        }
        QVERIFY(result.succeeded());
        QCOMPARE(result.suite->schemaVersion, testSuiteSchemaVersionV3);
        QCOMPARE(result.suite->cases.size(), 1);
        const auto &testCase = result.suite->cases.front();
        QCOMPARE(testCase.type, TestCaseType::GuidedRecovery);
        QCOMPARE(testCase.environment, ExecutionEnvironment::RealRs485);
        QCOMPARE(testCase.guidedRecovery.steps.size(), guidedRecoveryStepCount);

        const auto &disconnect = testCase.guidedRecovery.steps[0];
        QCOMPARE(disconnect.type, GuidedStepType::OperatorPrompt);
        QVERIFY(disconnect.operatorStep.has_value());
        QCOMPARE(disconnect.operatorStep->purpose, GuidedPromptPurpose::DisconnectRs485);
        QCOMPARE(disconnect.operatorStep->allowedActions.size(), 2);
        QVERIFY(disconnect.operatorStep->allowedActions.contains(GuidedOperatorAction::Confirm));
        QVERIFY(disconnect.operatorStep->allowedActions.contains(GuidedOperatorAction::Cancel));
        QVERIFY(!disconnect.operatorStep->cancelRecoveryInstruction.isEmpty());

        const auto &outage = testCase.guidedRecovery.steps[1];
        QCOMPARE(outage.type, GuidedStepType::ObserveOutage);
        QCOMPARE(outage.observationStep->target,
                 GuidedObservationTarget::ConsecutiveResponseTimeouts);
        QCOMPARE(outage.observationStep->probe.function,
                 ModbusFunction::ReadHoldingRegisters);
        QVERIFY(!outage.observationStep->businessAssertion.has_value());

        const auto &recovery = testCase.guidedRecovery.steps[3];
        QCOMPARE(recovery.type, GuidedStepType::ObserveRecovery);
        QCOMPARE(recovery.observationStep->target,
                 GuidedObservationTarget::ConsecutiveValidResponses);
        QVERIFY(recovery.observationStep->businessAssertion.has_value());
        QCOMPARE(recovery.observationStep->businessAssertion->type, AssertionType::Range);
    }

    void acceptsDocumentedV3Bounds()
    {
        const auto result = loadFixture(QStringLiteral("fixtures/v3/valid/boundaries.json"));
        QVERIFY(result.succeeded());
        const auto &testCase = result.suite->cases.front();
        QCOMPARE(testCase.timeout.request.count(), maximumRequestTimeoutMs);
        QCOMPARE(testCase.timeout.testCase.count(), maximumGuidedCaseTimeoutMs);
        QCOMPARE(testCase.guidedRecovery.steps[0].operatorStep->waitTimeout.count(),
                 maximumOperatorWaitMs);
        QCOMPARE(testCase.guidedRecovery.steps[1].observationStep->interval.count(),
                 maximumObservationIntervalMs);
        QCOMPARE(testCase.guidedRecovery.steps[1].observationStep->deadline.count(),
                 maximumObservationDeadlineMs);
        QCOMPARE(testCase.guidedRecovery.steps[1].observationStep->consecutiveMatches, 12);
    }

    void rejectsInvalidV3FixturesWithStructuredErrors()
    {
        const QDir directory(QStringLiteral(OMS555TV_TESTCASES_DIR)
                             + QStringLiteral("/fixtures/v3/invalid"));
        const auto files = directory.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        QCOMPARE(files.size(), 7);
        for (const QString &file : files) {
            const auto result = loadFixture(QStringLiteral("fixtures/v3/invalid/") + file);
            QVERIFY2(!result.succeeded(), qPrintable(file));
            QVERIFY2(!result.suite.has_value(), qPrintable(file));
            QVERIFY2(!result.errors.isEmpty(), qPrintable(file));
            QVERIFY2(!result.errors.front().path.isEmpty(), qPrintable(file));
            QCOMPARE(result.errors.front().suiteId.isEmpty(), false);
        }
        QVERIFY(hasError(loadFixture(QStringLiteral("fixtures/v3/invalid/unknown-step.json")),
                         ConfigErrorCode::UnknownStepType, QStringLiteral("/cases/0/steps/1/type")));
        QVERIFY(hasError(loadFixture(QStringLiteral("fixtures/v3/invalid/unknown-action.json")),
                         ConfigErrorCode::UnknownAction,
                         QStringLiteral("/cases/0/steps/0/allowed_actions/1")));
        QVERIFY(hasError(loadFixture(
                    QStringLiteral("fixtures/v3/invalid/missing-recovery-instruction.json")),
                         ConfigErrorCode::MissingField,
                         QStringLiteral("/cases/0/steps/0/cancel_recovery_instruction")));
        QVERIFY(hasError(loadFixture(QStringLiteral("fixtures/v3/invalid/invalid-deadline.json")),
                         ConfigErrorCode::InvalidCombination,
                         QStringLiteral("/cases/0/steps/1/deadline_ms")));
        QVERIFY(hasError(loadFixture(QStringLiteral("fixtures/v3/invalid/invalid-interval.json")),
                         ConfigErrorCode::OutOfRange,
                         QStringLiteral("/cases/0/steps/1/interval_ms")));
        QVERIFY(hasError(loadFixture(QStringLiteral("fixtures/v3/invalid/duplicate-step-id.json")),
                         ConfigErrorCode::DuplicateId,
                         QStringLiteral("/cases/0/steps/2/id")));
        const auto unsafe = loadFixture(QStringLiteral("fixtures/v3/invalid/unsafe-write-probe.json"));
        QVERIFY(hasError(unsafe, ConfigErrorCode::OutOfRange,
                         QStringLiteral("/cases/0/steps/1/probe/function")));
    }

    void v3SchemaAndChineseExampleMatchConstants()
    {
        QJsonParseError error;
        const auto schema = QJsonDocument::fromJson(
            readFile(QStringLiteral("schema/test-suite-v3.schema.json")), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(schema.isObject());
        const auto root = schema.object();
        QCOMPARE(root.value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("schema_version")).toObject()
                     .value(QStringLiteral("const")).toInt(), testSuiteSchemaVersionV3);
        const auto definitions = root.value(QStringLiteral("$defs")).toObject();
        QCOMPARE(definitions.value(QStringLiteral("guidedRecoveryCase")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("steps")).toObject()
                     .value(QStringLiteral("maxItems")).toInt(), guidedRecoveryStepCount);
        QCOMPARE(definitions.value(QStringLiteral("timeout")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("case_ms")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumGuidedCaseTimeoutMs);
        QCOMPARE(definitions.value(QStringLiteral("disconnectPrompt")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("wait_timeout_ms")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumOperatorWaitMs);
        QCOMPARE(definitions.value(QStringLiteral("outageObservation")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("deadline_ms")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumObservationDeadlineMs);
        const auto example = loadFixture(QStringLiteral("examples/phase7-schema-v3-example.json"));
        QVERIFY(example.succeeded());
        QCOMPARE(example.suite->cases.size(), 1);
    }

    void keepsV1V2AndFormalSuitesCompatible()
    {
        const QStringList fixtures{
            QStringLiteral("fixtures/valid/minimal.json"),
            QStringLiteral("fixtures/valid/all-types.json"),
            QStringLiteral("fixtures/v2/valid/all-features.json"),
            QStringLiteral("functional/phase5-smoke.json"),
            QStringLiteral("phase6/phase6-rs485-full.json"),
            QStringLiteral("phase6/phase6-fake-boundaries.json"),
        };
        for (const auto &fixture : fixtures) {
            const auto result = loadFixture(fixture);
            QVERIFY2(result.succeeded(), qPrintable(fixture));
            QVERIFY(result.suite->schemaVersion == testSuiteSchemaVersion
                    || result.suite->schemaVersion == testSuiteSchemaVersionV2);
        }
    }

    void rejectsStaleDuplicateAndCrossRunActionsWithoutMutation()
    {
        const GuidedActionContext context{
            42, QStringLiteral("TC-G001"), QStringLiteral("disconnect"),
            QStringLiteral("token-1"),
            {GuidedOperatorAction::Confirm, GuidedOperatorAction::Cancel}, true, false};
        GuidedActionCommand command{42, QStringLiteral("TC-G001"),
                                    QStringLiteral("disconnect"), QStringLiteral("token-1"),
                                    GuidedOperatorAction::Confirm, QStringLiteral("已断开")};
        QCOMPARE(validateGuidedAction(context, command), GuidedActionRejection::None);
        QCOMPARE(context.awaitingAction, true);
        QCOMPARE(context.tokenConsumed, false);

        auto altered = command;
        altered.runId = 43;
        QCOMPARE(validateGuidedAction(context, altered), GuidedActionRejection::RunMismatch);
        altered = command;
        altered.caseId = QStringLiteral("other");
        QCOMPARE(validateGuidedAction(context, altered), GuidedActionRejection::CaseMismatch);
        altered = command;
        altered.stepId = QStringLiteral("reconnect");
        QCOMPARE(validateGuidedAction(context, altered), GuidedActionRejection::StepMismatch);
        altered = command;
        altered.oneTimeToken = QStringLiteral("stale");
        QCOMPARE(validateGuidedAction(context, altered), GuidedActionRejection::TokenMismatch);

        auto consumed = context;
        consumed.tokenConsumed = true;
        QCOMPARE(validateGuidedAction(consumed, command),
                 GuidedActionRejection::TokenAlreadyConsumed);
        auto notWaiting = context;
        notWaiting.awaitingAction = false;
        QCOMPARE(validateGuidedAction(notWaiting, command),
                 GuidedActionRejection::NotAwaitingAction);
    }

    void calculatesBothRecoveryDurationsAndRejectsInvalidTime()
    {
        auto result = calculateRecoveryTiming({1000, 1000, 2800});
        QVERIFY(result.succeeded());
        QCOMPARE(result.timing->toFirstSuccessfulResponse.count(), 0);
        QCOMPARE(result.timing->toStableRecovery.count(), 1800);

        result = calculateRecoveryTiming({1000, {}, 1200});
        QCOMPARE(result.error, RecoveryTimingError::MissingFirstSuccess);
        result = calculateRecoveryTiming({1000, 900, 1200});
        QCOMPARE(result.error, RecoveryTimingError::FirstSuccessBeforeConfirmation);
        result = calculateRecoveryTiming({1000, 1200, 1100});
        QCOMPARE(result.error, RecoveryTimingError::StableSuccessBeforeFirstSuccess);
        const quint64 tooLarge = static_cast<quint64>(std::numeric_limits<qint64>::max()) + 1ULL;
        result = calculateRecoveryTiming({0, tooLarge, tooLarge});
        QCOMPARE(result.error, RecoveryTimingError::DurationOverflow);
    }

    void classifiesOnlyStructuredOutageAndRecoveryEvidenceAsMatches()
    {
        oms555tv::communication::ModbusRequestResult timeout;
        timeout.state = oms555tv::communication::RequestState::Failed;
        timeout.error = oms555tv::communication::CommunicationError{};
        timeout.error->code = oms555tv::communication::ErrorCode::ResponseTimeout;
        QCOMPARE(classifyGuidedProbe(GuidedObservationTarget::ConsecutiveResponseTimeouts,
                                     timeout), GuidedProbeClassification::Match);
        QCOMPARE(classifyGuidedProbe(GuidedObservationTarget::ConsecutiveValidResponses,
                                     timeout), GuidedProbeClassification::NonMatch);

        auto cancelled = timeout;
        cancelled.error->code = oms555tv::communication::ErrorCode::CancelledByCaller;
        QCOMPARE(classifyGuidedProbe(GuidedObservationTarget::ConsecutiveResponseTimeouts,
                                     cancelled), GuidedProbeClassification::Cancelled);
        auto permission = timeout;
        permission.error->code = oms555tv::communication::ErrorCode::PermissionError;
        QCOMPARE(classifyGuidedProbe(GuidedObservationTarget::ConsecutiveResponseTimeouts,
                                     permission), GuidedProbeClassification::FatalCommunicationError);

        oms555tv::communication::ModbusRequestResult success;
        success.state = oms555tv::communication::RequestState::Succeeded;
        oms555tv::communication::ReadHoldingRegistersResult read;
        read.values = {42};
        success.success = read;
        QCOMPARE(classifyGuidedProbe(GuidedObservationTarget::ConsecutiveResponseTimeouts,
                                     success), GuidedProbeClassification::NonMatch);
        QCOMPARE(classifyGuidedProbe(GuidedObservationTarget::ConsecutiveValidResponses,
                                     success), GuidedProbeClassification::Match);
        ExpectedAssertion expected;
        expected.type = AssertionType::Equals;
        expected.value = 42;
        QCOMPARE(classifyGuidedProbe(GuidedObservationTarget::ConsecutiveValidResponses,
                                     success, expected), GuidedProbeClassification::Match);
        expected.value = 43;
        QCOMPARE(classifyGuidedProbe(GuidedObservationTarget::ConsecutiveValidResponses,
                                     success, expected), GuidedProbeClassification::NonMatch);
    }

    void terminalPriorityIsDeterministic()
    {
        QCOMPARE(selectGuidedTerminalReason({GuidedTerminalReason::Pass,
                                             GuidedTerminalReason::RecoveryTimeout}),
                 GuidedTerminalReason::RecoveryTimeout);
        QCOMPARE(selectGuidedTerminalReason({GuidedTerminalReason::OperatorTimeout,
                                             GuidedTerminalReason::FatalCommunicationError,
                                             GuidedTerminalReason::OperatorCancelled}),
                 GuidedTerminalReason::OperatorCancelled);
        QCOMPARE(selectGuidedTerminalReason({GuidedTerminalReason::UserAborted,
                                             GuidedTerminalReason::InternalError}),
                 GuidedTerminalReason::InternalError);
    }

    void resultManagerPublishesImmutableGuidedEvidence()
    {
        TestResultManager manager;
        TestSuiteResult source;
        source.runId = {7};
        TestCaseResult caseResult;
        caseResult.caseId = QStringLiteral("TC-G001");
        caseResult.type = TestCaseType::GuidedRecovery;
        GuidedRecoveryCaseResult guided;
        guided.finalState = GuidedRunState::Finished;
        guided.terminalReason = GuidedTerminalReason::Pass;
        guided.physicalLinkRestored = true;
        guided.recoveryTiming = RecoveryTiming{std::chrono::milliseconds(200),
                                               std::chrono::milliseconds(700)};
        GuidedOperatorActionRecord action;
        action.runId = {7};
        action.caseId = caseResult.caseId;
        action.stepId = QStringLiteral("reconnect");
        action.oneTimeToken = QStringLiteral("token-2");
        action.action = GuidedOperatorAction::Confirm;
        guided.operatorActions.append(action);
        GuidedObservationResult observation;
        observation.runId = {7};
        observation.caseId = caseResult.caseId;
        observation.stepId = QStringLiteral("recovery");
        observation.target = GuidedObservationTarget::ConsecutiveValidResponses;
        observation.outcome = GuidedObservationOutcome::Matched;
        observation.requiredConsecutiveMatches = 3;
        observation.achievedConsecutiveMatches = 3;
        observation.firstMatchingRequestId = oms555tv::communication::RequestId{101};
        observation.stableMatchingRequestId = oms555tv::communication::RequestId{103};
        guided.observations.append(observation);
        caseResult.guidedRecovery = guided;
        source.cases.append(caseResult);

        manager.publish(source, true);
        source.cases[0].guidedRecovery->terminalReason = GuidedTerminalReason::InternalError;
        const auto snapshot = manager.snapshot();
        QVERIFY(snapshot);
        QCOMPARE(snapshot->cases[0].guidedRecovery->terminalReason, GuidedTerminalReason::Pass);
        QCOMPARE(snapshot->cases[0].guidedRecovery->observations[0]
                     .firstMatchingRequestId->value, quint64(101));
        QCOMPARE(snapshot->cases[0].guidedRecovery->recoveryTiming
                     ->toStableRecovery.count(), 700);
    }
};

QTEST_APPLESS_MAIN(GuidedTestModelTest)
#include "tst_guidedtestmodel.moc"
