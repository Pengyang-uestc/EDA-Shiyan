#include "feat_pcb/pad_geometry.h"

#include "core/geometry.h"
#include "core/primitive.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace eda {
namespace {

constexpr double kPi = 3.1415926535897932384626433832795;

QPolygonF roundedRect(const QSizeF& size, double radius, const QPointF& center,
                      int stepsPerQuarter = 8)
{
    const double halfWidth = size.width() / 2.0;
    const double halfHeight = size.height() / 2.0;
    const double maxRadius = std::min(halfWidth, halfHeight);
    radius = std::clamp(radius, 0.0, maxRadius);

    QPolygonF polygon;
    if (radius <= 0.0) {
        polygon << center + QPointF(-halfWidth, -halfHeight)
                << center + QPointF(halfWidth, -halfHeight)
                << center + QPointF(halfWidth, halfHeight)
                << center + QPointF(-halfWidth, halfHeight);
        return polygon;
    }

    stepsPerQuarter = std::max(stepsPerQuarter, 2);
    const QPointF corners[4] = {
        center + QPointF(halfWidth - radius, halfHeight - radius),
        center + QPointF(-halfWidth + radius, halfHeight - radius),
        center + QPointF(-halfWidth + radius, -halfHeight + radius),
        center + QPointF(halfWidth - radius, -halfHeight + radius),
    };
    const double startAngles[4] = {0.0, 90.0, 180.0, 270.0};

    for (int corner = 0; corner < 4; ++corner) {
        for (int step = 0; step < stepsPerQuarter; ++step) {
            const double degrees = startAngles[corner]
                                   + 90.0 * step / stepsPerQuarter;
            const double radians = degrees * kPi / 180.0;
            polygon << corners[corner] + QPointF(radius * std::cos(radians),
                                                 radius * std::sin(radians));
        }
    }
    return polygon;
}

double shapeRadius(const Pad& pad)
{
    return std::min(pad.size.width(), pad.size.height()) / 2.0;
}

} // namespace

QPolygonF padOutline(const Pad& pad, int circleSegments)
{
    switch (pad.shape) {
    case PadShape::Circle:
        circleSegments = std::max(circleSegments, 8);
        return roundedRect(pad.size, shapeRadius(pad), pad.pos,
                           std::max(2, circleSegments / 4));
    case PadShape::Rectangle:
        return roundedRect(pad.size, 0.0, pad.pos);
    case PadShape::Oblong:
        return roundedRect(pad.size, shapeRadius(pad), pad.pos);
    case PadShape::RoundRect:
        return roundedRect(pad.size, pad.cornerRadius, pad.pos);
    }
    return {};
}

QPolygonF padOutlineOnBoard(const Pad& pad, QPointF instancePos,
                            double rotationDeg, BoardSide side,
                            int circleSegments)
{
    const QPolygonF local = padOutline(pad, circleSegments);
    QPolygonF board;
    board.reserve(local.size());
    for (const QPointF& point : local) {
        board << footprintToBoard(point, instancePos, rotationDeg, side);
    }
    return board;
}

double distancePointToPad(QPointF point, const Pad& pad, QPointF instancePos,
                          double rotationDeg, BoardSide side)
{
    if (pad.shape == PadShape::Circle) {
        const QPointF center = footprintToBoard(pad.pos, instancePos,
                                                rotationDeg, side);
        const double radius = shapeRadius(pad);
        return std::max(0.0, std::hypot(point.x() - center.x(),
                                        point.y() - center.y())
                                 - radius);
    }
    return distancePointToPolygon(
        point, padOutlineOnBoard(pad, instancePos, rotationDeg, side));
}

double distanceSegmentToPad(QPointF a, QPointF b, const Pad& pad,
                            QPointF instancePos, double rotationDeg,
                            BoardSide side)
{
    if (pad.shape == PadShape::Circle) {
        const QPointF center = footprintToBoard(pad.pos, instancePos,
                                                rotationDeg, side);
        const double radius = shapeRadius(pad);
        return std::max(0.0, distancePointToSegment(center, a, b) - radius);
    }
    return distanceSegmentToPolygon(
        a, b, padOutlineOnBoard(pad, instancePos, rotationDeg, side));
}


double distancePolygonToPolygon(const QPolygonF& a, const QPolygonF& b)
{
    if (a.size() < 3 || b.size() < 3) {
        return std::numeric_limits<double>::infinity();
    }

    for (const QPointF& point : a) {
        if (containsPoint(b, point)) {
            return 0.0;
        }
    }
    for (const QPointF& point : b) {
        if (containsPoint(a, point)) {
            return 0.0;
        }
    }

    double minimum = std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i < a.size(); ++i) {
        minimum = std::min(
            minimum,
            distanceSegmentToPolygon(a[i], a[(i + 1) % a.size()], b));
    }
    for (qsizetype i = 0; i < b.size(); ++i) {
        minimum = std::min(
            minimum,
            distanceSegmentToPolygon(b[i], b[(i + 1) % b.size()], a));
    }
    return minimum;
}

double distancePadToPolygon(const Pad& pad, QPointF instancePos,
                            double rotationDeg, BoardSide side,
                            const QPolygonF& polygon)
{
    if (pad.shape == PadShape::Circle) {
        const QPointF center = footprintToBoard(pad.pos, instancePos,
                                                rotationDeg, side);
        return std::max(0.0, distancePointToPolygon(center, polygon)
                                 - shapeRadius(pad));
    }
    return distancePolygonToPolygon(
        padOutlineOnBoard(pad, instancePos, rotationDeg, side), polygon);
}

