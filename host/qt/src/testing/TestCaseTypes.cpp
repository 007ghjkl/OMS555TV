#include "testing/TestCaseTypes.h"

namespace oms555tv::testing {

QString testStatusName(const TestStatus status)
{
    switch (status) {
    case TestStatus::NotRun: return QStringLiteral("NOT_RUN");
    case TestStatus::Running: return QStringLiteral("RUNNING");
    case TestStatus::Pass: return QStringLiteral("PASS");
    case TestStatus::Fail: return QStringLiteral("FAIL");
    case TestStatus::Skipped: return QStringLiteral("SKIPPED");
    case TestStatus::Error: return QStringLiteral("ERROR");
    }
    return QStringLiteral("ERROR");
}

QString testCaseTypeName(const TestCaseType type)
{
    switch (type) {
    case TestCaseType::ReadRegister: return QStringLiteral("read_register");
    case TestCaseType::ReadRegisters: return QStringLiteral("read_registers");
    case TestCaseType::WriteRegister: return QStringLiteral("write_register");
    case TestCaseType::WriteAndVerify: return QStringLiteral("write_and_verify");
    case TestCaseType::ExpectException: return QStringLiteral("expect_exception");
    }
    return {};
}

QString assertionTypeName(const AssertionType type)
{
    switch (type) {
    case AssertionType::Equals: return QStringLiteral("equals");
    case AssertionType::Range: return QStringLiteral("range");
    case AssertionType::RegisterSequence: return QStringLiteral("register_sequence");
    case AssertionType::BitMask: return QStringLiteral("bitmask");
    case AssertionType::ModbusException: return QStringLiteral("modbus_exception");
    }
    return {};
}

QString configErrorCodeName(const ConfigErrorCode code)
{
    switch (code) {
    case ConfigErrorCode::InvalidUtf8: return QStringLiteral("InvalidUtf8");
    case ConfigErrorCode::JsonSyntax: return QStringLiteral("JsonSyntax");
    case ConfigErrorCode::RootNotObject: return QStringLiteral("RootNotObject");
    case ConfigErrorCode::UnknownSchemaVersion: return QStringLiteral("UnknownSchemaVersion");
    case ConfigErrorCode::MissingField: return QStringLiteral("MissingField");
    case ConfigErrorCode::UnknownField: return QStringLiteral("UnknownField");
    case ConfigErrorCode::WrongType: return QStringLiteral("WrongType");
    case ConfigErrorCode::EmptyString: return QStringLiteral("EmptyString");
    case ConfigErrorCode::InvalidIdentifier: return QStringLiteral("InvalidIdentifier");
    case ConfigErrorCode::OutOfRange: return QStringLiteral("OutOfRange");
    case ConfigErrorCode::UnknownCaseType: return QStringLiteral("UnknownCaseType");
    case ConfigErrorCode::UnknownAssertionType: return QStringLiteral("UnknownAssertionType");
    case ConfigErrorCode::DuplicateId: return QStringLiteral("DuplicateId");
    case ConfigErrorCode::DuplicateTag: return QStringLiteral("DuplicateTag");
    case ConfigErrorCode::AddressRangeOverflow: return QStringLiteral("AddressRangeOverflow");
    case ConfigErrorCode::InvalidCombination: return QStringLiteral("InvalidCombination");
    }
    return QStringLiteral("JsonSyntax");
}

} // namespace oms555tv::testing
