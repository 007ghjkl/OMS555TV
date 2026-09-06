#include "report/HtmlReportGenerator.h"
#include "report/ReportModelBuilder.h"

#include <QCryptographicHash>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using namespace oms555tv;

namespace {

const QDateTime startedUtc = QDateTime::fromString(
    QStringLiteral("2026-09-06T02:03:04.005Z"), Qt::ISODateWithMs);

testing::TestCase configuredCase(QString id, testing::TestCaseType type)
{
    testing::TestCase value;
    value.id = std::move(id);
    value.name = value.id + QStringLiteral(" 中文 <用例>");
    value.category = QStringLiteral("phase8");
    value.description = QStringLiteral("确定性 fixture & 离线报告");
    value.type = type;
    value.declaredType = type;
    value.tags = {QStringLiteral("fixture"), QStringLiteral("报告")};
    value.request.function = testing::ModbusFunction::ReadHoldingRegisters;
    value.request.address = device::PduAddress(0);
    value.request.count = 1;
    value.expected.type = testing::AssertionType::Equals;
    value.expected.value = 1;
    return value;
}

testing::TestCaseResult terminalResult(const testing::TestCase &configured,
                                       testing::TestStatus status)
{
    testing::TestCaseResult value;
    value.caseId = configured.id;
    value.type = configured.type;
    value.declaredType = configured.declaredType;
    value.status = status;
    value.expected = configured.expected;
    if (status != testing::TestStatus::Skipped) {
        value.startedUtc = startedUtc.addSecs(1);
        value.finishedUtc = startedUtc.addSecs(2);
        value.duration = std::chrono::milliseconds(1000);
    }
    return value;
}

testing::AssertionResult passedAssertion(QString actual = QStringLiteral("实际 1"))
{
    testing::AssertionResult value;
    value.status = testing::TestStatus::Pass;
    value.type = testing::AssertionType::Equals;
    value.expectedSummary = QStringLiteral("期望 1");
    value.actualSummary = std::move(actual);
    return value;
}

testing::TestRequestAttemptResult attempt(quint64 sequence, quint64 requestId)
{
    testing::TestRequestAttemptResult value;
    value.sequence = sequence;
    value.purpose = testing::TestStepPurpose::Read;
    value.requestId = communication::RequestId{requestId};
    value.startedUtc = startedUtc.addMSecs(static_cast<qint64>(sequence) * 10);
    value.finishedUtc = value.startedUtc.addMSecs(6);
    value.duration = std::chrono::milliseconds(6);
    value.requestResult.requestId = value.requestId;
    value.requestResult.state = communication::RequestState::Succeeded;
    value.requestResult.descriptor = communication::ReadRequestDescriptor{
        device::PduAddress(0), 1};
    auto &evidence = value.requestResult.evidence;
    evidence.requestId = value.requestId;
    evidence.serverAddress = 1;
    evidence.functionCode = 3;
    evidence.txAdu = QByteArray::fromHex("010300000001840A");
    evidence.rxAdu = QByteArray::fromHex("0103020001B9F0");
    evidence.txCrcStatus = communication::CrcStatus::Valid;
    evidence.rxCrcStatus = communication::CrcStatus::Valid;
    evidence.enqueuedUtc = value.startedUtc;
    evidence.completedUtc = value.finishedUtc;
    evidence.rtt = std::chrono::microseconds(4500);
    return value;
}

void retainAll(testing::TestCaseResult &value)
{
    value.evidenceRetention.policy = testing::TestEvidenceRetentionPolicy::Complete;
    value.evidenceRetention.totalAttempts =
        static_cast<quint64>(value.attempts.size());
    value.evidenceRetention.retainedAttempts =
        static_cast<quint64>(value.attempts.size());
    value.evidenceRetention.description = QStringLiteral("完整保留全部 attempt");
}

report::ReportInput inputFor(QString suiteId, QVector<testing::TestCase> cases,
                             QVector<testing::TestCaseResult> results,
                             testing::TestStatus status, int schemaVersion)
{
    auto suite = std::make_shared<testing::TestSuite>();
    suite->schemaVersion = schemaVersion;
    suite->id = std::move(suiteId);
    suite->name = suite->id + QStringLiteral(" 验收套件");
    suite->description = QStringLiteral("TASK-026 四类确定性结果 fixture");
    suite->tags = {QStringLiteral("phase8"), QStringLiteral("fixture")};
    suite->metadata.insert(QStringLiteral("device_model"),
                           QStringLiteral("NUCLEO-F411RE"));
    suite->cases = std::move(cases);

    report::ReportInput input;
    input.suiteResult.runId = testing::TestRunId{42};
    input.suiteResult.suite = std::move(suite);
    input.suiteResult.status = status;
    input.suiteResult.startedUtc = startedUtc;
    input.suiteResult.finishedUtc = startedUtc.addSecs(10);
    input.suiteResult.duration = std::chrono::milliseconds(10000);
    input.suiteResult.cases = std::move(results);
    input.suiteResult.sessionId = QStringLiteral("fixture-session");
    input.operatorMetadata.tester = QStringLiteral("测试员 <甲> & 乙");
    input.applicationVersion = QStringLiteral("0.1.0-test");
    input.sourceRevision = QStringLiteral("task026-fixture");
    input.displayTimeZone = QTimeZone(QByteArrayLiteral("Asia/Shanghai"));
    return input;
}

report::ReportInput allPassInput()
{
    auto configured = configuredCase(QStringLiteral("PASS-1"),
                                     testing::TestCaseType::ReadRegisters);
    auto result = terminalResult(configured, testing::TestStatus::Pass);
    result.actual = testing::ActualResult::registers({1});
    result.assertion = passedAssertion();
    result.attempts = {attempt(1, 101)};
    retainAll(result);
    return inputFor(QStringLiteral("fixture-all-pass"), {configured}, {result},
                    testing::TestStatus::Pass, testing::testSuiteSchemaVersion);
}

report::ReportInput mixedInput()
{
    QVector<testing::TestCase> cases;
    QVector<testing::TestCaseResult> results;
    for (const auto status : {testing::TestStatus::Pass, testing::TestStatus::Fail,
                              testing::TestStatus::Error,
                              testing::TestStatus::Skipped}) {
        auto configured = configuredCase(
            QStringLiteral("MIX-%1").arg(testing::testStatusName(status)),
            testing::TestCaseType::ReadRegisters);
        cases.append(configured);
        results.append(terminalResult(configured, status));
    }
    return inputFor(QStringLiteral("fixture-mixed"), std::move(cases),
                    std::move(results), testing::TestStatus::Error,
                    testing::testSuiteSchemaVersion);
}

report::ReportInput stabilityInput()
{
    auto configured = configuredCase(QStringLiteral("STABILITY"),
                                     testing::TestCaseType::Stability);
    configured.expected.type = testing::AssertionType::StabilitySummary;
    auto result = terminalResult(configured, testing::TestStatus::Pass);
    result.expected = configured.expected;
    const testing::StabilityStatistics statistics{10, 8, 1, 1, 9, 1, 3, 4, 8};
    result.stability = statistics;
    result.actual = testing::ActualResult::stabilitySummary(statistics);
    result.assertion = passedAssertion(QStringLiteral("10 次采样，8 次成功"));
    result.attempts = {attempt(1, 201)};
    result.evidenceRetention.policy =
        testing::TestEvidenceRetentionPolicy::BoundedRepresentative;
    result.evidenceRetention.totalAttempts = 10;
    result.evidenceRetention.retainedAttempts = 1;
    result.evidenceRetention.droppedAttempts = 9;
    result.evidenceRetention.totalFailures = 1;
    result.evidenceRetention.retainedFailures = 1;
    result.evidenceRetention.configuredLimit = 8;
    result.evidenceRetention.description = QStringLiteral("有界代表证据");
    return inputFor(QStringLiteral("fixture-stability"), {configured}, {result},
                    testing::TestStatus::Pass, testing::testSuiteSchemaVersionV2);
}

report::ReportInput guidedInput()
{
    auto configured = configuredCase(QStringLiteral("GUIDED"),
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
    observationStep.observationStep = observationConfig;
    configured.guidedRecovery.steps = {promptStep, observationStep};

    auto result = terminalResult(configured, testing::TestStatus::Pass);
    auto probe = attempt(1, 301);
    probe.logicalStepId = observationStep.id;
    result.attempts = {probe};
    retainAll(result);
    testing::GuidedRecoveryCaseResult recovery;
    recovery.finalState = testing::GuidedRunState::Finished;
    recovery.terminalReason = testing::GuidedTerminalReason::Pass;
    recovery.physicalLinkRestored = true;
    recovery.recoveryTiming = testing::RecoveryTiming{
        std::chrono::milliseconds(51), std::chrono::milliseconds(716)};
    testing::GuidedOperatorActionRecord action;
    action.runId = testing::TestRunId{42};
    action.caseId = configured.id;
    action.stepId = promptStep.id;
    action.oneTimeToken = QStringLiteral("fixture-token");
    action.promptShownUtc = startedUtc.addSecs(3);
    action.action = testing::GuidedOperatorAction::Confirm;
    action.actionUtc = startedUtc.addSecs(4);
    action.waitDuration = std::chrono::milliseconds(1000);
    action.note = QStringLiteral("人工确认不作为 PASS 证据");
    recovery.operatorActions = {action};
    testing::GuidedObservationResult observation;
    observation.runId = testing::TestRunId{42};
    observation.caseId = configured.id;
    observation.stepId = observationStep.id;
    observation.target = observationConfig.target;
    observation.outcome = testing::GuidedObservationOutcome::Matched;
    observation.startedUtc = startedUtc.addSecs(4);
    observation.finishedUtc = startedUtc.addSecs(5);
    observation.duration = std::chrono::milliseconds(716);
    observation.requiredConsecutiveMatches = 3;
    observation.achievedConsecutiveMatches = 3;
    observation.firstMatchingRequestId = communication::RequestId{301};
    observation.stableMatchingRequestId = communication::RequestId{303};
    observation.probes = {probe};
    recovery.observations = {observation};
    result.guidedRecovery = recovery;
    return inputFor(QStringLiteral("fixture-guided"), {configured}, {result},
                    testing::TestStatus::Pass, testing::testSuiteSchemaVersionV3);
}

} // namespace

class ReportFixturesTest final : public QObject
{
    Q_OBJECT

private slots:
    void generatesFourResultFixturesWithConsistentEvidence()
    {
        QTemporaryDir output;
        QVERIFY(output.isValid());
        const QVector<report::ReportInput> inputs{
            allPassInput(), mixedInput(), stabilityInput(), guidedInput()};
        const QVector<QByteArray> markers{
            QByteArrayLiteral("fixture-all-pass"), QByteArrayLiteral("SKIPPED"),
            QByteArrayLiteral("有界代表证据"), QByteArrayLiteral("稳定恢复")};
        QSet<QByteArray> digests;
        report::HtmlReportGenerator generator([] {
            return QDateTime::fromString(
                QStringLiteral("2026-09-06T08:09:10.123Z"), Qt::ISODateWithMs);
        });

        for (qsizetype index = 0; index < inputs.size(); ++index) {
            const auto built = report::ReportModelBuilder::build(inputs[index]);
            QVERIFY2(built.succeeded(), built.errors.isEmpty()
                ? "unknown" : qPrintable(built.errors.front().diagnostic));
            const QString path = output.filePath(QStringLiteral("fixture-%1.html").arg(index));
            const auto written = generator.write(*built.document, path);
            QVERIFY2(written.succeeded(), written.errors.isEmpty()
                ? "unknown" : qPrintable(written.errors.front().diagnostic));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray html = file.readAll();
            QVERIFY(html.startsWith("<!doctype html>"));
            QVERIFY(html.contains(markers[index]));
            QVERIFY(html.contains("RequestId") || index == 1);
            QVERIFY(html.contains("测试员 &lt;甲&gt; &amp; 乙"));
            QVERIFY(html.contains("default-src 'none'"));
            QVERIFY(!html.contains("<script"));
            QVERIFY(!html.contains(" href="));
            QVERIFY(!html.contains(" src="));
            digests.insert(QCryptographicHash::hash(html, QCryptographicHash::Sha256));
        }
        QCOMPARE(digests.size(), 4);
    }

