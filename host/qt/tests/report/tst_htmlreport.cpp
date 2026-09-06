#include "report/HtmlReportGenerator.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace oms555tv;

namespace {

constexpr auto fixedGeneratedAt = "2026-09-06T08:09:10.123Z";

report::ReportTimestamp timestamp(const int seconds)
{
    const QDateTime utc = QDateTime::fromString(
        QStringLiteral("2026-09-06T01:02:03.004Z"), Qt::ISODateWithMs).addSecs(seconds);
    return report::ReportTimestamp{
        utc,
        utc.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'")),
        utc.addSecs(8 * 3600).toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz+08:00")),
        QByteArrayLiteral("Asia/Shanghai")};
}

report::ReportMetadataValue metadata(const QString &value,
                                     const report::MetadataSource source)
{
    return report::ReportMetadataValue{value, source, true, {}};
}

report::ReportAssertion assertion(const testing::TestStatus status,
                                  const QString &expected = QStringLiteral("期望 1"),
                                  const QString &actual = QStringLiteral("实际 2"))
{
    report::ReportAssertion value;
    value.type = QStringLiteral("elements");
    value.expectedSummary = expected;
    value.actualSummary = actual;
    value.status = status;
    value.expectedValues = {1, 2};
    value.actualValues = {2, 3};
    value.actualRawValues = {2, 3};
    value.actualRawSamples = {{1, 2}, {1, 3}};
    value.actualScaledValues = {{21, 10}};
    value.expectedExceptionCode = quint8(2);
    value.actualExceptionCode = quint8(3);
    value.unit = QStringLiteral("℃");
    report::ReportExpectedElement element;
    element.index = 0;
    element.type = QStringLiteral("range");
    element.representation = QStringLiteral("signed_scaled");
    element.minimum = -400;
    element.maximum = 1250;
    element.mask = 0x00FF;
    element.decimalPlaces = 1;
    element.unit = QStringLiteral("℃");
    value.expectedElements = {element};
    value.expectedUInt32 = report::ReportUInt32Expectation{
        QStringLiteral("low_word_first"), QStringLiteral("range"), 1, 9, 0,
        QStringLiteral("count")};
    value.expectedStability = report::ReportStabilityExpectation{
        10, 9, 1, 1, 100000, true, 0, 50, 100};
    report::ReportAssertionDifference difference;
    difference.code = QStringLiteral("ValueMismatch");
    difference.index = 0;
    difference.expectedMinimum = 1;
    difference.expectedMaximum = 1;
    difference.actual = 2;
    difference.actualRaw = 2;
    difference.scaledDenominator = 10;
    difference.unit = QStringLiteral("℃");
    difference.reason = QStringLiteral("数值不一致");
    value.differences = {difference};
    return value;
}

report::ReportTransactionEvidence attempt(const quint64 sequence, const quint64 requestId)
{
    report::ReportTransactionEvidence value;
    value.attemptSequence = sequence;
    value.purpose = QStringLiteral("primary");
    value.stepAttempt = 1;
    value.retry = sequence > 1;
    value.retryReason = sequence > 1 ? QStringLiteral("超时重试") : QString();
    value.logicalStepId = QStringLiteral("step-1");
    value.logicalStepIndex = 0;
    value.repetition = 2;
    value.requestId = requestId;
    value.serverAddress = 1;
    value.functionCode = 3;
    value.descriptorKind = QStringLiteral("read_holding_registers");
    value.address = 39;
    value.count = quint16(2);
    value.requestState = QStringLiteral("Succeeded");
    value.started = timestamp(1);
    value.finished = timestamp(2);
    value.duration = std::chrono::milliseconds(12);
    value.tx = {report::ReportFramePresence::Value, QByteArray::fromHex("010300270002"),
                QStringLiteral("01 03 00 27 00 02")};
    value.rx = {report::ReportFramePresence::Empty, {}, {}};
    value.txCrcStatus = QStringLiteral("valid");
    value.rxCrcStatus = QStringLiteral("not_checked");
    return value;
}

report::ReportCase baseCase(const QString &id, const testing::TestStatus status)
{
    report::ReportCase value;
    value.id = id;
    value.name = QStringLiteral("中文用例 <name>");
    value.category = QStringLiteral("功能");
    value.description = QStringLiteral("<script>alert(\"x\") & 'y'</script>\r\n下一行\tX")
        + QChar(1) + QString(2048, QChar(u'长'));
    value.environment = QStringLiteral("real_rs485");
    value.tags = {QStringLiteral("phase8"), QStringLiteral("报告")};
    value.declaredType = QStringLiteral("read_registers");
    value.normalizedType = QStringLiteral("read_registers");
    value.request = report::ReportRequestDefinition{3, 39, 2, 0, false, false};
    value.status = status;
    value.assertion = assertion(status);
    value.started = timestamp(0);
    value.finished = timestamp(3);
    value.duration = std::chrono::milliseconds(3000);
    value.attempts = {attempt(1, 101)};
    value.evidenceRetention = {QStringLiteral("retain_all"), 1, 1, 0, 0, 0, 100,
                               QStringLiteral("完整保留")};
    value.sessionId = QStringLiteral("session-42");
    return value;
}

report::ReportDocumentModel completeModel()
{
    report::ReportDocumentModel model;
    model.runId = 42;
    model.schemaVersion = 3;
    model.suiteId = QStringLiteral("Phase 8/主套件_中文");
    model.suiteName = QStringLiteral("Phase 8 报告安全套件");
    model.suiteDescription = QStringLiteral("覆盖全部报告结构");
    model.suiteTags = {QStringLiteral("phase8"), QStringLiteral("offline")};
    model.status = testing::TestStatus::Error;
    model.started = timestamp(0);
    model.finished = timestamp(10);
    model.duration = std::chrono::milliseconds(10000);

    model.metadata.projectName = metadata(QStringLiteral("OMS555TV"),
        report::MetadataSource::SystemObserved);
    model.metadata.testObject = metadata(QStringLiteral("测试对象 & DUT"),
        report::MetadataSource::SuiteConfigured);
    model.metadata.deviceModel = metadata(QStringLiteral("NUCLEO-F411RE"),
        report::MetadataSource::SuiteConfigured);
    model.metadata.testBench = metadata(QStringLiteral("安全低压台架"),
        report::MetadataSource::OperatorEntered);
    model.metadata.environmentDescription = metadata(QStringLiteral("20 cm RS485"),
        report::MetadataSource::SuiteConfigured);
    model.metadata.firmwareVersion = metadata(QStringLiteral("0.2"),
        report::MetadataSource::SystemObserved);
    model.metadata.hostVersion = metadata(QStringLiteral("0.1.0"),
        report::MetadataSource::SystemObserved);
    model.metadata.sourceRevision = metadata(QStringLiteral("010fd56"),
        report::MetadataSource::SystemObserved);
    model.metadata.tester = metadata(QStringLiteral("测试员 A"),
        report::MetadataSource::OperatorEntered);
    model.metadata.sessionId = metadata(QStringLiteral("session-42"),
        report::MetadataSource::SystemObserved);
    model.metadata.connection = {true, QStringLiteral("未采集"), QStringLiteral("COM6"),
        115200, 8, QStringLiteral("none"), QStringLiteral("1"), QStringLiteral("none"),
        1, 500, 64, report::MetadataSource::SystemObserved};
    model.metadata.suiteEntries.insert(QStringLiteral("hardware"),
        metadata(QStringLiteral("NUCLEO <F411>"), report::MetadataSource::SuiteConfigured));

    auto failed = baseCase(QStringLiteral("TC-FAIL"), testing::TestStatus::Fail);
    failed.error = report::ReportExecutionError{QStringLiteral("AssertionFailed"),
        QStringLiteral("断言失败 <bad>"), {}, {}, {}};
    failed.cleanupError = report::ReportExecutionError{QStringLiteral("CleanupFailed"),
        QStringLiteral("恢复失败"), {}, {}, {}};
    failed.attempts.front().requestState = QStringLiteral("Failed");
    failed.attempts.front().error = report::ReportExecutionError{
        QStringLiteral("CommunicationFailed"), QStringLiteral("等待响应超时"),
        QStringLiteral("timeout"), QStringLiteral("ResponseTimeout"), {}};
    failed.evidenceRetention.totalFailures = 1;
    failed.evidenceRetention.retainedFailures = 1;

    auto sequence = baseCase(QStringLiteral("TC-SEQ"), testing::TestStatus::Pass);
    sequence.declaredType = QStringLiteral("sequence");
    sequence.normalizedType = QStringLiteral("sequence");
    report::ReportCompositeStep step;
    step.stepId = QStringLiteral("read-version");
    step.stepIndex = 0;
    step.repetition = 2;
    step.declaredType = QStringLiteral("read_registers");
    step.normalizedType = QStringLiteral("read_registers");
    step.delayBefore = std::chrono::milliseconds(5);
    step.request = sequence.request;
    step.status = testing::TestStatus::Pass;
    step.assertion = assertion(testing::TestStatus::Pass);
    step.started = timestamp(1);
    step.finished = timestamp(2);
    step.duration = std::chrono::milliseconds(10);
    step.attemptSequences = {1};
    sequence.steps = {step};

    auto stability = baseCase(QStringLiteral("TC-STABILITY"), testing::TestStatus::Error);
    stability.declaredType = QStringLiteral("stability");
    stability.normalizedType = QStringLiteral("stability");
    stability.stability = report::ReportStabilityStatistics{
        100, 97, 2, 1, 98, 2, 2, 4, 9};
    stability.attempts.append(attempt(2, 202));
    stability.attempts.back().rx = {report::ReportFramePresence::Missing, {}, {}};
    stability.attempts.back().rttNanoseconds = 1234567;
    stability.attempts.back().rttMilliseconds = QStringLiteral("1.235");
    stability.evidenceRetention = {QStringLiteral("bounded_representative"), 100, 2, 98,
        3, 1, 8, QStringLiteral("首条、失败窗口与末条")};

    auto guided = baseCase(QStringLiteral("TC-GUIDED"), testing::TestStatus::Skipped);
    guided.declaredType = QStringLiteral("guided_recovery");
    guided.normalizedType = QStringLiteral("guided_recovery");
    guided.skipReason = QStringLiteral("操作员取消");
    report::ReportGuidedRecovery recovery;
    recovery.finalState = QStringLiteral("finished");
    recovery.terminalReason = QStringLiteral("cancelled");
    recovery.toFirstSuccessfulResponse = std::chrono::milliseconds(51);
    recovery.toStableRecovery = std::chrono::milliseconds(716);
    recovery.physicalLinkRestored = false;
    recovery.recoveryInstructionRequired = true;
    recovery.recoveryInstruction = QStringLiteral("按原极性恢复 A/B 接线");
    report::ReportGuidedOperatorAction action;
    action.runId = 42;
    action.caseId = guided.id;
    action.stepId = QStringLiteral("reconnect");
    action.oneTimeToken = QStringLiteral("token-1");
    action.promptPurpose = QStringLiteral("reconnect_rs485");
    action.title = QStringLiteral("恢复 A/B");
    action.instruction = QStringLiteral("恢复接线");
    action.safetyNotice = QStringLiteral("保持安全低压");
    action.allowedActions = {QStringLiteral("confirm"), QStringLiteral("cancel")};
    action.cancelRecoveryInstruction = QStringLiteral("取消后仍需恢复接线");
    action.action = QStringLiteral("cancel");
    action.promptShown = timestamp(4);
    action.actionTime = timestamp(5);
    action.waitDuration = std::chrono::milliseconds(1000);
    action.note = QStringLiteral("人工动作不作为 PASS 证据");
    recovery.operatorActions = {action};
    report::ReportGuidedObservation observation;
    observation.runId = 42;
    observation.caseId = guided.id;
    observation.stepId = QStringLiteral("observe-recovery");
    observation.target = QStringLiteral("consecutive_valid_responses");
    observation.outcome = QStringLiteral("matched");
    observation.probe = guided.request;
    observation.interval = std::chrono::milliseconds(250);
    observation.deadline = std::chrono::milliseconds(4900);
    observation.businessExpectedSummary = QStringLiteral("Firmware minor == 2");
    observation.started = timestamp(5);
    observation.finished = timestamp(6);
    observation.duration = std::chrono::milliseconds(716);
    observation.requiredConsecutiveMatches = 3;
    observation.achievedConsecutiveMatches = 3;
    observation.firstMatchingRequestId = 301;
    observation.stableMatchingRequestId = 303;
    observation.attemptSequences = {1, 2, 3};
    recovery.observations = {observation};
    guided.guidedRecovery = recovery;

    model.cases = {failed, sequence, stability, guided};
    model.summary = {4, 3, 1, 1, 1, 1, 1, 3, 333333,
                     std::chrono::milliseconds(10000), testing::TestStatus::Error};
    model.auxiliaryErrors = {report::ReportExecutionError{
        QStringLiteral("LoggingFailed"), QStringLiteral("日志归档失败"), {}, {}, {}}};
    model.sessionLogArtifact = {true, QStringLiteral("session-42"),
        QStringLiteral("D:\\logs\\session-42.jsonl"), 123456,
        QString(64, QChar(u'A')), QStringLiteral("未提供")};
    return model;
}

report::HtmlReportGenerator fixedGenerator()
{
    return report::HtmlReportGenerator([] {
        return QDateTime::fromString(QString::fromLatin1(fixedGeneratedAt), Qt::ISODateWithMs);
    });
}

} // namespace

