#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QSizeF>
#include <QVector>

#include "core/status.h"

namespace eda {

struct LayoutResult {
    Status status;
    QVector<QPointF> positions;   // mil，包围盒中心，与输入顺序一一对应
};

// 规则网格布局，单位 mil，坐标是世界坐标。
// sizesMil 必须是实例旋转后的轴对齐包围盒尺寸；未旋转的尺寸由调用方先转换。
// gapMil 为包围盒边缘到边缘的间距；columns <= 0 时按 ceil(sqrt(n)) 取列数。
// 只做“整组居中放置”：居中放不下就返回失败，不尝试其它平移位置。
// 所有矩形必须完整落在 outlineMil 内（允许贴边），否则返回失败且 positions 为空。
[[nodiscard]] LayoutResult gridLayout(const QVector<QSizeF>& sizesMil,
                                      const QPolygonF& outlineMil,
                                      double gapMil,
                                      int columns);

} // namespace eda