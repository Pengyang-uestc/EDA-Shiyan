#include "core/manufacturing_rules.h"

#include <algorithm>
#include <cmath>

namespace eda::validation {
namespace {

constexpr double kEpsilon = 1e-9;

bool finite(double value)
{
    return std::isfinite(value);
}

} // namespace

bool drillOk(double drillMil)
{
    return finite(drillMil)
           && drillMil >= kMinDrillMil - kEpsilon
           && drillMil <= kMaxDrillMil + kEpsilon;
}

bool annularRingOk(double padWidthMil, double padHeightMil, double drillMil)
{
    if (!finite(padWidthMil) || !finite(padHeightMil) || !finite(drillMil)) {
        return false;
    }
    if (padWidthMil <= 0.0 || padHeightMil <= 0.0 || drillMil <= 0.0) {
        return false;
    }

    const double minimumPad = drillMil + 2.0 * kMinAnnularRingMil;
    return std::min(padWidthMil, padHeightMil) >= minimumPad - kEpsilon;
}

bool maskMarginOk(double maskMarginMil)
{
    return finite(maskMarginMil)
           && maskMarginMil >= -kEpsilon
           && maskMarginMil <= kMaxMaskMarginMil + kEpsilon;
}

bool clearanceOk(double clearanceMil)
{
    return finite(clearanceMil)
           && clearanceMil >= kMinClearanceMil - kEpsilon;
}

bool silkWidthOk(double widthMil)
{
    return finite(widthMil) && widthMil >= kMinSilkWidthMil - kEpsilon;
}

} // namespace eda::validation