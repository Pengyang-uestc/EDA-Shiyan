#pragma once

#include <QPointF>
#include <QVector>
#include <optional>

namespace eda {

enum class RouteError {
    None,
    InvalidCoordinate,
    InvalidMode
};

enum class OrthoMode {
    HV,    // 先横后竖
    VH,    // 先竖后横
    Z_H,   // 在中间 x 处拐两次
    Z_V    // 在中间 y 处拐两次
};

// 生成正交路径，单位 mil，输入输出都是世界坐标。
// 起终点相同返回 1 个点；同 x 或同 y 返回 2 个点；相邻重复点会去重。
// 近似同 x/y（容差 1e-3 mil）时会把坐标吸附到起始值，保证线段严格正交。
// Z 模式的 mid 为空时取中点，越界时夹到起终点区间内，非有限值按中点处理。
// 坐标含 NaN/Inf 或枚举值非法时返回空数组，并通过 error 给出原因。
[[nodiscard]] QVector<QPointF> orthogonalPath(
    QPointF start,
    QPointF end,
    OrthoMode mode,
    std::optional<double> mid = std::nullopt,
    RouteError* error = nullptr);

} // namespace eda