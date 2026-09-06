#pragma once

#include "report/ReportTypes.h"

namespace oms555tv::report {

class ReportModelBuilder final
{
public:
    [[nodiscard]] static ReportBuildResult build(const ReportInput &input);
};

} // namespace oms555tv::report
