#include "feat_pcb/layout_service.h"

#include "core/geometry.h"
#include "feat_pcb/grid_layout.h"
#include "feat_pcb/pad_geometry.h"

#include <QHash>
#include <QRectF>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace eda {
namespace {

constexpr double kScratchExtentMil = 1000000.0;

bool rectInsideOutline(const QRectF& rect, const QPolygonF& outline)
{
    const std::array<QPointF, 4> corners{
        rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
    for (const QPointF& corner : corners) {
        if (!containsPoint(outline, corner)) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        for (qsizetype j = 0; j < outline.size(); ++j) {
            if (segmentsIntersect(corners[i], corners[(i + 1) % 4],
                                  outline[j], outline[(j + 1) % outline.size()],
                                  kEpsilonMil)) {
                return false;
            }
        }
    }
    return true;
}

InstanceLayoutResult failure(const QString& message)
{
    InstanceLayoutResult result;
    result.status = Status::failure(message);
    return result;
}

} // namespace

InstanceLayoutResult layoutInstances(const Board& board,
                                     const FootprintResolver& resolveFootprint,
                                     const InstanceLayoutOptions& options)
{
    if (!std::isfinite(options.origin.x()) || !std::isfinite(options.origin.y())
        || !std::isfinite(options.gapMil) || options.gapMil < 0.0
        || options.origin.x() < 0.0 || options.origin.y() < 0.0) {
        return failure(QStringLiteral("布局原点或间距非法"));
    }
    if (!resolveFootprint) {
        return failure(QStringLiteral("未提供封装查找器"));
    }
    if (!board.instances.isEmpty()
        && (board.outline.size() < 3 || !isSimplePolygon(board.outline))) {
        return failure(QStringLiteral("板框为空或不是简单多边形"));
    }

    QHash<QString, Result<Footprint>> resolvedCache;
    QVector<QSizeF> sizes;
    QVector<QPointF> centerOffsets;
    QVector<Footprint> footprints;
    sizes.reserve(board.instances.size());
    centerOffsets.reserve(board.instances.size());
    footprints.reserve(board.instances.size());

    for (const Instance& instance : board.instances) {
        const auto found = resolvedCache.constFind(instance.footprintId);
        Result<Footprint> resolved;
        if (found != resolvedCache.cend()) {
            resolved = *found;
        } else {
            resolved = resolveFootprint(instance.footprintId);
            resolvedCache.insert(instance.footprintId, resolved);
        }
        if (!resolved.status.ok || !resolved.value.has_value()) {
            return failure(QStringLiteral("实例 %1 的封装 %2 无法解析：%3")
                               .arg(instance.refdes, instance.footprintId,
                                    resolved.status.message));
        }
        Instance unplaced = instance;
        unplaced.pos = QPointF(0, 0);
        const QRectF bounds = instanceBounds(unplaced, *resolved.value);
        sizes.append(bounds.size());
        centerOffsets.append(bounds.center());
        footprints.append(*resolved.value);
    }

    // 用足够大的合成矩形拿到 gridLayout 的相对中心，再整体平移到 origin。
    QPolygonF scratch;
    scratch << QPointF(0, 0) << QPointF(kScratchExtentMil, 0)
            << QPointF(kScratchExtentMil, kScratchExtentMil)
            << QPointF(0, kScratchExtentMil);
    const LayoutResult grid = gridLayout(sizes, scratch, options.gapMil,
                                         options.columns);
    if (!grid.status.ok) {
        return failure(grid.status.message);
    }
    if (grid.positions.isEmpty()) {
        InstanceLayoutResult result;
        result.status = Status::success();
        return result;
    }

    double minimumLeft = std::numeric_limits<double>::infinity();
    double minimumTop = std::numeric_limits<double>::infinity();
    for (int i = 0; i < grid.positions.size(); ++i) {
        minimumLeft = std::min(minimumLeft,
                               grid.positions[i].x() - sizes[i].width() / 2.0);
        minimumTop = std::min(minimumTop,
                              grid.positions[i].y() - sizes[i].height() / 2.0);
    }
    const QPointF shift(options.origin.x() - minimumLeft,
                        options.origin.y() - minimumTop);

    InstanceLayoutResult result;
    result.positions.reserve(grid.positions.size());
    result.bounds.reserve(grid.positions.size());
    for (int i = 0; i < grid.positions.size(); ++i) {
        const QPointF center = grid.positions[i] + shift;
        const QPointF position = center - centerOffsets[i];
        result.positions.append(position);

        Instance placed = board.instances[i];
        placed.pos = position;
        const QRectF bounds = instanceBounds(placed, footprints[i]);
        result.bounds.append(bounds);

        if (!rectInsideOutline(bounds, board.outline)) {
            return failure(QStringLiteral("实例 %1 布局后超出板框")
                               .arg(board.instances[i].refdes));
        }
    }
    result.status = Status::success();
    return result;
}

} // namespace eda
