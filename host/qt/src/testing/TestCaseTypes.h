#pragma once

#include "device/RegisterMap.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <chrono>
#include <optional>

namespace oms555tv::testing {

inline constexpr int testSuiteSchemaVersion = 1;
inline constexpr int defaultRequestTimeoutMs = 500;
inline constexpr int maximumRequestTimeoutMs = 60000;
inline constexpr int maximumCaseTimeoutMs = 300000;
inline constexpr int maximumRetryCount = 3;
inline constexpr int maximumReadRegisterCount = 125;

enum class TestStatus { NotRun, Running, Pass, Fail, Skipped, Error };

enum class TestCaseType {
    ReadRegister,
    ReadRegisters,
    WriteRegister,
    WriteAndVerify,
    ExpectException,
};

enum class ModbusFunction : quint8 {
    ReadHoldingRegisters = 0x03,
    WriteSingleRegister = 0x06,
};

enum class AssertionType {
    Equals,
    Range,
    RegisterSequence,
    BitMask,
    ModbusException,
};

enum class ValueRepresentation { UInt16, Int16 };

enum class RetryError { Timeout, Connection, Serial, Crc, Protocol };

struct TimeoutPolicy {
    std::chrono::milliseconds request{defaultRequestTimeoutMs};
    std::chrono::milliseconds testCase{defaultRequestTimeoutMs};
};

struct RetryPolicy {
    int maxRetries = 0;
    QVector<RetryError> onErrors;
};

struct TestRequest {
    ModbusFunction function = ModbusFunction::ReadHoldingRegisters;
    device::PduAddress address{0};
    quint16 count = 1;
    quint16 rawValue = 0;
    bool readBeforeWrite = false;
    bool restoreOriginal = false;
};

struct ExpectedAssertion {
    AssertionType type = AssertionType::Equals;
    ValueRepresentation representation = ValueRepresentation::UInt16;
    qint64 value = 0;
    qint64 minimum = 0;
    qint64 maximum = 0;
    QVector<qint64> values;
    quint16 mask = 0;
    quint8 exceptionCode = 0;
    QString unit;
};

struct TestCase {
    QString id;
    QString name;
    QString category;
    QString description;
    TestCaseType declaredType = TestCaseType::ReadRegisters;
    // read_register 成功加载后也规范化为 ReadRegisters。
    TestCaseType type = TestCaseType::ReadRegisters;
    bool enabled = true;
    QStringList tags;
    TestRequest request;
    ExpectedAssertion expected;
    TimeoutPolicy timeout;
    RetryPolicy retry;
};

struct TestSuite {
    int schemaVersion = testSuiteSchemaVersion;
    QString id;
    QString name;
    QString description;
    QStringList tags;
    QMap<QString, QString> metadata;
    QVector<TestCase> cases;
};

enum class ConfigErrorCode {
    InvalidUtf8,
    JsonSyntax,
    RootNotObject,
    UnknownSchemaVersion,
    MissingField,
    UnknownField,
    WrongType,
    EmptyString,
    InvalidIdentifier,
    OutOfRange,
    UnknownCaseType,
    UnknownAssertionType,
    DuplicateId,
    DuplicateTag,
    AddressRangeOverflow,
    InvalidCombination,
};

struct ConfigError {
    ConfigErrorCode code = ConfigErrorCode::JsonSyntax;
    QString path;
    QString suiteId;
    QString caseId;
    QString diagnostic;
};

struct LoadResult {
    std::optional<TestSuite> suite;
    QVector<ConfigError> errors;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return suite.has_value() && errors.isEmpty();
    }
};

[[nodiscard]] QString testStatusName(TestStatus status);
[[nodiscard]] QString testCaseTypeName(TestCaseType type);
[[nodiscard]] QString assertionTypeName(AssertionType type);
[[nodiscard]] QString configErrorCodeName(ConfigErrorCode code);

} // namespace oms555tv::testing
