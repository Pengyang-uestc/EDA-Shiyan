#include "geometry.h"

#include <algorithm>
#include <cmath>

namespace eda {
namespace {
double cross(QPointF a, QPointF b, QPointF c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}

bool finite(QPointF point) {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool onSegment(QPointF point, QPointF a, QPointF b, double toleranceMil) {
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double length = std::hypot(dx, dy);
    if (length <= toleranceMil)
        return std::hypot(point.x() - a.x(), point.y() - a.y()) <= toleranceMil;
    if (std::abs(cross(a, b, point)) > toleranceMil * length)
        return false;
    return point.x() >= std::min(a.x(), b.x()) - toleranceMil &&
           point.x() <= std::max(a.x(), b.x()) + toleranceMil &&
           point.y() >= std::min(a.y(), b.y()) - toleranceMil &&
           point.y() <= std::max(a.y(), b.y()) + toleranceMil;
}

int orientation(QPointF a, QPointF b, QPointF c, double toleranceMil) {
    const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
    if (length <= toleranceMil)
        return 0;
    const double value = cross(a, b, c);
    if (value > toleranceMil * length)
        return 1;
    if (value < -toleranceMil * length)
        return -1;
    return 0;
}
} // namespace

double signedArea(const QPolygonF& polygon) {
    if (polygon.size() < 3)
        return 0.0;
    long double twiceArea = 0.0L;
    for (qsizetype i = 0; i < polygon.size(); ++i) {
        const QPointF a = polygon[i];
        const QPointF b = polygon[(i + 1) % polygon.size()];
        twiceArea += static_cast<long double>(a.x()) * b.y() -
                     static_cast<long double>(b.x()) * a.y();
    }
    return static_cast<double>(twiceArea / 2.0L);
}

double distancePointToSegment(QPointF point, QPointF a, QPointF b) {
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0)
        return std::hypot(point.x() - a.x(), point.y() - a.y());
    const double projection = std::clamp(((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) /
                                              lengthSquared,
                                          0.0, 1.0);
    return std::hypot(point.x() - (a.x() + projection * dx),
                      point.y() - (a.y() + projection * dy));
}

QPointF footprintToBoard(QPointF localMil, QPointF instancePos, double rotationDeg, BoardSide side) {
    constexpr double pi = 3.1415926535897932384626433832795;
    const double radians = rotationDeg * pi / 180.0;
    const double x = side == BoardSide::Bottom ? -localMil.x() : localMil.x();
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {instancePos.x() + x * cosine - localMil.y() * sine,
            instancePos.y() + x * sine + localMil.y() * cosine};
}

QPointF boardToFootprint(QPointF boardMil, QPointF instancePos, double rotationDeg, BoardSide side) {
    constexpr double pi = 3.1415926535897932384626433832795;
    const double radians = rotationDeg * pi / 180.0;
    const double x = boardMil.x() - instancePos.x();
    const double y = boardMil.y() - instancePos.y();
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const double unrotatedX = x * cosine + y * sine;
    const double localX = side == BoardSide::Bottom ? -unrotatedX : unrotatedX;
    return {localX, -x * sine + y * cosine};
}

LayerId mapFootprintLayerToBoard(LayerId footprintLayer, BoardSide side) noexcept {
    if (side != BoardSide::Bottom)
        return footprintLayer;
    switch (footprintLayer) {
    case kTopCopper:
        return kBottomCopper;
    case kBottomCopper:
        return kTopCopper;
    case kTopSilk:
        return kBottomSilk;
    case kBottomSilk:
        return kTopSilk;
    case kTopMask:
        return kBottomMask;
    case kBottomMask:
        return kTopMask;
    default:
        return footprintLayer;
    }
}

bool segmentsIntersect(QPointF a1, QPointF a2, QPointF b1, QPointF b2, double toleranceMil) {
    if (!finite(a1) || !finite(a2) || !finite(b1) || !finite(b2) ||
        !std::isfinite(toleranceMil) || toleranceMil < 0.0)
        return false;

    const int o1 = orientation(a1, a2, b1, toleranceMil);
    const int o2 = orientation(a1, a2, b2, toleranceMil);
    const int o3 = orientation(b1, b2, a1, toleranceMil);
    const int o4 = orientation(b1, b2, a2, toleranceMil);
    const bool properCrossing = ((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) &&
                                ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0));
    if (properCrossing)
        return true;
    return (o1 == 0 && onSegment(b1, a1, a2, toleranceMil)) ||
           (o2 == 0 && onSegment(b2, a1, a2, toleranceMil)) ||
           (o3 == 0 && onSegment(a1, b1, b2, toleranceMil)) ||
           (o4 == 0 && onSegment(a2, b1, b2, toleranceMil));
}

bool isSimplePolygon(const QPolygonF& polygon, double toleranceMil) {
    if (polygon.size() < 3 || !std::isfinite(toleranceMil) || toleranceMil < 0.0)
        return false;
    for (qsizetype i = 0; i < polygon.size(); ++i) {
        const QPointF current = polygon[i];
        const QPointF next = polygon[(i + 1) % polygon.size()];
        if (!finite(current) || distancePointToSegment(current, next, next) <= toleranceMil)
            return false;
    }

    // Adjacent collinear edges may continue straight, but may not double back over each other.
    for (qsizetype i = 0; i < polygon.size(); ++i) {
        const QPointF previous = polygon[(i + polygon.size() - 1) % polygon.size()];
        const QPointF current = polygon[i];
        const QPointF next = polygon[(i + 1) % polygon.size()];
        if (orientation(previous, current, next, toleranceMil) == 0) {
            const double toPreviousX = previous.x() - current.x();
            const double toPreviousY = previous.y() - current.y();
            const double toNextX = next.x() - current.x();
            const double toNextY = next.y() - current.y();
            if (toPreviousX * toNextX + toPreviousY * toNextY > toleranceMil * toleranceMil)
                return false;
        }
    }

    for (qsizetype i = 0; i < polygon.size(); ++i) {
        const QPointF a1 = polygon[i];
        const QPointF a2 = polygon[(i + 1) % polygon.size()];
        for (qsizetype j = i + 1; j < polygon.size(); ++j) {
            if (j == i || j == i + 1 || (i == 0 && j == polygon.size() - 1))
                continue;
            const QPointF b1 = polygon[j];
            const QPointF b2 = polygon[(j + 1) % polygon.size()];
            if (segmentsIntersect(a1, a2, b1, b2, toleranceMil))
                return false;
        }
    }
    return std::isfinite(signedArea(polygon)) && std::abs(signedArea(polygon)) > toleranceMil * toleranceMil;
}

bool containsPoint(const QPolygonF& polygon, QPointF point, double toleranceMil) {
    if (polygon.size() < 3 || !finite(point) || !std::isfinite(toleranceMil) || toleranceMil < 0.0)
        return false;
    bool inside = false;
    for (qsizetype i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const QPointF a = polygon[j];
        const QPointF b = polygon[i];
        if (!finite(a) || !finite(b))
            return false;
        if (onSegment(point, a, b, toleranceMil))
            return true;
        if ((a.y() > point.y()) != (b.y() > point.y())) {
            const double crossingX = a.x() + (point.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
            if (point.x() < crossingX)
                inside = !inside;
        }
    }
    return inside;
}

} // namespace eda
