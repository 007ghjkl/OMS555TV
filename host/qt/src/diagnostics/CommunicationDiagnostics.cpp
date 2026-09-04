#include "diagnostics/CommunicationDiagnostics.h"

#include <algorithm>
#include <variant>

namespace oms555tv::diagnostics {

DiagnosticRecord makeDiagnosticRecord(
    const communication::ModbusRequestResult &result)
{
    DiagnosticRecord record{result, logging::LogLevel::Debug};
    if (result.state == communication::RequestState::Succeeded) {
        return record;
    }
    if (result.state == communication::RequestState::Cancelled
        || (result.error
            && result.error->category == communication::ErrorCategory::Cancelled)) {
        record.level = logging::LogLevel::Info;
    } else if (result.error
               && (result.error->category == communication::ErrorCategory::Timeout
                   || result.error->category
                       == communication::ErrorCategory::RemoteException)) {
        record.level = logging::LogLevel::Warning;
    } else {
        record.level = logging::LogLevel::Error;
    }
    return record;
}

QString byteArrayHex(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

QString ownerName(communication::CommunicationOwner owner)
{
    switch (owner) {
    case communication::CommunicationOwner::None: return QStringLiteral("None");
    case communication::CommunicationOwner::Monitor: return QStringLiteral("Monitor");
    case communication::CommunicationOwner::Testing: return QStringLiteral("Testing");
    case communication::CommunicationOwner::ManualDebug: return QStringLiteral("ManualDebug");
    }
    return QStringLiteral("Unknown");
}

QString requestStateName(communication::RequestState state)
{
    switch (state) {
    case communication::RequestState::Queued: return QStringLiteral("Queued");
    case communication::RequestState::InFlight: return QStringLiteral("InFlight");
    case communication::RequestState::Succeeded: return QStringLiteral("Succeeded");
    case communication::RequestState::Failed: return QStringLiteral("Failed");
    case communication::RequestState::Cancelled: return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

QString crcStatusName(communication::CrcStatus status)
{
    switch (status) {
    case communication::CrcStatus::NotAvailable: return QStringLiteral("N/A");
    case communication::CrcStatus::Valid: return QStringLiteral("Valid");
    case communication::CrcStatus::Invalid: return QStringLiteral("Invalid");
    }
    return QStringLiteral("Unknown");
}

QString descriptorSummary(const communication::RequestDescriptor &descriptor)
{
    if (const auto *read = std::get_if<communication::ReadRequestDescriptor>(&descriptor)) {
        return QStringLiteral("地址 %1，数量 %2")
            .arg(read->startAddress.value()).arg(read->count);
    }
    const auto &write = std::get<communication::WriteRequestDescriptor>(descriptor);
    return QStringLiteral("地址 %1，值 0x%2")
        .arg(write.address.value())
        .arg(write.rawValue, 4, 16, QLatin1Char('0'))
        .toUpper();
}

QString diagnosticDetails(const DiagnosticRecord &record)
{
    const auto &result = record.result;
    const auto &evidence = result.evidence;
    const QString rtt = evidence.rtt
        ? QStringLiteral("%1 ms").arg(
              std::chrono::duration<double, std::milli>(*evidence.rtt).count(),
              0, 'f', 3)
        : QStringLiteral("--");
    QString text = QStringLiteral(
        "完成时间：%1\nRequestId：%2\nowner：%3\n结果：%4\n"
        "功能码：0x%5\n请求：%6\nRTT：%7\n"
        "TX 校验：%8\nRX 校验：%9\nTX：%10\nRX：%11")
        .arg(evidence.completedUtc.toLocalTime().toString(Qt::ISODateWithMs))
        .arg(result.requestId.value)
        .arg(ownerName(evidence.owner))
        .arg(requestStateName(result.state))
        .arg(evidence.functionCode, 2, 16, QLatin1Char('0'))
        .arg(descriptorSummary(result.descriptor))
        .arg(rtt)
        .arg(crcStatusName(evidence.txCrcStatus))
        .arg(crcStatusName(evidence.rxCrcStatus))
        .arg(byteArrayHex(evidence.txAdu))
        .arg(byteArrayHex(evidence.rxAdu));
    if (result.error) {
        text += QStringLiteral("\n错误类别：%1\n错误码：%2")
            .arg(static_cast<int>(result.error->category))
            .arg(static_cast<int>(result.error->code));
        if (result.error->exceptionCode) {
            text += QStringLiteral("\n异常码：0x%1")
                .arg(*result.error->exceptionCode, 2, 16, QLatin1Char('0'));
        }
        if (!result.error->diagnostic.isEmpty()) {
            text += QStringLiteral("\n说明：%1").arg(result.error->diagnostic);
        }
    }
    return text;
}

CommunicationDiagnosticsModel::CommunicationDiagnosticsModel(
    communication::IModbusClient &client,
    qsizetype capacity,
    QObject *parent)
    : QObject(parent)
    , capacity_(std::max<qsizetype>(1, capacity))
{
    qRegisterMetaType<DiagnosticRecord>();
    connect(&client, &communication::IModbusClient::requestCompleted,
            this, &CommunicationDiagnosticsModel::append);
}

qsizetype CommunicationDiagnosticsModel::capacity() const noexcept
{
    return capacity_;
}

const QVector<DiagnosticRecord> &CommunicationDiagnosticsModel::records() const noexcept
{
    return records_;
}

QVector<DiagnosticRecord> CommunicationDiagnosticsModel::filtered(
    const DiagnosticFilter &filter) const
{
    QVector<DiagnosticRecord> result;
    result.reserve(records_.size());
    for (const auto &record : records_) {
        if (filter.level && record.level != *filter.level) {
            continue;
        }
        const bool succeeded = record.result.state
            == communication::RequestState::Succeeded;
        if (filter.succeeded && succeeded != *filter.succeeded) {
            continue;
        }
        if (filter.requestId && record.result.requestId != *filter.requestId) {
            continue;
        }
        result.push_back(record);
    }
    return result;
}

void CommunicationDiagnosticsModel::clear()
{
    if (records_.isEmpty()) {
        return;
    }
    records_.clear();
    emit recordsChanged();
}

void CommunicationDiagnosticsModel::append(
    const communication::ModbusRequestResult &result)
{
    const DiagnosticRecord record = makeDiagnosticRecord(result);
    if (records_.size() >= capacity_) {
        records_.remove(0, records_.size() - capacity_ + 1);
    }
    records_.push_back(record);
    emit recordAdded(record);
    emit recordsChanged();
}

} // namespace oms555tv::diagnostics
