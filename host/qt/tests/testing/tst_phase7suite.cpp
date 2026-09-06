#include "testing/TestCaseLoader.h"

#include <QFile>
#include <QTest>

using namespace oms555tv;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

} // namespace

class Phase7SuiteTest final : public QObject
{
    Q_OBJECT

private slots:
    void formalCatalogIsUniqueSafeAndEnforcesStableRecoveryUnderFiveSeconds()
    {
        const auto loaded = testing::TestCaseLoader::load(
            readFile(QStringLiteral(OMS555TV_PHASE7_RS485_SUITE)));
        for (const auto &error : loaded.errors) {
            qDebug() << testing::configErrorCodeName(error.code)
                     << error.path << error.diagnostic;
        }
        QVERIFY(loaded.succeeded());
        QCOMPARE(loaded.suite->schemaVersion, testing::testSuiteSchemaVersionV3);
        QCOMPARE(loaded.suite->id,
                 QStringLiteral("phase7-rs485-disconnect-recovery"));
        QCOMPARE(loaded.suite->cases.size(), 1);

        const auto &testCase = loaded.suite->cases.front();
        QCOMPARE(testCase.id, QStringLiteral("TC-R001"));
        QCOMPARE(testCase.environment, testing::ExecutionEnvironment::RealRs485);
        QCOMPARE(testCase.type, testing::TestCaseType::GuidedRecovery);
        QVERIFY(testCase.enabled);
        QCOMPARE(testCase.guidedRecovery.steps.size(), testing::guidedRecoveryStepCount);

        const auto &disconnect = testCase.guidedRecovery.steps[0];
        const auto &outage = testCase.guidedRecovery.steps[1];
        const auto &reconnect = testCase.guidedRecovery.steps[2];
        const auto &recovery = testCase.guidedRecovery.steps[3];
        QCOMPARE(disconnect.type, testing::GuidedStepType::OperatorPrompt);
        QCOMPARE(disconnect.operatorStep->purpose,
                 testing::GuidedPromptPurpose::DisconnectRs485);
        QVERIFY(disconnect.operatorStep->instruction.contains(QStringLiteral("只断开")));
        QVERIFY(disconnect.operatorStep->safetyNotice.contains(QStringLiteral("不要拔 USB")));
        QCOMPARE(outage.type, testing::GuidedStepType::ObserveOutage);
        QCOMPARE(reconnect.type, testing::GuidedStepType::OperatorPrompt);
        QCOMPARE(reconnect.operatorStep->purpose,
                 testing::GuidedPromptPurpose::ReconnectRs485);
        QVERIFY(reconnect.operatorStep->instruction.contains(QStringLiteral("原极性")));
        QCOMPARE(recovery.type, testing::GuidedStepType::ObserveRecovery);

        for (const auto *step : {&outage, &recovery}) {
            QVERIFY(step->observationStep.has_value());
            QCOMPARE(step->observationStep->probe.function,
                     testing::ModbusFunction::ReadHoldingRegisters);
            QCOMPARE(step->observationStep->probe.address.value(), quint16(40));
            QCOMPARE(step->observationStep->probe.count, quint16(1));
            QCOMPARE(step->observationStep->consecutiveMatches, 3);
        }
        QCOMPARE(outage.observationStep->target,
                 testing::GuidedObservationTarget::ConsecutiveResponseTimeouts);
        QCOMPARE(recovery.observationStep->target,
                 testing::GuidedObservationTarget::ConsecutiveValidResponses);
        QVERIFY(recovery.observationStep->deadline < std::chrono::seconds(5));
        QVERIFY(recovery.observationStep->businessAssertion.has_value());
        QCOMPARE(recovery.observationStep->businessAssertion->type,
                 testing::AssertionType::Equals);
        QCOMPARE(recovery.observationStep->businessAssertion->value, qint64(2));
    }
};

QTEST_MAIN(Phase7SuiteTest)

#include "tst_phase7suite.moc"