class HtmlReportTest final : public QObject {
    Q_OBJECT

private slots:
    void generatesDeterministicSelfContainedStructure()
    {
        const auto generated = fixedGenerator().generate(completeModel());
        QVERIFY2(generated.succeeded(), generated.errors.isEmpty()
            ? "unknown" : qPrintable(generated.errors.front().diagnostic));
        const QByteArray &html = generated.document->utf8;
        QVERIFY(html.startsWith("<!doctype html>\n<html lang=\"zh-CN\">"));
        QVERIFY(html.contains("<meta charset=\"utf-8\">"));
        QVERIFY(html.contains("Content-Security-Policy"));
        QVERIFY(html.contains("default-src 'none'"));
        QVERIFY(html.contains("@media print"));
        QVERIFY(html.contains("details>*{display:block!important}"));
        QVERIFY(!html.contains("<script"));
        QVERIFY(!html.contains("<link"));
        QVERIFY(!html.contains("<img"));
        QVERIFY(!html.contains(" src="));
        QVERIFY(!html.contains(" href="));
        QCOMPARE(generated.document->generatedAtUtcIso,
                 QString::fromLatin1(fixedGeneratedAt));
        QCOMPARE(generated.document->suggestedFileName,
                 QStringLiteral("phase-8_20260906-010203-004Z_run-42.html"));

        const QByteArray digest = QCryptographicHash::hash(html, QCryptographicHash::Sha256).toHex();
        QCOMPARE(digest, QByteArrayLiteral(
            "1e6444ef66102b90d2aecb260a8ee4e31a97a29d423753b5b92c816a366396fc"));
        QCOMPARE(fixedGenerator().generate(completeModel()).document->utf8, html);
    }

