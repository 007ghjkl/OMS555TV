#include "testing/TestAssertions.h"

#include <QTest>

#include <algorithm>

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

    void elementsPreserveRawSignedAndDecimalScale()
    {
        ExpectedAssertion expected;
        expected.type = AssertionType::Elements;
        ElementAssertion temperature;
        temperature.index = 0;
        temperature.type = AssertionType::Range;
        temperature.representation = ValueRepresentation::Int16;
        temperature.minimum = -10;
        temperature.maximum = 10;
        temperature.decimalPlaces = 1;
        temperature.unit = QStringLiteral("℃");
        ElementAssertion status;
        status.index = 1;
        status.type = AssertionType::BitMask;
        status.mask = 0x0021;
        status.value = 0x0021;
        expected.elements = {temperature, status};

        const auto passed = evaluateAssertion(expected,
                                              ActualResult::registers({0xFFFF, 0xFFE1}));
        QVERIFY(passed.passed());
        QCOMPARE(passed.expectedElements.size(), 2);
        QCOMPARE(passed.expectedElements[0].minimum, qint64(-10));
        QCOMPARE(passed.expectedElements[0].decimalPlaces, 1);
        QCOMPARE(passed.actualRawValues, QVector<quint16>({0xFFFF, 0xFFE1}));
        QCOMPARE(passed.actualValues, QVector<qint64>({-1, 0xFFE1}));
        QCOMPARE(passed.actualScaledValues[0].numerator, qint64(-1));
        QCOMPARE(passed.actualScaledValues[0].denominator, qint64(10));
        QVERIFY(passed.actualSummary.contains(QStringLiteral("-0.1℃")));

        const auto failed = evaluateAssertion(expected,
                                              ActualResult::registers({11, 0x0001}));
        QCOMPARE(failed.status, TestStatus::Fail);
        QCOMPARE(failed.differences.size(), 2);
        QCOMPARE(failed.differences[0].index, std::optional<qsizetype>(0));
        QCOMPARE(failed.differences[0].actualRaw, std::optional<quint16>(11));
        QCOMPARE(failed.differences[0].scaledDenominator, std::optional<qint64>(10));
    }

    void uint32LowWordFirstRangeAndMonotonicity()
    {
        ExpectedAssertion expected;
        expected.type = AssertionType::UInt32;
        expected.uint32.minimum = 65536;
        expected.uint32.maximum = 65538;
        expected.uint32.comparison = UInt32Comparison::NonDecreasing;
        expected.uint32.unit = QStringLiteral("s");
        auto result = evaluateAssertion(expected,
            ActualResult::samples({{0, 1}, {1, 1}, {1, 1}, {2, 1}}));
        QVERIFY(result.passed());
        QVERIFY(result.expectedUInt32.has_value());
        QCOMPARE(result.expectedUInt32->wordOrder, UInt32WordOrder::LowWordFirst);
        QCOMPARE(result.expectedUInt32->comparison, UInt32Comparison::NonDecreasing);
        QCOMPARE(result.actualValues,
                 QVector<qint64>({65536, 65537, 65537, 65538}));
        QCOMPARE(result.actualRawSamples[0], QVector<quint16>({0, 1}));

        expected.uint32.comparison = UInt32Comparison::StrictlyIncreasing;
        result = evaluateAssertion(expected,
            ActualResult::samples({{0, 1}, {1, 1}, {1, 1}}));
        QCOMPARE(result.status, TestStatus::Fail);
        QCOMPARE(result.differences.back().code,
                 AssertionDifferenceCode::UInt32MonotonicityMismatch);
        QCOMPARE(result.differences.back().index, std::optional<qsizetype>(2));

        result = evaluateAssertion(expected, ActualResult::samples({{0, 1}, {0}}));
        QCOMPARE(result.differences.front().code,
                 AssertionDifferenceCode::UInt32WordCountMismatch);
    }

    void responseTimeoutDistinguishesFailAndError()
    {
        ExpectedAssertion expected;
        expected.type = AssertionType::ResponseTimeout;
        QVERIFY(evaluateAssertion(expected, ActualResult::responseTimeout()).passed());
        QCOMPARE(evaluateAssertion(expected, ActualResult::registers({1})).status,
                 TestStatus::Fail);
        QCOMPARE(evaluateAssertion(expected, ActualResult::exception(2)).status,
                 TestStatus::Fail);
        QCOMPARE(evaluateAssertion(expected, ActualResult::communicationError()).status,
                 TestStatus::Error);
    }

    void stabilityFailureRateBoundaryIsDeterministic()
    {
        ExpectedAssertion expected;
        expected.type = AssertionType::StabilitySummary;
        expected.stability.minimumTotal = 100;
        expected.stability.minimumSuccesses = 99;
        expected.stability.maximumFailures = 1;
        expected.stability.maximumTimeouts = 0;
        expected.stability.maximumFailureRatePpm = 10000;
        expected.stability.requireValidRttSamples = true;
        expected.stability.minimumRttMs = 1;
        expected.stability.maximumAverageRttMs = 10;
        expected.stability.maximumRttMs = 20;

        StabilityStatistics exactly;
        exactly.total = 100;
        exactly.successes = 99;
        exactly.failures = 1;
        exactly.validRttSamples = 99;
        exactly.minimumRttMs = 1;
        exactly.averageRttMs = 5;
        exactly.maximumRttMs = 10;
        QVERIFY(evaluateAssertion(expected,
                                  ActualResult::stabilitySummary(exactly)).passed());
        const auto exactResult = evaluateAssertion(expected,
                                                    ActualResult::stabilitySummary(exactly));
        QVERIFY(exactResult.expectedStability.has_value());
        QCOMPARE(exactResult.expectedStability->maximumFailureRatePpm, quint32(10000));

        StabilityStatistics exceeded = exactly;
        exceeded.successes = 98;
        exceeded.failures = 2;
        auto result = evaluateAssertion(expected,
                                        ActualResult::stabilitySummary(exceeded));
        QCOMPARE(result.status, TestStatus::Fail);
        QVERIFY(std::any_of(result.differences.cbegin(), result.differences.cend(),
                            [](const AssertionDifference &difference) {
            return difference.code == AssertionDifferenceCode::FailureRateExceeded;
        }));

        StabilityStatistics missingRtt = exactly;
        missingRtt.validRttSamples = 0;
        missingRtt.minimumRttMs.reset();
        missingRtt.averageRttMs.reset();
        missingRtt.maximumRttMs.reset();
        result = evaluateAssertion(expected, ActualResult::stabilitySummary(missingRtt));
        QCOMPARE(result.differences.back().code, AssertionDifferenceCode::RttMissing);
    }
};

QTEST_APPLESS_MAIN(TestAssertionsTest)
#include "tst_testassertions.moc"
