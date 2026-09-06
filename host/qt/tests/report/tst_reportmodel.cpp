#include "report/ReportModelBuilder.h"

#include <QTest>

#include <algorithm>

using namespace oms555tv;

namespace {

const QDateTime startedUtc = QDateTime::fromString(
    QStringLiteral("2026-09-06T01:02:03.000Z"), Qt::ISODateWithMs);

testing::TestCase configuredCase(QString id, testing::TestCaseType type,
                                 QString name = {})
{
    testing::TestCase result;
    result.id = std::move(id);
    result.name = name.isEmpty() ? result.id + QStringLiteral(" 名称") : std::move(name);
    result.category = QStringLiteral("functional");
    result.description = QStringLiteral("前置条件与操作说明");
    result.type = type;
    result.declaredType = type;
    result.tags = {QStringLiteral("report")};
    result.request.function = testing::ModbusFunction::ReadHoldingRegisters;
    result.request.address = device::PduAddress(0);
    result.request.count = 1;
    result.expected.type = testing::AssertionType::Equals;
    result.expected.value = 1;
    return result;
}

testing::TestCaseResult terminalResult(const testing::TestCase &configured,
                                       testing::TestStatus status)
{
    testing::TestCaseResult result;
    result.caseId = configured.id;
    result.type = configured.type;
    result.declaredType = configured.declaredType;
    result.status = status;
    result.expected = configured.expected;
    if (status != testing::TestStatus::Skipped) {
        result.startedUtc = startedUtc.addSecs(1);
        result.finishedUtc = startedUtc.addSecs(2);
        result.duration = std::chrono::milliseconds(1000);
    }
    return result;
}

report::ReportInput inputFor(const QVector<testing::TestCase> &cases,
                             const QVector<testing::TestCaseResult> &results,
                             testing::TestStatus status,
                             int schemaVersion = testing::testSuiteSchemaVersion)
{
    auto suite = std::make_shared<testing::TestSuite>();
    suite->schemaVersion = schemaVersion;
    suite->id = QStringLiteral("suite-report");
    suite->name = QStringLiteral("报告测试套件");
    suite->description = QStringLiteral("纯数据报告映射");
    suite->tags = {QStringLiteral("phase8"), QStringLiteral("中文")};
    suite->cases = cases;

    report::ReportInput input;
    input.suiteResult.runId = testing::TestRunId{42};
    input.suiteResult.suite = std::move(suite);
    input.suiteResult.status = status;
    input.suiteResult.startedUtc = startedUtc;
    input.suiteResult.finishedUtc = startedUtc.addSecs(10);
    input.suiteResult.duration = std::chrono::milliseconds(9876);
    input.suiteResult.cases = results;
    input.suiteResult.sessionId = QStringLiteral("session-42");
    input.displayTimeZone = QTimeZone(QByteArray("Asia/Shanghai"));
    return input;
}

testing::TestRequestAttemptResult attempt(quint64 sequence, quint64 requestId,
                                          QByteArray tx = QByteArray::fromHex("010300000001840A"),
                                          QByteArray rx = QByteArray::fromHex("0103020001B9F0"),
                                          bool withRtt = true)
{
    testing::TestRequestAttemptResult result;
    result.sequence = sequence;
    result.purpose = testing::TestStepPurpose::Read;
    result.requestId = communication::RequestId{requestId};
    result.startedUtc = startedUtc.addMSecs(static_cast<qint64>(sequence) * 10);
    result.finishedUtc = result.startedUtc.addMSecs(5);
    result.duration = std::chrono::milliseconds(5);
    result.requestResult.requestId = result.requestId;
    result.requestResult.state = communication::RequestState::Succeeded;
    result.requestResult.descriptor = communication::ReadRequestDescriptor{
        device::PduAddress(0), 1};
    result.requestResult.evidence.requestId = result.requestId;
    result.requestResult.evidence.serverAddress = 1;
    result.requestResult.evidence.functionCode = 0x03;
    result.requestResult.evidence.txAdu = std::move(tx);
    result.requestResult.evidence.rxAdu = std::move(rx);
    result.requestResult.evidence.txCrcStatus = communication::CrcStatus::Valid;
    result.requestResult.evidence.rxCrcStatus = communication::CrcStatus::Valid;
    result.requestResult.evidence.enqueuedUtc = result.startedUtc;
    result.requestResult.evidence.completedUtc = result.finishedUtc;
    if (withRtt) result.requestResult.evidence.rtt = std::chrono::microseconds(4500);
    return result;
}

void retainAll(testing::TestCaseResult &result)
{
    result.evidenceRetention.policy = testing::TestEvidenceRetentionPolicy::Complete;
    result.evidenceRetention.totalAttempts = static_cast<quint64>(result.attempts.size());
    result.evidenceRetention.retainedAttempts = static_cast<quint64>(result.attempts.size());
    result.evidenceRetention.description = QStringLiteral("完整保留全部 attempt");
}

testing::AssertionResult passedAssertion(QString expected = QStringLiteral("期望"),
                                         QString actual = QStringLiteral("实际"))
{
    testing::AssertionResult result;
    result.status = testing::TestStatus::Pass;
    result.type = testing::AssertionType::Equals;
    result.expectedSummary = std::move(expected);
    result.actualSummary = std::move(actual);
    return result;
}

} // namespace

class ReportModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void mixedSummaryMetadataAndArtifact()
    {
        QVector<testing::TestCase> cases;
        QVector<testing::TestCaseResult> results;
        const QVector<testing::TestStatus> statuses{
            testing::TestStatus::Pass, testing::TestStatus::Fail,
            testing::TestStatus::Error, testing::TestStatus::Skipped};
        for (qsizetype index = 0; index < statuses.size(); ++index) {
            cases.append(configuredCase(QStringLiteral("TC-%1").arg(index + 1),
                                        testing::TestCaseType::ReadRegisters));
            results.append(terminalResult(cases.back(), statuses.at(index)));
        }
        auto input = inputFor(cases, results, testing::TestStatus::Error);
        auto suite = std::make_shared<testing::TestSuite>(*input.suiteResult.suite);
        suite->metadata.insert(QStringLiteral("hardware"),
                               QStringLiteral("NUCLEO-F411RE"));
        suite->metadata.insert(QStringLiteral("hardware_scope"),
                               QStringLiteral("安全低压短台架"));
        input.suiteResult.suite = std::move(suite);
        input.applicationVersion = QStringLiteral("0.1.0");
        input.sourceRevision = QStringLiteral("abc123");
        input.operatorMetadata.tester = QStringLiteral("测试员甲");
        input.operatorMetadata.deviceModel = QStringLiteral("不得覆盖套件值");
        input.operatorMetadata.testBench = QStringLiteral("台架 A");
        communication::ModbusConnectionConfig connection;
        connection.serial.portName = QStringLiteral("COM6");
        input.connectionConfig = connection;
        input.sessionLogArtifact = report::SessionLogArtifactInput{
            true, QStringLiteral("session-42"), QStringLiteral("output/logs/session.jsonl"),
            1234, QString(64, QLatin1Char('a'))};

        const auto built = report::ReportModelBuilder::build(input);
        QVERIFY2(built.succeeded(), built.errors.isEmpty()
            ? "unknown" : qPrintable(built.errors.front().diagnostic));
        const auto &document = *built.document;
        QCOMPARE(document.summary.total, quint64(4));
        QCOMPARE(document.summary.executed, quint64(3));
        QCOMPARE(document.summary.passed, quint64(1));
        QCOMPARE(document.summary.failed, quint64(1));
        QCOMPARE(document.summary.errors, quint64(1));
        QCOMPARE(document.summary.skipped, quint64(1));
        QCOMPARE(document.summary.passRateNumerator, quint64(1));
        QCOMPARE(document.summary.passRateDenominator, quint64(3));
        QCOMPARE(*document.summary.passRatePpm, quint32(333333));
        QCOMPARE(document.status, testing::TestStatus::Error);
        QCOMPARE(document.started.utcIso, QStringLiteral("2026-09-06T01:02:03.000Z"));
        QVERIFY(document.started.localIso.startsWith(QStringLiteral("2026-09-06T09:02:03.000")));
        QCOMPARE(document.metadata.projectName.value, QStringLiteral("OMS555TV"));
        QCOMPARE(document.metadata.projectName.source, report::MetadataSource::SystemObserved);
        QCOMPARE(document.metadata.testObject.source, report::MetadataSource::SuiteConfigured);
        QCOMPARE(document.metadata.deviceModel.value, QStringLiteral("NUCLEO-F411RE"));
        QCOMPARE(document.metadata.deviceModel.source, report::MetadataSource::SuiteConfigured);
        QCOMPARE(document.metadata.testBench.value, QStringLiteral("台架 A"));
        QCOMPARE(document.metadata.testBench.source, report::MetadataSource::OperatorEntered);
        QCOMPARE(document.metadata.tester.source, report::MetadataSource::OperatorEntered);
        QVERIFY(!document.metadata.firmwareVersion.available);
        QCOMPARE(document.metadata.firmwareVersion.missingDisplay, QStringLiteral("未采集"));
        QVERIFY(document.metadata.connection.available);
        QCOMPARE(document.metadata.connection.portName, QStringLiteral("COM6"));
        QCOMPARE(document.metadata.connection.source, report::MetadataSource::SystemObserved);
        QVERIFY(document.sessionLogArtifact.available);
        QCOMPARE(document.sessionLogArtifact.sha256, QString(64, QLatin1Char('A')));
        QCOMPARE(report::metadataSourceName(report::MetadataSource::Unavailable),
                 QStringLiteral("unavailable"));
    }

    void emptyAndAllSkippedUseEngineSemantics()
    {
        auto empty = inputFor({}, {}, testing::TestStatus::Pass);
        auto built = report::ReportModelBuilder::build(empty);
        QVERIFY(built.succeeded());
        QCOMPARE(built.document->summary.total, quint64(0));
        QVERIFY(!built.document->summary.passRatePpm);

        const auto one = configuredCase(QStringLiteral("TC-SKIP"),
                                        testing::TestCaseType::ReadRegisters);
        auto skippedResult = terminalResult(one, testing::TestStatus::Skipped);
        skippedResult.skipReason = testing::TestSkipReason::Disabled;
        auto v1 = inputFor({one}, {skippedResult}, testing::TestStatus::Pass,
                           testing::testSuiteSchemaVersion);
        built = report::ReportModelBuilder::build(v1);
        QVERIFY(built.succeeded());
        QCOMPARE(built.document->summary.skipped, quint64(1));
        QVERIFY(!built.document->summary.passRatePpm);

        auto v3 = inputFor({one}, {skippedResult}, testing::TestStatus::Skipped,
                           testing::testSuiteSchemaVersionV3);
        built = report::ReportModelBuilder::build(v3);
        QVERIFY(built.succeeded());
        QCOMPARE(built.document->status, testing::TestStatus::Skipped);
    }

    void rejectsNonTerminalTimesIdentityAndStatusMismatch()
    {
        const auto configured = configuredCase(QStringLiteral("TC-1"),
                                               testing::TestCaseType::ReadRegisters);
        auto result = terminalResult(configured, testing::TestStatus::Pass);

        auto input = inputFor({configured}, {result}, testing::TestStatus::Running);
        auto built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QCOMPARE(built.errors.front().code, report::ReportErrorCode::NonTerminalStatus);

        input = inputFor({configured}, {result}, testing::TestStatus::Pass);
        input.suiteResult.startedUtc = {};
        built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QVERIFY(std::any_of(built.errors.cbegin(), built.errors.cend(), [](const auto &error) {
            return error.code == report::ReportErrorCode::InvalidTimestamp;
        }));

        result.caseId = QStringLiteral("wrong");
        input = inputFor({configured}, {result}, testing::TestStatus::Pass);
        built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QCOMPARE(built.errors.front().code, report::ReportErrorCode::CaseIdentityMismatch);

        result.caseId = configured.id;
        input = inputFor({configured}, {result}, testing::TestStatus::Fail);
        built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QCOMPARE(built.errors.front().code, report::ReportErrorCode::SuiteStatusMismatch);

        input = inputFor({configured}, {}, testing::TestStatus::Pass);
        built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QCOMPARE(built.errors.front().code,
                 report::ReportErrorCode::SuiteResultCountMismatch);
    }

    void firmwareUsesOnlyAssertedActualReadsAndRejectsConflicts()
    {
        auto firmware = configuredCase(QStringLiteral("FW"),
                                       testing::TestCaseType::ReadRegisters);
        firmware.request.address = device::PduAddress(39);
        firmware.request.count = 2;
        firmware.expected.type = testing::AssertionType::RegisterSequence;
        firmware.expected.values = {0, 2};
        auto result = terminalResult(firmware, testing::TestStatus::Pass);
        result.expected = firmware.expected;
        result.actual = testing::ActualResult::registers({0, 2});
        result.assertion = passedAssertion(QStringLiteral("[0,2]"), QStringLiteral("[0,2]"));
        auto input = inputFor({firmware}, {result}, testing::TestStatus::Pass);

        auto built = report::ReportModelBuilder::build(input);
        QVERIFY(built.succeeded());
        QCOMPARE(built.document->metadata.firmwareVersion.value, QStringLiteral("0.2"));
        QCOMPARE(built.document->metadata.firmwareVersion.source,
                 report::MetadataSource::SystemObserved);

        result.actual.reset();
        input = inputFor({firmware}, {result}, testing::TestStatus::Pass);
        built = report::ReportModelBuilder::build(input);
        QVERIFY(built.succeeded());
        QVERIFY(!built.document->metadata.firmwareVersion.available);

        auto second = firmware;
        second.id = QStringLiteral("FW-2");
        auto secondResult = terminalResult(second, testing::TestStatus::Pass);
        secondResult.expected = second.expected;
        secondResult.actual = testing::ActualResult::registers({0, 3});
        secondResult.assertion = passedAssertion();
        result.actual = testing::ActualResult::registers({0, 2});
        input = inputFor({firmware, second}, {result, secondResult},
                         testing::TestStatus::Pass);
        built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QCOMPARE(built.errors.front().code,
                 report::ReportErrorCode::InconsistentFirmwareVersion);
    }

    void mapsBaseSequenceConsistencyStabilityAndGuidedRecovery()
    {
        QVector<testing::TestCase> cases;
        QVector<testing::TestCaseResult> results;

        auto base = configuredCase(QStringLiteral("BASE"),
                                   testing::TestCaseType::ReadRegisters);
        auto baseResult = terminalResult(base, testing::TestStatus::Pass);
        baseResult.actual = testing::ActualResult::registers({1});
        baseResult.assertion = passedAssertion();
        baseResult.attempts = {attempt(1, 101, QByteArray::fromHex("01AB"), {}, false)};
        retainAll(baseResult);
        cases.append(base);
        results.append(baseResult);

        auto sequence = configuredCase(QStringLiteral("SEQ"), testing::TestCaseType::Sequence);
        testing::SequenceStep configuredStep;
        configuredStep.id = QStringLiteral("read-version");
        configuredStep.request.address = device::PduAddress(39);
        configuredStep.request.count = 2;
        sequence.sequence.steps = {configuredStep};
        auto sequenceResult = terminalResult(sequence, testing::TestStatus::Pass);
        testing::TestCompositeStepResult step;
        step.stepId = configuredStep.id;
        step.stepIndex = 0;
        step.repetition = 2;
        step.status = testing::TestStatus::Pass;
        step.expected = sequence.expected;
        step.actual = testing::ActualResult::registers({1});
        step.assertion = passedAssertion();
        step.startedUtc = startedUtc.addSecs(2);
        step.finishedUtc = startedUtc.addSecs(3);
        step.duration = std::chrono::milliseconds(1000);
        step.attemptSequences = {2};
        sequenceResult.steps = {step};
        auto sequenceAttempt = attempt(2, 102);
        sequenceAttempt.logicalStepId = configuredStep.id;
        sequenceAttempt.logicalStepIndex = 0;
        sequenceAttempt.repetition = 2;
        sequenceAttempt.purpose = testing::TestStepPurpose::SequenceStep;
        sequenceResult.attempts = {sequenceAttempt};
        retainAll(sequenceResult);
        cases.append(sequence);
        results.append(sequenceResult);

        auto consistency = configuredCase(QStringLiteral("CONS"),
                                           testing::TestCaseType::Consistency);
        auto consistencyResult = terminalResult(consistency, testing::TestStatus::Pass);
        consistencyResult.actual = testing::ActualResult::samples({{1, 2}, {1, 3}});
        consistencyResult.assertion = passedAssertion();
        consistencyResult.assertion->actualSummary.clear();
        consistencyResult.attempts = {attempt(3, 103)};
        retainAll(consistencyResult);
        cases.append(consistency);
        results.append(consistencyResult);

        auto stability = configuredCase(QStringLiteral("STAB"),
                                         testing::TestCaseType::Stability);
        stability.expected.type = testing::AssertionType::StabilitySummary;
        auto stabilityResult = terminalResult(stability, testing::TestStatus::Pass);
        stabilityResult.expected = stability.expected;
        const testing::StabilityStatistics statistics{
            10, 8, 1, 1, 9, 1, 3, 4, 8};
        stabilityResult.stability = statistics;
        stabilityResult.actual = testing::ActualResult::stabilitySummary(statistics);
        stabilityResult.assertion = passedAssertion();
        stabilityResult.attempts = {attempt(4, 104)};
        stabilityResult.evidenceRetention.policy =
            testing::TestEvidenceRetentionPolicy::BoundedRepresentative;
        stabilityResult.evidenceRetention.totalAttempts = 10;
        stabilityResult.evidenceRetention.retainedAttempts = 1;
        stabilityResult.evidenceRetention.droppedAttempts = 9;
        stabilityResult.evidenceRetention.totalFailures = 1;
        stabilityResult.evidenceRetention.retainedFailures = 1;
        stabilityResult.evidenceRetention.configuredLimit = 8;
        stabilityResult.evidenceRetention.description = QStringLiteral("有界代表证据");
        cases.append(stability);
        results.append(stabilityResult);

        auto guided = configuredCase(QStringLiteral("GUIDED"),
                                      testing::TestCaseType::GuidedRecovery);
        testing::GuidedStep promptStep;
        promptStep.id = QStringLiteral("reconnect");
        promptStep.type = testing::GuidedStepType::OperatorPrompt;
        testing::GuidedOperatorStep prompt;
        prompt.purpose = testing::GuidedPromptPurpose::ReconnectRs485;
        prompt.title = QStringLiteral("恢复 A/B");
        prompt.instruction = QStringLiteral("按原极性恢复接线");
        prompt.safetyNotice = QStringLiteral("保持安全低压");
        prompt.allowedActions = {testing::GuidedOperatorAction::Confirm,
                                 testing::GuidedOperatorAction::Cancel};
        prompt.cancelRecoveryInstruction = QStringLiteral("取消后仍需恢复接线");
        promptStep.operatorStep = prompt;
        testing::GuidedStep observationStep;
        observationStep.id = QStringLiteral("observe-recovery");
        observationStep.type = testing::GuidedStepType::ObserveRecovery;
        testing::GuidedObservationStep observationConfig;
        observationConfig.target =
            testing::GuidedObservationTarget::ConsecutiveValidResponses;
        observationConfig.interval = std::chrono::milliseconds(250);
        observationConfig.deadline = std::chrono::milliseconds(4900);
        testing::ExpectedAssertion businessExpected;
        businessExpected.type = testing::AssertionType::Equals;
        businessExpected.value = 2;
        observationConfig.businessAssertion = businessExpected;
        observationStep.observationStep = observationConfig;
        guided.guidedRecovery.steps = {promptStep, observationStep};
        auto guidedResult = terminalResult(guided, testing::TestStatus::Pass);
        auto guidedAttempt = attempt(5, 105);
        guidedAttempt.logicalStepId = QStringLiteral("observe-recovery");
        guidedResult.attempts = {guidedAttempt};
        retainAll(guidedResult);
        testing::GuidedRecoveryCaseResult recovery;
        recovery.finalState = testing::GuidedRunState::Finished;
        recovery.terminalReason = testing::GuidedTerminalReason::Pass;
        recovery.physicalLinkRestored = true;
        recovery.recoveryTiming = testing::RecoveryTiming{
            std::chrono::milliseconds(51), std::chrono::milliseconds(716)};
        testing::GuidedOperatorActionRecord action;
        action.runId = testing::TestRunId{42};
        action.caseId = guided.id;
        action.stepId = QStringLiteral("reconnect");
        action.oneTimeToken = QStringLiteral("token-1");
        action.promptShownUtc = startedUtc.addSecs(3);
        action.action = testing::GuidedOperatorAction::Confirm;
        action.actionUtc = startedUtc.addSecs(4);
        action.waitDuration = std::chrono::milliseconds(1000);
        action.note = QStringLiteral("人工确认不是自动 PASS 证据");
        recovery.operatorActions = {action};
        testing::GuidedObservationResult observation;
        observation.runId = testing::TestRunId{42};
        observation.caseId = guided.id;
        observation.stepId = QStringLiteral("observe-recovery");
        observation.target = testing::GuidedObservationTarget::ConsecutiveValidResponses;
        observation.outcome = testing::GuidedObservationOutcome::Matched;
        observation.startedUtc = startedUtc.addSecs(4);
        observation.finishedUtc = startedUtc.addSecs(5);
        observation.duration = std::chrono::milliseconds(716);
        observation.requiredConsecutiveMatches = 3;
        observation.achievedConsecutiveMatches = 3;
        observation.firstMatchingRequestId = communication::RequestId{105};
        observation.stableMatchingRequestId = communication::RequestId{107};
        observation.probes = {guidedAttempt};
        recovery.observations = {observation};
        guidedResult.guidedRecovery = recovery;
        cases.append(guided);
        results.append(guidedResult);

        auto input = inputFor(cases, results, testing::TestStatus::Pass,
                              testing::testSuiteSchemaVersionV3);
        const auto built = report::ReportModelBuilder::build(input);
        QVERIFY2(built.succeeded(), built.errors.isEmpty()
            ? "unknown" : qPrintable(built.errors.front().diagnostic));
        QCOMPARE(built.document->cases.size(), 5);
        const auto &mappedBase = built.document->cases.at(0);
        QCOMPARE(mappedBase.attempts.front().tx.uppercaseHex, QStringLiteral("01 AB"));
        QCOMPARE(mappedBase.attempts.front().rx.presence, report::ReportFramePresence::Empty);
        QVERIFY(!mappedBase.attempts.front().rttNanoseconds);
        QCOMPARE(mappedBase.request->functionCode, quint8(0x03));
        QCOMPARE(mappedBase.attempts.front().descriptorKind,
                 QStringLiteral("read_holding_registers"));
        QCOMPARE(mappedBase.attempts.front().serverAddress, quint8(1));
        QCOMPARE(report::ReportFrame{}.presence, report::ReportFramePresence::Missing);
        QCOMPARE(built.document->cases.at(1).steps.front().stepId,
                 QStringLiteral("read-version"));
        QCOMPARE(built.document->cases.at(1).steps.front().attemptSequences,
                 QVector<quint64>{2});
        QVERIFY(built.document->cases.at(2).assertion.actualSummary.contains(
            QStringLiteral("2 组")));
        QCOMPARE(built.document->cases.at(2).assertion.actualRawSamples.size(), 2);
        QCOMPARE(built.document->cases.at(3).evidenceRetention.totalAttempts, quint64(10));
        QCOMPARE(built.document->cases.at(3).evidenceRetention.droppedAttempts, quint64(9));
        QCOMPARE(built.document->cases.at(3).stability->timeouts, quint64(1));
        const auto &mappedGuided = *built.document->cases.at(4).guidedRecovery;
        QCOMPARE(mappedGuided.operatorActions.front().action,
                 std::optional<QString>(QStringLiteral("confirm")));
        QCOMPARE(mappedGuided.operatorActions.front().title, QStringLiteral("恢复 A/B"));
        QCOMPARE(mappedGuided.operatorActions.front().promptPurpose,
                 QStringLiteral("reconnect_rs485"));
        QCOMPARE(mappedGuided.observations.front().attemptSequences,
                 QVector<quint64>{5});
        QCOMPARE(mappedGuided.observations.front().deadline.count(), qint64(4900));
        QVERIFY(mappedGuided.observations.front().businessExpectedSummary->contains(
            QStringLiteral("2")));
        QCOMPARE(mappedGuided.toStableRecovery->count(), qint64(716));
        QVERIFY(mappedGuided.physicalLinkRestored);
    }

    void mapsAssertionCommunicationAndCleanupErrors()
    {
        const auto configured = configuredCase(QStringLiteral("FAIL"),
                                               testing::TestCaseType::ReadRegisters);
        auto result = terminalResult(configured, testing::TestStatus::Fail);
        testing::AssertionResult assertion;
        assertion.status = testing::TestStatus::Fail;
        assertion.type = testing::AssertionType::Equals;
        assertion.expectedSummary = QStringLiteral("1");
        assertion.actualSummary = QStringLiteral("2");
        testing::AssertionDifference difference;
        difference.code = testing::AssertionDifferenceCode::ValueMismatch;
        difference.expected = 1;
        difference.actual = 2;
        difference.reason = QStringLiteral("数值不一致");
        assertion.differences = {difference};
        result.assertion = assertion;
        testing::TestError cleanup;
        cleanup.code = testing::TestErrorCode::CleanupFailed;
        cleanup.diagnostic = QStringLiteral("恢复失败");
        result.cleanupError = cleanup;
        auto failedAttempt = attempt(1, 201);
        failedAttempt.requestResult.state = communication::RequestState::Failed;
        communication::CommunicationError error;
        error.category = communication::ErrorCategory::Timeout;
        error.code = communication::ErrorCode::ResponseTimeout;
        error.diagnostic = QStringLiteral("等待响应超时");
        failedAttempt.requestResult.error = error;
        result.attempts = {failedAttempt};
        result.evidenceRetention.totalFailures = 1;
        result.evidenceRetention.retainedFailures = 1;
        retainAll(result);

        const auto built = report::ReportModelBuilder::build(
            inputFor({configured}, {result}, testing::TestStatus::Fail));
        QVERIFY(built.succeeded());
        const auto &mapped = built.document->cases.front();
        QCOMPARE(mapped.assertion.differences.front().code, QStringLiteral("ValueMismatch"));
        QCOMPARE(mapped.cleanupError->code, QStringLiteral("CleanupFailed"));
        QCOMPARE(mapped.attempts.front().error->communicationCode,
                 std::optional<QString>(QStringLiteral("ResponseTimeout")));
    }

    void rejectsInvalidMetadataRetentionAndArtifact()
    {
        const auto configured = configuredCase(QStringLiteral("TC-1"),
                                               testing::TestCaseType::ReadRegisters);
        auto result = terminalResult(configured, testing::TestStatus::Pass);
        auto input = inputFor({configured}, {result}, testing::TestStatus::Pass);
        input.operatorMetadata.tester = QString(129, QLatin1Char('x'));
        auto built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QCOMPARE(built.errors.front().code, report::ReportErrorCode::InvalidMetadata);

        input = inputFor({configured}, {result}, testing::TestStatus::Pass);
        input.sessionLogArtifact = report::SessionLogArtifactInput{
            true, QStringLiteral("session-42"), QStringLiteral("log.jsonl"), 1,
            QStringLiteral("not-a-hash")};
        built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QCOMPARE(built.errors.front().code, report::ReportErrorCode::InvalidArtifact);

        input = inputFor({configured}, {result}, testing::TestStatus::Pass);
        input.sourceRevision = QString(QChar(u'\0'));
        built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QCOMPARE(built.errors.front().code, report::ReportErrorCode::InvalidMetadata);

        result.evidenceRetention.totalAttempts = 1;
        input = inputFor({configured}, {result}, testing::TestStatus::Pass);
        built = report::ReportModelBuilder::build(input);
        QVERIFY(!built.succeeded());
        QCOMPARE(built.errors.front().code,
                 report::ReportErrorCode::EvidenceInvariantViolation);
    }

    void acceptsV1V2V3TerminalResults()
    {
        const auto configured = configuredCase(QStringLiteral("TC-1"),
                                               testing::TestCaseType::ReadRegisters);
        const auto result = terminalResult(configured, testing::TestStatus::Pass);
        for (const int schema : {testing::testSuiteSchemaVersion,
                                 testing::testSuiteSchemaVersionV2,
                                 testing::testSuiteSchemaVersionV3}) {
            const auto built = report::ReportModelBuilder::build(
                inputFor({configured}, {result}, testing::TestStatus::Pass, schema));
            QVERIFY2(built.succeeded(), qPrintable(QStringLiteral("schema %1").arg(schema)));
            QCOMPARE(built.document->schemaVersion, schema);
        }
    }
};

QTEST_APPLESS_MAIN(ReportModelTest)
#include "tst_reportmodel.moc"