    void rendersAllStatusesLayersAndEvidenceBoundary()
    {
        const QByteArray html = fixedGenerator().generate(completeModel()).document->utf8;
        for (const QByteArray status : {QByteArray("PASS"), QByteArray("FAIL"),
                                        QByteArray("ERROR"), QByteArray("SKIPPED")}) {
            QVERIFY2(html.contains(status), status.constData());
        }
        for (const QByteArray marker : {
                 QByteArray("Sequence 步骤"), QByteArray("一致性原始样本"),
                 QByteArray("稳定性聚合统计"), QByteArray("人工引导恢复"),
                 QByteArray("RequestId 202"), QByteArray("空帧（0 字节）"),
                 QByteArray("未保留"), QByteArray("未采集"),
                 QByteArray("ResponseTimeout"), QByteArray("CleanupFailed"),
                 QByteArray("有界代表证据"), QByteArray("session-42.jsonl"),
                 QByteArray("123456 bytes"), QByteArray(64, 'A')}) {
            QVERIFY2(html.contains(marker), marker.constData());
        }
        QVERIFY(html.contains("33.3333%（1/3）"));
        QVERIFY(html.contains("首个 / 稳定 RequestId"));
        QVERIFY(html.contains("301 / 303"));
    }

    void escapesExternalTextAndNormalizesControls()
    {
        auto model = completeModel();
        model.suiteName += QStringLiteral(" %4");
        model.metadata.connection.portName = QStringLiteral("COM%8");
        model.cases.back().guidedRecovery->operatorActions.front().oneTimeToken =
            QStringLiteral("token-%4");
        const QByteArray html = fixedGenerator().generate(model).document->utf8;
        QVERIFY(!html.contains("<script>alert"));
        QVERIFY(html.contains("&lt;script&gt;alert(&quot;x&quot;) &amp; &#39;y&#39;&lt;/script&gt;"));
        QVERIFY(html.contains("下一行    X"));
        QVERIFY(QString::fromUtf8(html).contains(QChar(0xFFFD)));
        QVERIFY(!html.contains(char(1)));
        QVERIFY(html.contains("中文用例 &lt;name&gt;"));
        QVERIFY(html.contains("NUCLEO &lt;F411&gt;"));
        QVERIFY(html.contains("Phase 8 报告安全套件 %4"));
        QVERIFY(html.contains("COM%8"));
        QVERIFY(html.contains("token-%4"));
        QVERIFY(html.size() > 2048);
    }

