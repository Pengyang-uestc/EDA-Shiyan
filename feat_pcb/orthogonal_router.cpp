#include "feat_pcb/orthogonal_router.h"

#include <algorithm>
#include <cmath>

namespace eda {
namespace {

constexpr double kEpsilonMil = 1e-3;

bool nearlyEqual(double a, double b)
{
    return std::abs(a - b) <= kEpsilonMil;
}

bool finitePoint(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

void appendUnique(QVector<QPointF>& points, const QPointF& point)
{
    if (points.isEmpty()) {
        points.append(point);
        return;
    }

    const QPointF& last = points.last();
    if (!nearlyEqual(last.x(), point.x()) || !nearlyEqual(last.y(), point.y())) {
        points.append(point);
    }
}

double safeMid(double value, double a, double b)
{
    if (!std::isfinite(value)) {
        value = (a + b) / 2.0;
    }
    return std::clamp(value, std::min(a, b), std::max(a, b));
}

} // namespace

QVector<QPointF> orthogonalPath(QPointF start, QPointF end, OrthoMode mode,
                                std::optional<double> mid, RouteError* error)
{
    if (error) {
        *error = RouteError::None;
    }
    if (!finitePoint(start) || !finitePoint(end)) {
        if (error) {
            *error = RouteError::InvalidCoordinate;
        }
        return {};
    }

    QVector<QPointF> points;
    appendUnique(points, start);

    if (nearlyEqual(start.x(), end.x()) && nearlyEqual(start.y(), end.y())) {
        return points;
    }

    // 近似同 x/y 时吸附坐标，避免返回一条 0.0005 mil 的斜线。
    if (nearlyEqual(start.y(), end.y())) {
        end.setY(start.y());
        appendUnique(points, end);
        return points;
    }
    if (nearlyEqual(start.x(), end.x())) {
        end.setX(start.x());
        appendUnique(points, end);
        return points;
    }

    switch (mode) {
    case OrthoMode::HV:
        appendUnique(points, QPointF(end.x(), start.y()));
        break;
    case OrthoMode::VH:
        appendUnique(points, QPointF(start.x(), end.y()));
        break;
    case OrthoMode::Z_H: {
        const double midX = safeMid(mid.value_or((start.x() + end.x()) / 2.0),
                                    start.x(), end.x());
        appendUnique(points, QPointF(midX, start.y()));
        appendUnique(points, QPointF(midX, end.y()));
        break;
    }
    case OrthoMode::Z_V: {
        const double midY = safeMid(mid.value_or((start.y() + end.y()) / 2.0),
                                    start.y(), end.y());
        appendUnique(points, QPointF(start.x(), midY));
        appendUnique(points, QPointF(end.x(), midY));
        break;
    }
    default:
        if (error) {
            *error = RouteError::InvalidMode;
        }
        return {};
    }

    appendUnique(points, end);
    return points;
}

} // namespace eda