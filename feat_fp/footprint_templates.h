#pragma once

#include "core/footprint.h"
#include "core/status.h"

namespace eda {

// 全部为通孔、顶层、mil、footprint-local；原点在焊盘包围盒中心。
// 返回的 Footprint 已通过 core 的结构校验；制造规则由 validateFootprint 再查。
[[nodiscard]] Result<Footprint> makeDip8Footprint();
[[nodiscard]] Result<Footprint> makeDip16Footprint();
[[nodiscard]] Result<Footprint> makeRad01Footprint();
[[nodiscard]] Result<Footprint> makeAxial04Footprint();

} // namespace eda