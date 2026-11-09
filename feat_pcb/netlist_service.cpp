#include "feat_pcb/netlist_service.h"

#include "core/geometry.h"
#include "core/primitive.h"

#include <QFile>
#include <QHash>
#include <QRectF>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <array>
#include <utility>

namespace eda {
namespace {

Result<NetlistImportResult> fail(const QString& message)
{
    return {Status::failure(message), std::nullopt};
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
                       pad.size.width(),
                       pad.size.height()));
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

    if (first) {
        return QRectF(-50, -50, 100, 100);
    }
    return bounds;
}

void addLayer(Board& board, LayerId id, LayerGroupId groupId,
              const QString& name, BoardLayerType type,
              const QString& color, int stackOrder)
{
    BoardLayer layer;
    layer.id = id;
    layer.groupId = groupId;
    layer.name = name;
    layer.type = type;
    layer.color = QColor(color);
    layer.stackOrder = stackOrder;
    board.layers.append(layer);
}

Board makeBaseBoard(const QSizeF& size)
{
    Board board;
    board.outline << QPointF(0, 0)
                  << QPointF(size.width(), 0)
                  << QPointF(size.width(), size.height())
                  << QPointF(0, size.height());

    board.groups.append(LayerGroup{1, QStringLiteral("Top"), 0});
    board.groups.append(LayerGroup{2, QStringLiteral("Bottom"), 1});
    board.groups.append(LayerGroup{3, QStringLiteral("Mechanical"), 2});

    addLayer(board, kTopCopper, 1, QStringLiteral("Top Copper"),
             BoardLayerType::Copper, QStringLiteral("#C83232"), 0);
    addLayer(board, kBottomCopper, 2, QStringLiteral("Bottom Copper"),
             BoardLayerType::Copper, QStringLiteral("#3264C8"), 0);
    addLayer(board, kTopSilk, 1, QStringLiteral("Top Silk"),
             BoardLayerType::Silk, QStringLiteral("#F0F0F0"), 1);
    addLayer(board, kBottomSilk, 2, QStringLiteral("Bottom Silk"),
             BoardLayerType::Silk, QStringLiteral("#D0D0D0"), 1);
    addLayer(board, kTopMask, 1, QStringLiteral("Top Mask"),
             BoardLayerType::Mask, QStringLiteral("#C8A000"), 2);
    addLayer(board, kBottomMask, 2, QStringLiteral("Bottom Mask"),
             BoardLayerType::Mask, QStringLiteral("#A08000"), 2);
    addLayer(board, kBoardOutline, 3, QStringLiteral("Board Outline"),
             BoardLayerType::Outline, QStringLiteral("#FFFF00"), 0);
    return board;
}

} // namespace

