#pragma once

#include "core/board.h"
#include "core/footprint.h"

#include <QPointF>
#include <QPolygonF>
#include <QVector>

namespace eda {

// 焊盘外形的课程级近似：圆用 4 的倍数个点（默认 32），矩形用 4 点，
// 长圆用半径 = min(w,h)/2 的圆角矩形，圆角矩形用 cornerRadius。
// 单位 mil；padOutline 是 footprint-local，padOutlineOnBoard 是 board-world。
[[nodiscard]] QPolygonF padOutline(const Pad& pad, int circleSegments = 32);
[[nodiscard]] QPolygonF padOutlineOnBoard(const Pad& pad,
                                          QPointF instancePos,
                                          double rotationDeg,
                                          BoardSide side,
                                          int circleSegments = 32);

// 铜体到点的距离；点在铜体内返回 0。圆形精确，其它形状用上面的多边形。
[[nodiscard]] double distancePointToPad(QPointF point,
                                        const Pad& pad,
                                        QPointF instancePos,
                                        double rotationDeg,
                                        BoardSide side);

// 铜体到线段的距离；线段在铜体内返回 0。
[[nodiscard]] double distanceSegmentToPad(QPointF a,
                                          QPointF b,
                                          const Pad& pad,
                                          QPointF instancePos,
                                          double rotationDeg,
                                          BoardSide side);

// 两个板级焊盘铜体之间的最小距离；重叠返回 0。
[[nodiscard]] double distancePadToPad(const Pad& a,
                                      QPointF instancePosA,
                                      double rotationDegA,
                                      BoardSide sideA,
                                      const Pad& b,
                                      QPointF instancePosB,
                                      double rotationDegB,
                                      BoardSide sideB);

// 两个简单多边形之间的最小距离；重叠返回 0。
[[nodiscard]] double distancePolygonToPolygon(const QPolygonF& a,
                                              const QPolygonF& b);

// 焊盘铜体到板级多边形（覆铜）的最小距离；圆形焊盘精确，其它走多边形。
[[nodiscard]] double distancePadToPolygon(const Pad& pad,
                                          QPointF instancePos,
                                          double rotationDeg,
                                          BoardSide side,
                                          const QPolygonF& polygon);

// 封装局部包围盒（含焊盘和丝印）；无图元时返回 100×100 的占位框。
[[nodiscard]] QRectF footprintBounds(const Footprint& footprint);

// 实例在板级坐标下的轴对齐包围盒（含旋转/底面）。
[[nodiscard]] QRectF instanceBounds(const Instance& instance,
                                    const Footprint& footprint);

// 通孔焊盘返回所有铜层；SMD 只返回映射后的本层。
// copperLayers 为空时退化为顶层/底层铜（兼容旧调用）。
[[nodiscard]] QVector<LayerId> padCopperLayers(
    const Pad& pad, BoardSide side,
    const QVector<LayerId>& copperLayers = {});

} // namespace eda