    void representsUnavailableMetadataAndZeroDenominator()
    {
        auto model = completeModel();
        model.metadata.firmwareVersion = {{}, report::MetadataSource::Unavailable,
                                          false, QStringLiteral("未采集")};
        model.metadata.connection = {};
        model.summary.passRatePpm.reset();
        model.summary.passRateNumerator = 0;
        model.summary.passRateDenominator = 0;
        model.sessionLogArtifact = {};
        const QByteArray html = fixedGenerator().generate(model).document->utf8;
        QVERIFY(html.contains("[unavailable]"));
        QVERIFY(html.contains("N/A（分母为 0）"));
        QVERIFY(html.contains("未提供"));
        QVERIFY(html.contains("本报告不会读取或嵌入 JSONL"));
    }

    void rejectsInvalidGenerationTime()
    {
        report::HtmlReportGenerator generator([] { return QDateTime{}; });
        const auto generated = generator.generate(completeModel());
        QVERIFY(!generated.succeeded());
        QCOMPARE(generated.errors.front().code,
                 report::ReportErrorCode::InvalidGenerationTime);
        QVERIFY(!generated.document);
    }

    void suggestsSafeStableFileNames()
    {
        auto model = completeModel();
        model.suiteId = QStringLiteral("  中文///***  ");
        QCOMPARE(report::HtmlReportGenerator::suggestFileName(model),
                 QStringLiteral("suite_20260906-010203-004Z_run-42.html"));
        model.suiteId = QString(60, QChar(u'A'));
        const QString fileName = report::HtmlReportGenerator::suggestFileName(model);
        QCOMPARE(fileName.section(QChar(u'_'), 0, 0).size(), 48);
        QVERIFY(fileName.endsWith(QStringLiteral("_run-42.html")));
    }

