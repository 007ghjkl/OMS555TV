#pragma once

#include "testing/TestCaseTypes.h"

#include <QByteArray>

namespace oms555tv::testing {

class TestCaseLoader final
{
public:
    [[nodiscard]] static LoadResult load(const QByteArray &utf8Json);
};

} // namespace oms555tv::testing
