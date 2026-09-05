#pragma once

#include "testing/TestCaseTypes.h"

#include <QJsonObject>

namespace oms555tv::testing {

[[nodiscard]] LoadResult loadTestSuiteV3(const QJsonObject &root);

} // namespace oms555tv::testing