    void validatesSanitizedRealRs485Sample()
    {
        QFile file(QStringLiteral(OMS555TV_TASK026_SAMPLE_REPORT));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        const QByteArray html = file.readAll();

        QVERIFY(html.startsWith("<!doctype html>"));
        QCOMPARE(html.count("<article class=\"case\">"), 8);
        QVERIFY(html.contains("100.0000%（8/8）"));
        QVERIFY(html.contains("Firmware 版本</th><td class=\"text\">0.2"));
        QVERIFY(html.contains("COM6，115200 baud"));
        QVERIFY(html.contains("Slave 1，timeout 500 ms"));
        QVERIFY(html.contains("bfcadd81-2564-4206-90d3-ea2c4be10c7b"));
        QVERIFY(html.contains("18306 bytes"));
        QVERIFY(html.contains(
            "151AB0778C805D1A93BBC222DCD2F03043EEB69F0431B85E437776E1E1558B55"));
        QVERIFY(html.contains("read_before_write"));
        QVERIFY(html.contains("verify_readback"));
        QVERIFY(html.contains("restore_original"));
        for (int requestId = 1; requestId <= 11; ++requestId) {
            QVERIFY(html.contains(QByteArray("RequestId ") + QByteArray::number(requestId)
                                  + " ·"));
        }
        QCOMPARE(html.count("<th scope=\"row\">TX</th>"), 11);
        QCOMPARE(html.count("<th scope=\"row\">RX</th>"), 11);
        QCOMPARE(html.count("<th scope=\"row\">RTT</th>"), 11);
        QVERIFY(html.contains("示例报告已移除本机绝对路径"));
        QVERIFY(!html.contains("D:/"));
        QVERIFY(!html.contains("C:/"));
        QVERIFY(html.contains("default-src 'none'"));
        QVERIFY(!html.contains("<script"));
        QVERIFY(!html.contains(" href="));
        QVERIFY(!html.contains(" src="));
        QCOMPARE(
            QCryptographicHash::hash(html, QCryptographicHash::Sha256).toHex().toUpper(),
            QByteArrayLiteral(
                "CA02E7C326901648188A17EB32FC110EB779BAE086EAA66301F7D9844ADF394C"));
    }
};

QTEST_APPLESS_MAIN(ReportFixturesTest)

#include "tst_reportfixtures.moc"
