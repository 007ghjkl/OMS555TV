#pragma once

#include "communication/IModbusClient.h"
#include "logging/LogEntry.h"

#include <QObject>
#include <QVector>

#include <optional>

namespace oms555tv::diagnostics {

struct DiagnosticRecord {
    communication::ModbusRequestResult result;
    logging::LogLevel level = logging::LogLevel::Debug;
};

struct DiagnosticFilter {
    std::optional<logging::LogLevel> level;
    std::optional<bool> succeeded;
    std::optional<communication::RequestId> requestId;
};

[[nodiscard]] DiagnosticRecord makeDiagnosticRecord(
    const communication::ModbusRequestResult &result);
[[nodiscard]] QString byteArrayHex(const QByteArray &bytes);
[[nodiscard]] QString ownerName(communication::CommunicationOwner owner);
[[nodiscard]] QString requestStateName(communication::RequestState state);
[[nodiscard]] QString crcStatusName(communication::CrcStatus status);
[[nodiscard]] QString descriptorSummary(const communication::RequestDescriptor &descriptor);
[[nodiscard]] QString diagnosticDetails(const DiagnosticRecord &record);

class CommunicationDiagnosticsModel final : public QObject
{
    Q_OBJECT

public:
    explicit CommunicationDiagnosticsModel(communication::IModbusClient &client,
                                           qsizetype capacity = 1000,
                                           QObject *parent = nullptr);

    [[nodiscard]] qsizetype capacity() const noexcept;
    [[nodiscard]] const QVector<DiagnosticRecord> &records() const noexcept;
    [[nodiscard]] QVector<DiagnosticRecord> filtered(
        const DiagnosticFilter &filter) const;
    void clear();

signals:
    void recordAdded(const oms555tv::diagnostics::DiagnosticRecord &record);
    void recordsChanged();

private:
    void append(const communication::ModbusRequestResult &result);

    qsizetype capacity_;
    QVector<DiagnosticRecord> records_;
};

} // namespace oms555tv::diagnostics

Q_DECLARE_METATYPE(oms555tv::diagnostics::DiagnosticRecord)
