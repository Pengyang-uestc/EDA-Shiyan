#pragma once

#include "board.h"

namespace eda {

// All coordinates and tolerances are in mil. Polygon input does not repeat its first point.
double signedArea(const QPolygonF& polygon);
// Requires non-zero area, no repeated adjacent vertices, and no self-touching/intersection.
bool isSimplePolygon(const QPolygonF& polygon, double toleranceMil = kEpsilonMil);
// A point on an edge, or within toleranceMil of it, counts as inside.
bool containsPoint(const QPolygonF& polygon, QPointF point, double toleranceMil = kEpsilonMil);
// Endpoint touching and collinear overlap count as an intersection.
bool segmentsIntersect(QPointF a1, QPointF a2, QPointF b1, QPointF b2,
                       double toleranceMil = kEpsilonMil);
double distancePointToSegment(QPointF point, QPointF a, QPointF b);

// D4.6 footprint-local to board-world transforms. Both use mil and counter-clockwise degrees.
QPointF footprintToBoard(QPointF localMil, QPointF instancePos, double rotationDeg, BoardSide side);
QPointF boardToFootprint(QPointF boardMil, QPointF instancePos, double rotationDeg, BoardSide side);
LayerId mapFootprintLayerToBoard(LayerId footprintLayer, BoardSide side) noexcept;

} // namespace eda
