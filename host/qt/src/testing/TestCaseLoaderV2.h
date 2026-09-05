#pragma once

#include "testing/TestCaseTypes.h"

#include <QJsonObject>

namespace oms555tv::testing {

[[nodiscard]] LoadResult loadTestSuiteV2(const QJsonObject &root);

} // namespace oms555tv::testing
