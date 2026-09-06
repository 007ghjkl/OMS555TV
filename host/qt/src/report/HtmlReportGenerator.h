#pragma once

#include "report/ReportTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace oms555tv::report {

struct HtmlReportDocument {
    QByteArray utf8;
    QString suggestedFileName;
    QString generatedAtUtcIso;
};

struct HtmlReportGenerationResult {
    std::optional<HtmlReportDocument> document;
    QVector<ReportError> errors;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return document.has_value() && errors.isEmpty();
    }
};

struct HtmlReportWriteResult {
    std::optional<QString> filePath;
    QVector<ReportError> errors;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return filePath.has_value() && errors.isEmpty();
    }
};

class HtmlReportGenerator final {
public:
    using Clock = std::function<QDateTime()>;

    explicit HtmlReportGenerator(Clock clock = {});

    [[nodiscard]] HtmlReportGenerationResult generate(
        const ReportDocumentModel &model) const;
    [[nodiscard]] HtmlReportWriteResult write(
        const ReportDocumentModel &model, const QString &targetFilePath) const;

    [[nodiscard]] static QString suggestFileName(const ReportDocumentModel &model);

private:
    Clock clock_;
};

} // namespace oms555tv::report