Result<NetlistImportResult> NetlistService::importData(
    const QByteArray& data, const FootprintResolver& resolveFootprint,
    const NetlistImportOptions& options) const
{
    const Result<Netlist> decoded = decodeNetlist(data);
    if (!decoded.status.ok || !decoded.value.has_value()) {
        return fail(decoded.status.message);
    }
    if (!std::isfinite(options.origin.x()) || !std::isfinite(options.origin.y())
        || !std::isfinite(options.gapMil) || options.gapMil < 0.0
        || options.origin.x() < 0.0 || options.origin.y() < 0.0) {
        return fail(QStringLiteral("布局原点或间距非法"));
    }
    if (decoded.value->components.isEmpty()) {
        return fail(QStringLiteral("网表没有元件"));
    }
    if (!resolveFootprint) {
        return fail(QStringLiteral("未提供封装查找器"));
    }

    struct Component {
        QString refdes;
        QString footprintId;
        QRectF bounds;
    };

    QHash<QString, Result<Footprint>> resolvedCache;
    FootprintResolver cachedResolver =
        [&](const QString& footprintId) -> Result<Footprint> {
        const auto found = resolvedCache.constFind(footprintId);
        if (found != resolvedCache.cend()) {
            return *found;
        }
        Result<Footprint> resolved = resolveFootprint(footprintId);
        resolvedCache.insert(footprintId, resolved);
        return resolved;
    };

    QVector<Component> components;
    for (const NetlistComponent& item : decoded.value->components) {
        const Result<Footprint> resolved = cachedResolver(item.footprintId);
        if (!resolved.status.ok || !resolved.value.has_value()) {
            return fail(QStringLiteral("元件 %1 的封装 %2 无法解析：%3")
                            .arg(item.refdes, item.footprintId,
                                 resolved.status.message));
        }
        if (resolved.value->id != item.footprintId) {
            return fail(QStringLiteral("元件 %1 的封装查找器返回了错误的 ID：%2")
                            .arg(item.refdes, resolved.value->id));
        }

        Component component;
        component.refdes = item.refdes;
        component.footprintId = item.footprintId;
        component.bounds = footprintBounds(*resolved.value);
        components.append(std::move(component));
    }

    double cellWidth = 0.0;
    double cellHeight = 0.0;
    for (const Component& component : components) {
        cellWidth = std::max(cellWidth, component.bounds.width());
        cellHeight = std::max(cellHeight, component.bounds.height());
    }
    cellWidth += options.gapMil;
    cellHeight += options.gapMil;

    int columns = options.columns;
    if (columns <= 0) {
        columns = components.isEmpty()
                      ? 1
                      : static_cast<int>(std::ceil(std::sqrt(components.size())));
    }
    columns = std::clamp(columns, 1, std::max(1, static_cast<int>(components.size())));
    const int rows = components.isEmpty()
                         ? 0
                         : (components.size() + columns - 1) / columns;

    const double boardWidth = std::max(
        2000.0, options.origin.x() + columns * cellWidth + 200.0);
    const double boardHeight = std::max(
        1000.0, options.origin.y() + rows * cellHeight + 200.0);
    if (!std::isfinite(boardWidth) || !std::isfinite(boardHeight)) {
        return fail(QStringLiteral("网表布局尺寸非法"));
    }
    Board board = makeBaseBoard(QSizeF(boardWidth, boardHeight));

    for (int index = 0; index < components.size(); ++index) {
        const Component& component = components[index];
        const int row = index / columns;
        const int column = index % columns;
        const QPointF cellCenter(options.origin.x() + (column + 0.5) * cellWidth,
                                 options.origin.y() + (row + 0.5) * cellHeight);

        Instance instance;
        instance.refdes = component.refdes;
        instance.footprintId = component.footprintId;
        instance.pos = cellCenter - component.bounds.center();
        instance.rotationDeg = 0.0;
        instance.side = BoardSide::Top;
        board.instances.append(instance);

        const QRectF placed = component.bounds.translated(instance.pos);
        const std::array<QPointF, 4> corners{
            placed.topLeft(), placed.topRight(),
            placed.bottomRight(), placed.bottomLeft()};
        for (const QPointF& corner : corners) {
            if (!containsPoint(board.outline, corner)) {
                return fail(QStringLiteral("元件 %1 的包围盒超出板框")
                                .arg(component.refdes));
            }
        }
    }

    board.nets = decoded.value->nets;

    NetlistImportResult result;
    result.board = std::move(board);

    QSet<QString> connectedRefdes;
    for (const Net& net : result.board.nets) {
        if (net.pins.size() < 2) {
            result.warnings.append(QStringLiteral("网络 %1 少于两个引脚")
                                       .arg(net.name));
        }
        for (const PinRef& pin : net.pins) {
            connectedRefdes.insert(pin.refdes);
        }
    }
    for (const Component& component : components) {
        if (!connectedRefdes.contains(component.refdes)) {
            result.warnings.append(QStringLiteral("元件 %1 没有被任何网络引用")
                                       .arg(component.refdes));
        }
    }

    const Status validation = validateBoard(result.board, cachedResolver);
    if (!validation.ok) {
        return fail(validation.message);
    }

    return {Status::success(), std::move(result)};
}

Result<NetlistImportResult> NetlistService::importFile(
    const QString& path, const FootprintResolver& resolveFootprint,
    const NetlistImportOptions& options) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("无法读取网表文件：%1").arg(path));
    }
    constexpr qint64 kMaxNetlistBytes = 16 * 1024 * 1024;
    if (file.size() > kMaxNetlistBytes) {
        return fail(QStringLiteral("网表文件超过 16 MiB"));
    }
    return importData(file.readAll(), resolveFootprint, options);
}

} // namespace eda