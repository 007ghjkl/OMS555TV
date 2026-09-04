#include "testing/TestAssertions.h"

#include <QTest>

using namespace oms555tv::testing;

class TestAssertionsTest final : public QObject
{
    Q_OBJECT

private slots:
    void equalsPassAndFailPreserveRawActualAndUnit()
    {
        ExpectedAssertion expected;
        expected.type = AssertionType::Equals;
        expected.representation = ValueRepresentation::Int16;
        expected.value = -1;
        expected.unit = QStringLiteral("0.1℃");

        auto result = evaluateAssertion(expected, ActualResult::registers({0xFFFF}));
        QVERIFY(result.passed());
        QCOMPARE(result.actualValues, QVector<qint64>({-1}));
        QCOMPARE(result.actualRawValues, QVector<quint16>({0xFFFF}));
        QCOMPARE(result.unit, QStringLiteral("0.1℃"));

        result = evaluateAssertion(expected, ActualResult::registers({0}));
        QCOMPARE(result.status, TestStatus::Fail);
        QCOMPARE(result.differences.size(), 1);
        QCOMPARE(result.differences.front().code, AssertionDifferenceCode::ValueMismatch);
        QCOMPARE(result.differences.front().expected, std::optional<qint64>(-1));
        QCOMPARE(result.differences.front().actualRaw, std::optional<quint16>(0));
    }

    void rangePassAndFailHaveStructuredBounds()
    {
        ExpectedAssertion expected;
        expected.type = AssertionType::Range;
        expected.minimum = 10;
        expected.maximum = 20;
        QVERIFY(evaluateAssertion(expected, ActualResult::registers({10})).passed());
        QVERIFY(evaluateAssertion(expected, ActualResult::registers({20})).passed());
        const auto failed = evaluateAssertion(expected, ActualResult::registers({21}));
        QCOMPARE(failed.differences.front().code, AssertionDifferenceCode::OutOfRange);
        QCOMPARE(failed.differences.front().expectedMinimum, std::optional<qint64>(10));
        QCOMPARE(failed.differences.front().expectedMaximum, std::optional<qint64>(20));
    }

    void sequencePassAndFailReportLengthAndIndexes()
    {
        ExpectedAssertion expected;
        expected.type = AssertionType::RegisterSequence;
        expected.values = {1, 2, 3};
        QVERIFY(evaluateAssertion(expected, ActualResult::registers({1, 2, 3})).passed());

        auto failed = evaluateAssertion(expected, ActualResult::registers({1, 9, 8}));
        QCOMPARE(failed.differences.size(), 2);
        QCOMPARE(failed.differences[0].code, AssertionDifferenceCode::SequenceValueMismatch);
        QCOMPARE(failed.differences[0].index, std::optional<qsizetype>(1));

        failed = evaluateAssertion(expected, ActualResult::registers({1, 2}));
        QCOMPARE(failed.differences.front().code,
                 AssertionDifferenceCode::SequenceLengthMismatch);
    }

    void bitmaskPassAndFailPreserveMaskedAndRawValues()
    {
        ExpectedAssertion expected;
        expected.type = AssertionType::BitMask;
        expected.mask = 0x000F;
        expected.value = 0x0005;
        QVERIFY(evaluateAssertion(expected, ActualResult::registers({0xFFF5})).passed());
        const auto failed = evaluateAssertion(expected, ActualResult::registers({0xFFF4}));
        QCOMPARE(failed.differences.front().code, AssertionDifferenceCode::BitMaskMismatch);
        QCOMPARE(failed.differences.front().actual, std::optional<qint64>(4));
        QCOMPARE(failed.differences.front().actualRaw, std::optional<quint16>(0xFFF4));
    }

    void exceptionPassFailAndActualTypeMismatch()
    {
        ExpectedAssertion expected;
        expected.type = AssertionType::ModbusException;
        expected.exceptionCode = 2;
        QVERIFY(evaluateAssertion(expected, ActualResult::exception(2)).passed());

        auto failed = evaluateAssertion(expected, ActualResult::exception(3));
        QCOMPARE(failed.differences.front().code,
                 AssertionDifferenceCode::ExceptionCodeMismatch);
        QCOMPARE(failed.actualExceptionCode, std::optional<quint8>(3));

        failed = evaluateAssertion(expected, ActualResult::registers({2}));
        QCOMPARE(failed.differences.front().code,
                 AssertionDifferenceCode::ActualTypeMismatch);
        QCOMPARE(failed.actualRawValues, QVector<quint16>({2}));
        QCOMPARE(failed.actualValues, QVector<qint64>({2}));

        expected.type = AssertionType::Equals;
        expected.value = 2;
        failed = evaluateAssertion(expected, ActualResult::exception(2));
        QCOMPARE(failed.differences.front().code,
                 AssertionDifferenceCode::ActualTypeMismatch);
        QCOMPARE(failed.expectedValues, QVector<qint64>({2}));
        QCOMPARE(failed.expectedSummary, QStringLiteral("2"));
    }

    void stableStatusNamesCoverExecutionModel()
    {
        QCOMPARE(testStatusName(TestStatus::NotRun), QStringLiteral("NOT_RUN"));
        QCOMPARE(testStatusName(TestStatus::Running), QStringLiteral("RUNNING"));
        QCOMPARE(testStatusName(TestStatus::Pass), QStringLiteral("PASS"));
        QCOMPARE(testStatusName(TestStatus::Fail), QStringLiteral("FAIL"));
        QCOMPARE(testStatusName(TestStatus::Skipped), QStringLiteral("SKIPPED"));
        QCOMPARE(testStatusName(TestStatus::Error), QStringLiteral("ERROR"));
    }
};

QTEST_APPLESS_MAIN(TestAssertionsTest)
#include "tst_testassertions.moc"
