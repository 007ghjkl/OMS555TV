#pragma once

#include "diagnostics/CommunicationDiagnostics.h"
#include "logging/LogEntry.h"

#include <QFile>
#include <QObject>
#include <QVector>

#include <optional>

namespace oms555tv::logging {

enum class SessionLogErrorCode {
    AlreadyActive,
    NotActive,
    CreateDirectoryFailed,
    OpenFailed,
    WriteFailed,
    FlushFailed,
    RetentionLimitReached,
};

struct SessionLogError {
    SessionLogErrorCode code = SessionLogErrorCode::WriteFailed;
    QString diagnostic;
};

struct SessionLogResult {
    bool succeeded = false;
    QString sessionId;
    QString filePath;
    std::optional<SessionLogError> error;
};

class SessionLogService final : public QObject
{
    Q_OBJECT

public:
    explicit SessionLogService(
        diagnostics::CommunicationDiagnosticsModel &diagnostics,
        QString outputRoot = {},
        qsizetype memoryCapacity = 2000,
        qint64 maximumFileBytes = 10 * 1024 * 1024,
        QObject *parent = nullptr);
    ~SessionLogService() override;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] QString sessionId() const;
    [[nodiscard]] QString currentFilePath() const;
    [[nodiscard]] const QVector<LogEntry> &entries() const noexcept;
    [[nodiscard]] const std::optional<SessionLogError> &lastError() const noexcept;

    SessionLogResult startSession(const QVariantMap &metadata = {});
    SessionLogResult endSession();
    void append(LogEntry entry);
    void clearMemory();

signals:
    void entriesChanged();
    void sessionStateChanged(bool active);
    void fileError(const oms555tv::logging::SessionLogError &error);

private:
    void appendDiagnostic(const diagnostics::DiagnosticRecord &record);
    [[nodiscard]] QByteArray serialize(const LogEntry &entry) const;
    [[nodiscard]] bool openPart(int part);
    [[nodiscard]] bool rotateIfNeeded(qint64 nextBytes);
    void recordError(SessionLogError error);

    QString outputRoot_;
    qsizetype memoryCapacity_;
    qint64 maximumFileBytes_;
    QVector<LogEntry> entries_;
    QString sessionId_;
    QString baseStem_;
    QString currentFilePath_;
    QFile file_;
    int part_ = 0;
    bool active_ = false;
    bool fileWritingEnabled_ = false;
    std::optional<SessionLogError> lastError_;
};

} // namespace oms555tv::logging

Q_DECLARE_METATYPE(oms555tv::logging::SessionLogError)
