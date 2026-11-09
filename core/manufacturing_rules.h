#pragma once

namespace eda::validation {

// 项目约定阈值，单位 mil，统一取 3 位小数。
// 精确换算：0.2 mm = 7.874015748 mil，6.0 mm = 236.2204724 mil。
inline constexpr double kMinDrillMil = 7.874;
inline constexpr double kMaxDrillMil = 236.220;
inline constexpr double kMinAnnularRingMil = 7.874;
inline constexpr double kMaxMaskMarginMil = 7.874;
inline constexpr double kMinClearanceMil = 7.874;
inline constexpr double kMinSilkWidthMil = 6.0;
inline constexpr double kMinTrackWidthMil = 6.0;

[[nodiscard]] bool drillOk(double drillMil);
[[nodiscard]] bool annularRingOk(double padWidthMil,
                                 double padHeightMil,
                                 double drillMil);
[[nodiscard]] bool maskMarginOk(double maskMarginMil);
[[nodiscard]] bool clearanceOk(double clearanceMil);
[[nodiscard]] bool silkWidthOk(double widthMil);

} // namespace eda::validation