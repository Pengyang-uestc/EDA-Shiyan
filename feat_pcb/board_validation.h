#pragma once

#include "core/board.h"
#include "core/io.h"
#include "core/validation.h"

#include <functional>

namespace eda {

// 可选的封装制造规则回调；不传时只做板级规则和结构/引用检查，
// 并在有实例时给出 BOARD_FOOTPRINT_UNCHECKED Warning。
// 传 C 的 validateFootprint 即可把封装规则并入板级检查，避免 feat_pcb 直接依赖 feat_fp。
using FootprintValidation = std::function<QVector<Issue>(const Footprint&)>;

// 单位/坐标沿用 Board 的 mil、board-world。
// 检查结构、封装引用、标准层、层组/层顺序、实例中心与焊盘四角越界、
// 走线宽度/中心线越界、过孔钻孔/环宽/中心越界、覆铜间距/顶点越界。
// 说明：走线只查端点与中点，过孔只查中心，覆铜只查顶点；
// 线宽/孔径/覆铜边与凹形板框的精确边缘 DRC 留给下一阶段。
// 过孔环宽沿用焊盘规则：(diameter - drill) / 2 >= 0.2 mm，且 diameter > drill。
// 返回空数组表示通过；Error 阻止保存，Warning 允许确认后继续。
[[nodiscard]] QVector<Issue> validateBoardManufacturing(
    const Board& board,
    const FootprintResolver& resolveFootprint,
    const FootprintValidation& validateFootprint = {});

} // namespace eda