double distancePadToPad(const Pad& a, QPointF instancePosA, double rotationDegA,
                        BoardSide sideA, const Pad& b, QPointF instancePosB,
                        double rotationDegB, BoardSide sideB)
{
    const bool aCircle = a.shape == PadShape::Circle;
    const bool bCircle = b.shape == PadShape::Circle;

    if (aCircle && bCircle) {
        const QPointF centerA = footprintToBoard(a.pos, instancePosA,
                                                 rotationDegA, sideA);
        const QPointF centerB = footprintToBoard(b.pos, instancePosB,
                                                 rotationDegB, sideB);
        return std::max(0.0, std::hypot(centerA.x() - centerB.x(),
                                        centerA.y() - centerB.y())
                                 - shapeRadius(a) - shapeRadius(b));
    }

    const QPolygonF outlineB =
        padOutlineOnBoard(b, instancePosB, rotationDegB, sideB);
    if (aCircle) {
        const QPointF centerA = footprintToBoard(a.pos, instancePosA,
                                                 rotationDegA, sideA);
        return std::max(0.0, distancePointToPolygon(centerA, outlineB)
                                 - shapeRadius(a));
    }

    const QPolygonF outlineA =
        padOutlineOnBoard(a, instancePosA, rotationDegA, sideA);
    if (bCircle) {
        const QPointF centerB = footprintToBoard(b.pos, instancePosB,
                                                 rotationDegB, sideB);
        return std::max(0.0, distancePointToPolygon(centerB, outlineA)
                                 - shapeRadius(b));
    }
    return distancePolygonToPolygon(outlineA, outlineB);
}

QRectF footprintBounds(const Footprint& footprint)
{
    QRectF bounds;
    bool first = true;
    auto include = [&](const QRectF& rect) {
        if (!rect.isValid()) {
            return;
        }
        bounds = first ? rect : bounds.united(rect);
        first = false;
    };

    for (const Pad& pad : footprint.pads) {
        include(QRectF(pad.pos.x() - pad.size.width() / 2.0,
                       pad.pos.y() - pad.size.height() / 2.0,
                       pad.size.width(), pad.size.height()));
    }
    for (const MountingHole& hole : footprint.holes) {
        const double radius = hole.drill / 2.0 + hole.keepout;
        include(QRectF(hole.pos.x() - radius, hole.pos.y() - radius,
                       radius * 2.0, radius * 2.0));
    }

    for (const auto& primitive : footprint.silk) {
        if (!primitive) {
            continue;
        }
        switch (primitive->kind()) {
        case PrimitiveKind::Line: {
            const auto* line = dynamic_cast<const SilkLine*>(primitive.get());
            if (line) {
                include(QRectF(line->a, line->b).normalized());
            }
            break;
        }
        case PrimitiveKind::Arc: {
            const auto* arc = dynamic_cast<const SilkArc*>(primitive.get());
            if (arc) {
                include(QRectF(arc->center.x() - arc->radius,
                               arc->center.y() - arc->radius,
                               arc->radius * 2.0, arc->radius * 2.0));
            }
            break;
        }
        case PrimitiveKind::Rect: {
            const auto* rect = dynamic_cast<const SilkRect*>(primitive.get());
            if (rect) {
                include(rect->rect.normalized());
            }
            break;
        }
        case PrimitiveKind::Polygon: {
            const auto* polygon = dynamic_cast<const SilkPolygon*>(primitive.get());
            if (polygon) {
                include(polygon->points.boundingRect());
            }
            break;
        }
        case PrimitiveKind::Text: {
            const auto* text = dynamic_cast<const SilkText*>(primitive.get());
            if (text) {
                const double width = std::max(text->height,
                                              text->text.size() * text->height * 0.6);
                include(QRectF(text->pos.x(), text->pos.y() - text->height,
                               width, text->height * 1.4));
            }
            break;
        }
        }
    }
    return first ? QRectF(-50, -50, 100, 100) : bounds;
}

QRectF instanceBounds(const Instance& instance, const Footprint& footprint)
{
    const QRectF local = footprintBounds(footprint);
    const std::array<QPointF, 4> corners{
        local.topLeft(), local.topRight(),
        local.bottomRight(), local.bottomLeft()};
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool first = true;
    for (const QPointF& corner : corners) {
        const QPointF board = footprintToBoard(corner, instance.pos,
                                               instance.rotationDeg,
                                               instance.side);
        if (first) {
            minX = maxX = board.x();
            minY = maxY = board.y();
            first = false;
        } else {
            minX = std::min(minX, board.x());
            maxX = std::max(maxX, board.x());
            minY = std::min(minY, board.y());
            maxY = std::max(maxY, board.y());
        }
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

QVector<LayerId> padCopperLayers(const Pad& pad, BoardSide side,
                                 const QVector<LayerId>& copperLayers)
{
    QVector<LayerId> layers{mapFootprintLayerToBoard(pad.layer, side)};
    if (pad.drill > 0.0) {
        const QVector<LayerId> all = copperLayers.isEmpty()
                                         ? QVector<LayerId>{kTopCopper, kBottomCopper}
                                         : copperLayers;
        for (LayerId layer : all) {
            if (!layers.contains(layer)) {
                layers.append(layer);
            }
        }
    }
    return layers;
}

} // namespace eda