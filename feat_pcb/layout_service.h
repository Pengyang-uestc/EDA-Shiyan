#pragma once

#include "core/board.h"
#include "core/io.h"

#include <QPointF>
#include <QRectF>
#include <QVector>

namespace eda {

struct InstanceLayoutOptions {
    QPointF origin{100, 100};
    double gapMil = 100.0;
    int columns = 0;   // <=0: ceil(sqrt(instanceCount))
};

struct InstanceLayoutResult {
    Status status;
    QVector<QPointF> positions;   // 与 board.instances 顺序一致的新 Instance.pos
    QVector<QRectF> bounds;       // 应用 positions 后的板级包围盒
};

// 计算实例的新坐标，不直接修改 Board；B 的 PlaceTool 通过命令应用。
// 使用 gridLayout（整组居中）后平移到 origin，并按每个实例的旋转/底面包围盒换算成 Instance.pos。
// 板框非空时要求所有实例包围盒在 outline 内；缺封装返回失败。
[[nodiscard]] InstanceLayoutResult layoutInstances(
    const Board& board,
    const FootprintResolver& resolveFootprint,
    const InstanceLayoutOptions& options = {});

} // namespace eda
