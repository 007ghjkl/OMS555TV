#pragma once

#include "testing/TestEngineTypes.h"

#include <QObject>

namespace oms555tv::testing {

class TestResultManager final : public QObject
{
    Q_OBJECT

public:
    explicit TestResultManager(QObject *parent = nullptr);

    [[nodiscard]] std::shared_ptr<const TestSuiteResult> snapshot() const noexcept;
    [[nodiscard]] const QVector<std::shared_ptr<const TestSuiteResult>> &history() const noexcept;
    void publish(const TestSuiteResult &result, bool terminal);
    void clearHistory();

signals:
    void snapshotChanged();

private:
    std::shared_ptr<const TestSuiteResult> snapshot_;
    QVector<std::shared_ptr<const TestSuiteResult>> history_;
};

} // namespace oms555tv::testing
