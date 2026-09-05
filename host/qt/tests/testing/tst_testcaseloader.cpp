#include "testing/TestCaseLoader.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace oms555tv::testing;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QString fixturePath(const QString &relative)
{
    return QStringLiteral(OMS555TV_TESTCASES_DIR) + QLatin1Char('/') + relative;
}

LoadResult loadFixture(const QString &relative)
{
    return TestCaseLoader::load(readFile(fixturePath(relative)));
}

QByteArray suiteWithCase(const QByteArray &testCase)
{
    return QByteArrayLiteral("{\"schema_version\":1,\"id\":\"suite\",\"name\":\"套件\",\"cases\":[")
        + testCase + QByteArrayLiteral("]}");
}

QByteArray v2SuiteWithCase(const QByteArray &testCase)
{
    return QByteArrayLiteral("{\"schema_version\":2,\"id\":\"suite-v2\",\"name\":\"套件\",\"cases\":[")
        + testCase + QByteArrayLiteral("]}");
}

bool hasError(const LoadResult &result, const ConfigErrorCode code, const QString &path = {})
{
    for (const auto &error : result.errors) {
        if (error.code == code && (path.isEmpty() || error.path == path)) return true;
    }
    return false;
}

} // namespace

class TestCaseLoaderTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadsMinimalSuiteAndAppliesDefaults()
    {
        const auto result = loadFixture(QStringLiteral("fixtures/valid/minimal.json"));
        QVERIFY(result.succeeded());
        QCOMPARE(result.suite->schemaVersion, 1);
        QCOMPARE(result.suite->id, QStringLiteral("minimal"));
        QVERIFY(result.suite->cases.isEmpty());
        QVERIFY(result.suite->tags.isEmpty());
        QVERIFY(result.suite->metadata.isEmpty());
    }

    void loadsAllTypesPreservesOrderAndNormalizesReadRegister()
    {
        const auto result = loadFixture(QStringLiteral("fixtures/valid/all-types.json"));
        if (!result.succeeded()) {
            for (const auto &error : result.errors) {
                qDebug() << configErrorCodeName(error.code) << error.path << error.diagnostic;
            }
        }
        QVERIFY(result.succeeded());
        QCOMPARE(result.suite->cases.size(), 6);
        QCOMPARE(result.suite->cases[0].id, QStringLiteral("read-one"));
        QCOMPARE(result.suite->cases[0].declaredType, TestCaseType::ReadRegister);
        QCOMPARE(result.suite->cases[0].type, TestCaseType::ReadRegisters);
        QCOMPARE(result.suite->cases[0].request.count, quint16(1));
        QCOMPARE(result.suite->cases[0].expected.representation, ValueRepresentation::Int16);
        QCOMPARE(result.suite->cases[1].id, QStringLiteral("read-many"));
        QCOMPARE(result.suite->cases[1].timeout.request.count(), 250);
        QCOMPARE(result.suite->cases[1].timeout.testCase.count(), 750);
        QCOMPARE(result.suite->cases[1].retry.maxRetries, 2);
        QCOMPARE(result.suite->cases[2].enabled, false);
        QVERIFY(result.suite->cases[3].request.readBeforeWrite);
        QVERIFY(result.suite->cases[3].request.restoreOriginal);
        QCOMPARE(result.suite->cases[5].request.function,
                 ModbusFunction::WriteSingleRegister);
    }

    void acceptsDocumentedBoundaries()
    {
        const auto result = loadFixture(QStringLiteral("fixtures/valid/boundaries.json"));
        QVERIFY(result.succeeded());
        QCOMPARE(result.suite->cases[0].request.address.value(), quint16(65535));
        QCOMPARE(result.suite->cases[1].request.count, quint16(125));
        QCOMPARE(result.suite->cases[1].expected.values.size(), 125);
        QCOMPARE(result.suite->cases[1].timeout.request.count(), 60000);
        QCOMPARE(result.suite->cases[1].timeout.testCase.count(), 300000);
        QCOMPARE(result.suite->cases[1].retry.maxRetries, 3);
    }

    void rejectsEveryInvalidFixtureWithStructuredContext()
    {
        const QDir directory(fixturePath(QStringLiteral("fixtures/invalid")));
        const QStringList files = directory.entryList({QStringLiteral("*.json")}, QDir::Files,
                                                       QDir::Name);
        QVERIFY(files.size() >= 5);
        for (const QString &file : files) {
            const auto result = loadFixture(QStringLiteral("fixtures/invalid/") + file);
            QVERIFY2(!result.succeeded(), qPrintable(file));
            QVERIFY2(!result.suite.has_value(), qPrintable(file));
            QVERIFY2(!result.errors.isEmpty(), qPrintable(file));
            QVERIFY2(!result.errors.front().path.isEmpty(), qPrintable(file));
            QVERIFY2(!configErrorCodeName(result.errors.front().code).isEmpty(), qPrintable(file));
        }
    }

    void reportsStableCodesAndPaths()
    {
        auto result = loadFixture(QStringLiteral("fixtures/invalid/unknown-version.json"));
        QVERIFY(hasError(result, ConfigErrorCode::UnknownSchemaVersion,
                         QStringLiteral("/schema_version")));
        result = loadFixture(QStringLiteral("fixtures/invalid/duplicate-id.json"));
        QVERIFY(hasError(result, ConfigErrorCode::DuplicateId,
                         QStringLiteral("/cases/1/id")));
        QCOMPARE(result.errors.back().suiteId, QStringLiteral("duplicate"));
        QCOMPARE(result.errors.back().caseId, QStringLiteral("same"));
        result = loadFixture(QStringLiteral("fixtures/invalid/address-overflow.json"));
        QVERIFY(hasError(result, ConfigErrorCode::AddressRangeOverflow,
                         QStringLiteral("/cases/0/request")));
        result = loadFixture(QStringLiteral("fixtures/invalid/unknown-field.json"));
        QVERIFY(hasError(result, ConfigErrorCode::UnknownField, QStringLiteral("/extra")));
    }

    void rejectsSyntaxEncodingTypesRangesAndCombinations()
    {
        auto result = TestCaseLoader::load(QByteArrayLiteral("{"));
        QVERIFY(hasError(result, ConfigErrorCode::JsonSyntax));
        result = TestCaseLoader::load(QByteArray::fromHex("7bc3287d"));
        QVERIFY(hasError(result, ConfigErrorCode::InvalidUtf8));
        result = TestCaseLoader::load(QByteArray::fromHex("efbbbf")
                                      + QByteArrayLiteral("{}"));
        QVERIFY(hasError(result, ConfigErrorCode::InvalidUtf8));
        result = TestCaseLoader::load(QByteArrayLiteral("[]"));
        QVERIFY(hasError(result, ConfigErrorCode::RootNotObject));

        result = TestCaseLoader::load(suiteWithCase(QByteArrayLiteral(
            "{\"id\":\"bad\",\"name\":\"x\",\"category\":\"x\",\"type\":\"read_register\","
            "\"request\":{\"function\":3,\"address\":0,\"count\":2},"
            "\"expected\":{\"type\":\"equals\",\"value\":0}}")));
        QVERIFY(hasError(result, ConfigErrorCode::OutOfRange,
                         QStringLiteral("/cases/0/request/count")));

        result = TestCaseLoader::load(suiteWithCase(QByteArrayLiteral(
            "{\"id\":\"bad\",\"name\":\"x\",\"category\":\"x\",\"type\":\"write_register\","
            "\"request\":{\"function\":6,\"address\":9,\"value\":65536},"
            "\"expected\":{\"type\":\"equals\",\"value\":0}}")));
        QVERIFY(hasError(result, ConfigErrorCode::OutOfRange,
                         QStringLiteral("/cases/0/request/value")));

        result = TestCaseLoader::load(suiteWithCase(QByteArrayLiteral(
            "{\"id\":\"bad\",\"name\":\"x\",\"category\":\"x\",\"type\":\"expect_exception\","
            "\"request\":{\"function\":3,\"address\":0,\"count\":1},"
            "\"expected\":{\"type\":\"modbus_exception\",\"code\":0}}")));
        QVERIFY(hasError(result, ConfigErrorCode::OutOfRange,
                         QStringLiteral("/cases/0/expected/code")));
    }

    void rejectsUnknownTypesAndUnrelatedFields()
    {
        auto result = TestCaseLoader::load(suiteWithCase(QByteArrayLiteral(
            "{\"id\":\"bad\",\"name\":\"x\",\"category\":\"x\",\"type\":\"future_type\","
            "\"request\":{},\"expected\":{}}")));
        QVERIFY(hasError(result, ConfigErrorCode::UnknownCaseType,
                         QStringLiteral("/cases/0/type")));

        result = TestCaseLoader::load(suiteWithCase(QByteArrayLiteral(
            "{\"id\":\"bad\",\"name\":\"x\",\"category\":\"x\",\"type\":\"read_register\","
            "\"request\":{\"function\":3,\"address\":0,\"count\":1},"
            "\"expected\":{\"type\":\"future_assertion\"}}")));
        QVERIFY(hasError(result, ConfigErrorCode::UnknownAssertionType,
                         QStringLiteral("/cases/0/expected/type")));

        result = TestCaseLoader::load(suiteWithCase(QByteArrayLiteral(
            "{\"id\":\"bad\",\"name\":\"x\",\"category\":\"x\",\"type\":\"write_register\","
            "\"request\":{\"function\":6,\"address\":9,\"value\":600,\"count\":1},"
            "\"expected\":{\"type\":\"equals\",\"value\":600}}")));
        QVERIFY(hasError(result, ConfigErrorCode::UnknownField,
                         QStringLiteral("/cases/0/request/count")));
    }

    void rejectsBoundaryAndCrossFieldViolations()
    {
        struct InvalidCase {
            const char *json;
            ConfigErrorCode code;
            const char *path;
        };
        const QVector<InvalidCase> cases{
            {R"({"id":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0}})", ConfigErrorCode::MissingField, "/cases/0/name"},
            {R"({"id":"x","name":"x","category":1,"type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0}})", ConfigErrorCode::WrongType, "/cases/0/category"},
            {R"({"id":" ","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0}})", ConfigErrorCode::EmptyString, "/cases/0/id"},
            {R"({"id":"x","name":"x","category":"c","tags":["same","same"],"type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0}})", ConfigErrorCode::DuplicateTag, "/cases/0/tags/1"},
            {R"({"id":"x","name":"x","category":"c","type":"read_registers","request":{"function":3,"address":0,"count":0},"expected":{"type":"register_sequence","values":[0]}})", ConfigErrorCode::OutOfRange, "/cases/0/request/count"},
            {R"({"id":"x","name":"x","category":"c","type":"read_registers","request":{"function":3,"address":0,"count":126},"expected":{"type":"register_sequence","values":[0]}})", ConfigErrorCode::OutOfRange, "/cases/0/request/count"},
            {R"({"id":"x","name":"x","category":"c","type":"write_register","request":{"function":6,"address":9,"value":-1},"expected":{"type":"equals","value":0}})", ConfigErrorCode::OutOfRange, "/cases/0/request/value"},
            {R"({"id":"x","name":"x","category":"c","type":"expect_exception","request":{"function":3,"address":0,"count":1},"expected":{"type":"modbus_exception","code":256}})", ConfigErrorCode::OutOfRange, "/cases/0/expected/code"},
            {R"({"id":"x","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"range","min":2,"max":1}})", ConfigErrorCode::InvalidCombination, "/cases/0/expected/max"},
            {R"({"id":"x","name":"x","category":"c","type":"read_registers","request":{"function":3,"address":0,"count":2},"expected":{"type":"register_sequence","values":[1]}})", ConfigErrorCode::InvalidCombination, "/cases/0/expected/values"},
            {R"({"id":"x","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"bitmask","mask":1,"value":2}})", ConfigErrorCode::InvalidCombination, "/cases/0/expected/value"},
            {R"({"id":"x","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0},"timeout":{"request_ms":0}})", ConfigErrorCode::OutOfRange, "/cases/0/timeout/request_ms"},
            {R"({"id":"x","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0},"timeout":{"request_ms":60001}})", ConfigErrorCode::OutOfRange, "/cases/0/timeout/request_ms"},
            {R"({"id":"x","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0},"timeout":{"request_ms":500,"case_ms":499}})", ConfigErrorCode::InvalidCombination, "/cases/0/timeout/case_ms"},
            {R"({"id":"x","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0},"timeout":{"request_ms":500,"case_ms":300001}})", ConfigErrorCode::OutOfRange, "/cases/0/timeout/case_ms"},
            {R"({"id":"x","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0},"retry":{"max_retries":0,"on_errors":["timeout"]}})", ConfigErrorCode::OutOfRange, "/cases/0/retry/max_retries"},
            {R"({"id":"x","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0},"retry":{"max_retries":4,"on_errors":["timeout"]}})", ConfigErrorCode::OutOfRange, "/cases/0/retry/max_retries"},
            {R"({"id":"x","name":"x","category":"c","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":0},"retry":{"max_retries":1,"on_errors":["remote_exception"]}})", ConfigErrorCode::OutOfRange, "/cases/0/retry/on_errors/0"},
            {R"({"id":"x","name":"x","category":"c","type":"read_registers","request":{"function":3,"address":0,"count":2},"expected":{"type":"equals","value":0}})", ConfigErrorCode::InvalidCombination, "/cases/0/expected"},
            {R"({"id":"x","name":"x","category":"c","type":"write_register","request":{"function":6,"address":9,"value":600},"expected":{"type":"equals","value":601}})", ConfigErrorCode::InvalidCombination, "/cases/0/expected"}
        };

        for (const auto &item : cases) {
            const auto result = TestCaseLoader::load(suiteWithCase(QByteArray(item.json)));
            QVERIFY2(hasError(result, item.code, QString::fromLatin1(item.path)), item.path);
            QVERIFY(!result.suite.has_value());
        }
    }

    void schemaAndExamplesUseLoaderConstantsAndAreLoadable()
    {
        QJsonParseError error;
        const QJsonDocument schema = QJsonDocument::fromJson(
            readFile(fixturePath(QStringLiteral("schema/test-suite-v1.schema.json"))), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(schema.isObject());
        const QJsonObject root = schema.object();
        QCOMPARE(root.value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("schema_version")).toObject()
                     .value(QStringLiteral("const")).toInt(), testSuiteSchemaVersion);
        const QJsonObject definitions = root.value(QStringLiteral("$defs")).toObject();
        QCOMPARE(definitions.value(QStringLiteral("readRequest")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("count")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumReadRegisterCount);
        QCOMPARE(definitions.value(QStringLiteral("timeout")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("request_ms")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumRequestTimeoutMs);
        QCOMPARE(definitions.value(QStringLiteral("retry")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("max_retries")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumRetryCount);

        const auto example = loadFixture(QStringLiteral("examples/phase5-schema-example.json"));
        QVERIFY(example.succeeded());
        QCOMPARE(example.suite->cases.size(), 3);
    }

    void loadsV2AllFeaturesIntoNormalizedModel()
    {
        const auto result = loadFixture(QStringLiteral("fixtures/v2/valid/all-features.json"));
        if (!result.succeeded()) {
            for (const auto &error : result.errors) {
                qDebug() << configErrorCodeName(error.code) << error.path << error.diagnostic;
            }
        }
        QVERIFY(result.succeeded());
        QCOMPARE(result.suite->schemaVersion, testSuiteSchemaVersionV2);
        QCOMPARE(result.suite->cases.size(), 5);

        const auto &elements = result.suite->cases[0];
        QCOMPARE(elements.environment, ExecutionEnvironment::Both);
        QCOMPARE(elements.expected.type, AssertionType::Elements);
        QCOMPARE(elements.expected.elements.size(), 2);
        QCOMPARE(elements.expected.elements[0].representation, ValueRepresentation::Int16);
        QCOMPARE(elements.expected.elements[0].decimalPlaces, 1);

        const auto &sequence = result.suite->cases[1];
        QCOMPARE(sequence.type, TestCaseType::Sequence);
        QCOMPARE(sequence.sequence.repeatCount, 2);
        QCOMPARE(sequence.sequence.failurePolicy, SequenceFailurePolicy::ContinueOnFailure);
        QCOMPARE(sequence.sequence.steps.size(), 2);
        QCOMPARE(sequence.sequence.steps[0].type, TestCaseType::ExpectTimeout);
        QCOMPARE(sequence.sequence.steps[0].fault, FaultInjection::NoResponse);
        QCOMPARE(sequence.sequence.steps[1].type, TestCaseType::ReadRegisters);
        QCOMPARE(sequence.sequence.steps[1].delayBefore.count(), 25);

        const auto &timeout = result.suite->cases[2];
        QCOMPARE(timeout.expected.type, AssertionType::ResponseTimeout);
        QCOMPARE(timeout.environment, ExecutionEnvironment::Fake);
        QCOMPARE(timeout.fault, FaultInjection::NoResponse);

        const auto &uptime = result.suite->cases[3];
        QCOMPARE(uptime.type, TestCaseType::Consistency);
        QCOMPARE(uptime.request.count, quint16(2));
        QCOMPARE(uptime.consistency.sampleCount, 2);
        QCOMPARE(uptime.expected.uint32.comparison, UInt32Comparison::StrictlyIncreasing);

        const auto &stability = result.suite->cases[4];
        QCOMPARE(stability.type, TestCaseType::Stability);
        QCOMPARE(stability.stability.duration.count(), minimumStabilityDurationMs);
        QCOMPARE(stability.stability.evidenceSampleLimit, defaultEvidenceSampleLimit);
        QCOMPARE(stability.stability.allowedFailureRatePpm, quint32(10000));
        QCOMPARE(stability.expected.stability.minimumSuccesses, quint64(594));
    }

    void keepsEveryV1FixtureAndPhase5SuiteCompatible()
    {
        const QStringList fixtures{
            QStringLiteral("fixtures/valid/minimal.json"),
            QStringLiteral("fixtures/valid/all-types.json"),
            QStringLiteral("fixtures/valid/boundaries.json"),
            QStringLiteral("examples/phase5-schema-example.json"),
            QStringLiteral("functional/phase5-smoke.json"),
        };
        for (const QString &fixture : fixtures) {
            const auto result = loadFixture(fixture);
            QVERIFY2(result.succeeded(), qPrintable(fixture));
            QCOMPARE(result.suite->schemaVersion, testSuiteSchemaVersion);
        }
    }

    void rejectsEveryInvalidV2FixtureWithStructuredContext()
    {
        const QDir directory(fixturePath(QStringLiteral("fixtures/v2/invalid")));
        const QStringList files = directory.entryList({QStringLiteral("*.json")}, QDir::Files,
                                                       QDir::Name);
        QCOMPARE(files.size(), 6);
        for (const QString &file : files) {
            const auto result = loadFixture(QStringLiteral("fixtures/v2/invalid/") + file);
            QVERIFY2(!result.succeeded(), qPrintable(file));
            QVERIFY2(!result.suite.has_value(), qPrintable(file));
            QVERIFY2(!result.errors.isEmpty(), qPrintable(file));
            QVERIFY2(!result.errors.front().path.isEmpty(), qPrintable(file));
            QCOMPARE(result.errors.front().suiteId.isEmpty(), false);
        }

        auto result = loadFixture(QStringLiteral("fixtures/v2/invalid/duplicate-index.json"));
        QVERIFY(hasError(result, ConfigErrorCode::DuplicateIndex,
                         QStringLiteral("/cases/0/expected/items/1/index")));
        result = loadFixture(QStringLiteral("fixtures/v2/invalid/wrong-word-order.json"));
        QVERIFY(hasError(result, ConfigErrorCode::InvalidCombination,
                         QStringLiteral("/cases/0/expected/word_order")));
        result = loadFixture(QStringLiteral("fixtures/v2/invalid/stability-overflow.json"));
        QVERIFY(hasError(result, ConfigErrorCode::OutOfRange,
                         QStringLiteral("/cases/0/stability/duration_ms")));
        result = loadFixture(QStringLiteral("fixtures/v2/invalid/statistics-conflict.json"));
        QVERIFY(hasError(result, ConfigErrorCode::StatisticsConflict,
                         QStringLiteral("/cases/0/expected")));
        result = loadFixture(QStringLiteral("fixtures/v2/invalid/unknown-field.json"));
        QVERIFY(hasError(result, ConfigErrorCode::UnknownField,
                         QStringLiteral("/cases/0/script")));
    }

    void acceptsV2DocumentedUpperBoundFixture()
    {
        const auto result = loadFixture(QStringLiteral("fixtures/v2/valid/boundaries.json"));
        if (!result.succeeded()) {
            for (const auto &error : result.errors) {
                qDebug() << configErrorCodeName(error.code) << error.path << error.diagnostic;
            }
        }
        QVERIFY(result.succeeded());
        QCOMPARE(result.suite->cases.size(), 2);
        const auto &sequence = result.suite->cases[0];
        QCOMPARE(sequence.sequence.repeatCount, maximumSequenceRepeatCount);
        QCOMPARE(sequence.sequence.steps[0].delayBefore.count(), maximumStepDelayMs);
        QCOMPARE(sequence.timeout.testCase.count(), maximumCompositeCaseTimeoutMs);
        const auto &stability = result.suite->cases[1];
        QCOMPARE(stability.stability.duration.count(), maximumStabilityDurationMs);
        QCOMPARE(stability.stability.interval.count(), minimumStabilityIntervalMs);
        QCOMPARE(stability.stability.evidenceSampleLimit, maximumEvidenceSampleLimit);
        QCOMPARE(stability.timeout.testCase.count(), maximumStabilityCaseTimeoutMs);
    }

    void rejectsV2StepIndexBudgetAndStatisticsCombinations()
    {
        struct InvalidCase {
            QByteArray json;
            ConfigErrorCode code;
            QString path;
        };
        const QVector<InvalidCase> cases{
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"read_registers","request":{"function":3,"address":0,"count":2},"expected":{"type":"elements","items":[{"index":0,"type":"equals","value":1}]}})", ConfigErrorCode::InvalidCombination, QStringLiteral("/cases/0/expected/items")},
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"sequence","repeat_count":101,"timeout":{"request_ms":100,"case_ms":1000},"steps":[{"id":"s","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":1}}]})", ConfigErrorCode::OutOfRange, QStringLiteral("/cases/0/repeat_count")},
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"sequence","timeout":{"request_ms":100,"case_ms":1000},"steps":[{"id":"s","type":"write_register","request":{"function":6,"address":9,"value":600},"expected":{"type":"equals","value":600}}]})", ConfigErrorCode::InvalidCombination, QStringLiteral("/cases/0/steps/0/type")},
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"expect_exception","request":{"function":4,"address":0,"value":1},"expected":{"type":"modbus_exception","code":1}})", ConfigErrorCode::OutOfRange, QStringLiteral("/cases/0/request/function")},
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"sequence","timeout":{"request_ms":100,"case_ms":1000},"steps":[{"id":"s","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":1}},{"id":"s","type":"read_register","request":{"function":3,"address":0,"count":1},"expected":{"type":"equals","value":1}}]})", ConfigErrorCode::DuplicateId, QStringLiteral("/cases/0/steps/1/id")},
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"expect_timeout","request":{"function":3,"address":0,"count":1},"expected":{"type":"response_timeout","source":"deterministic_no_response"},"timeout":{"request_ms":100,"case_ms":200},"retry":{"max_retries":1,"on_errors":["timeout"]},"fault":"no_response"})", ConfigErrorCode::InvalidCombination, QStringLiteral("/cases/0")},
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"consistency","request":{"function":3,"address":30,"count":2},"expected":{"type":"uint32","word_order":"low_word_first","comparison":"non_decreasing","min":0,"max":100},"timeout":{"request_ms":100,"case_ms":100},"consistency":{"sample_count":1,"interval_ms":0}})", ConfigErrorCode::InvalidCombination, QStringLiteral("/cases/0/consistency/sample_count")},
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"consistency","request":{"function":3,"address":30,"count":2},"expected":{"type":"uint32","word_order":"low_word_first","comparison":"range","min":0,"max":100},"timeout":{"request_ms":100,"case_ms":299},"consistency":{"sample_count":2,"interval_ms":100}})", ConfigErrorCode::BudgetOverflow, QStringLiteral("/cases/0/timeout/case_ms")},
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"stability","request":{"function":3,"address":0,"count":1},"expected":{"type":"stability_summary","min_total":601,"min_successes":1,"max_failures":0,"max_timeouts":0,"max_failure_rate_ppm":0,"rtt_ms":{"require_valid_samples":true,"minimum_sample":0,"maximum_average":100,"maximum_sample":100}},"timeout":{"request_ms":100,"case_ms":600100},"stability":{"duration_ms":600000,"interval_ms":1000,"minimum_successes":1,"allowed_failure_rate_ppm":0}})", ConfigErrorCode::StatisticsConflict, QStringLiteral("/cases/0/stability")},
            {R"({"id":"x","name":"x","category":"x","environment":"fake","type":"stability","request":{"function":3,"address":0,"count":1},"expected":{"type":"stability_summary","min_total":1,"min_successes":1,"max_failures":0,"max_timeouts":0,"max_failure_rate_ppm":0,"rtt_ms":{"require_valid_samples":true,"minimum_sample":0,"maximum_average":100,"maximum_sample":100}},"timeout":{"request_ms":100,"case_ms":600099},"stability":{"duration_ms":600000,"interval_ms":1000,"minimum_successes":1,"allowed_failure_rate_ppm":0}})", ConfigErrorCode::BudgetOverflow, QStringLiteral("/cases/0/timeout/case_ms")},
        };
        for (const auto &item : cases) {
            const auto result = TestCaseLoader::load(v2SuiteWithCase(item.json));
            QVERIFY2(hasError(result, item.code, item.path), qPrintable(item.path));
            QVERIFY(!result.suite.has_value());
        }
    }

    void v2SchemaAndChineseExampleMatchLoaderConstants()
    {
        QJsonParseError error;
        const QJsonDocument schema = QJsonDocument::fromJson(
            readFile(fixturePath(QStringLiteral("schema/test-suite-v2.schema.json"))), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        const QJsonObject root = schema.object();
        QCOMPARE(root.value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("schema_version")).toObject()
                     .value(QStringLiteral("const")).toInt(), testSuiteSchemaVersionV2);
        const QJsonObject definitions = root.value(QStringLiteral("$defs")).toObject();
        QCOMPARE(definitions.value(QStringLiteral("sequenceCase")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("steps")).toObject()
                     .value(QStringLiteral("maxItems")).toInt(), maximumSequenceSteps);
        QCOMPARE(definitions.value(QStringLiteral("sequenceCase")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("repeat_count")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumSequenceRepeatCount);
        QCOMPARE(definitions.value(QStringLiteral("sequenceStep")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("delay_before_ms")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumStepDelayMs);
        QCOMPARE(definitions.value(QStringLiteral("stability")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("duration_ms")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumStabilityDurationMs);
        QCOMPARE(definitions.value(QStringLiteral("stability")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("allowed_failure_rate_ppm")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), failureRateScalePpm);
        QCOMPARE(definitions.value(QStringLiteral("stability")).toObject()
                     .value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("evidence_sample_limit")).toObject()
                     .value(QStringLiteral("maximum")).toInt(), maximumEvidenceSampleLimit);

        const auto example = loadFixture(QStringLiteral("examples/phase6-schema-v2-example.json"));
        QVERIFY(example.succeeded());
        QCOMPARE(example.suite->cases.size(), 5);
    }
};

QTEST_APPLESS_MAIN(TestCaseLoaderTest)
#include "tst_testcaseloader.moc"
