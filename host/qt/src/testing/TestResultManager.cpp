#include "testing/TestResultManager.h"

namespace oms555tv::testing {

TestResultManager::TestResultManager(QObject *parent) : QObject(parent) {}

std::shared_ptr<const TestSuiteResult> TestResultManager::snapshot() const noexcept
{
    return snapshot_;
}

const QVector<std::shared_ptr<const TestSuiteResult>> &TestResultManager::history() const noexcept
{
    return history_;
}

void TestResultManager::publish(const TestSuiteResult &result, bool terminal)
{
    snapshot_ = std::make_shared<const TestSuiteResult>(result);
    if (terminal) {
        history_.append(snapshot_);
    }
    emit snapshotChanged();
}

void TestResultManager::clearHistory()
{
    history_.clear();
}

} // namespace oms555tv::testing
