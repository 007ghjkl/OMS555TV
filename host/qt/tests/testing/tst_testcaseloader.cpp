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
};

QTEST_APPLESS_MAIN(TestCaseLoaderTest)
#include "tst_testcaseloader.moc"
