#pragma once

#include "core/footprint.h"
#include "core/validation.h"

#include <QVector>

namespace eda {

// 单位/坐标：沿用 Footprint 的 mil、footprint-local。
// 先跑 core 的结构校验，再跑制造规则：钻孔、环宽、阻焊、间距、引脚占用、
// 焊盘编号、安装孔孔径、丝印线宽。
// 返回空数组表示通过；Error 阻止保存，Warning 允许确认后继续。
// 间距检查只比较同层焊盘；封装没有网络信息，所有同层焊盘按不同网络处理。
// 长圆/圆角矩形暂用外接矩形，无法精确判断时给 Warning，不阻止保存。
[[nodiscard]] QVector<Issue> validateFootprint(const Footprint& footprint);

} // namespace eda