    void writesAtomicallyAndRejectsInvalidOrExistingTargets()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto model = completeModel();
        auto generator = fixedGenerator();
        const auto generated = generator.generate(model);
        QVERIFY(generated.succeeded());
        const QString target = directory.filePath(generated.document->suggestedFileName);
        const auto written = generator.write(model, target);
        QVERIFY2(written.succeeded(), written.errors.isEmpty()
            ? "unknown" : qPrintable(written.errors.front().diagnostic));
        QCOMPARE(*written.filePath, QFileInfo(target).absoluteFilePath());
        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), generated.document->utf8);

        const auto existing = generator.write(model, target);
        QVERIFY(!existing.succeeded());
        QCOMPARE(existing.errors.front().code,
                 report::ReportErrorCode::TargetAlreadyExists);

        const auto missingParent = generator.write(
            model, directory.filePath(QStringLiteral("missing/report.html")));
        QVERIFY(!missingParent.succeeded());
        QCOMPARE(missingParent.errors.front().code,
                 report::ReportErrorCode::InvalidOutputPath);

        const auto directoryTarget = generator.write(model, directory.path());
        QVERIFY(!directoryTarget.succeeded());
        QCOMPARE(directoryTarget.errors.front().code,
                 report::ReportErrorCode::InvalidOutputPath);

        const auto empty = generator.write(model, QString());
        QVERIFY(!empty.succeeded());
        QCOMPARE(empty.errors.front().code, report::ReportErrorCode::InvalidOutputPath);

#ifdef Q_OS_WIN
        const auto openFailure = generator.write(
            model, directory.filePath(QStringLiteral("invalid<name>.html")));
        QVERIFY(!openFailure.succeeded());
        QCOMPARE(openFailure.errors.front().code,
                 report::ReportErrorCode::OutputOpenFailed);
#endif
    }
};

QTEST_APPLESS_MAIN(HtmlReportTest)
#include "tst_htmlreport.moc"
