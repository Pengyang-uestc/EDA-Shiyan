#include "board.h"

#include "geometry.h"

#include <QHash>
#include <QSet>
#include <cmath>
#include <limits>

namespace eda {
namespace {
template <class Id, class Container> Id allocateId(Id& next, const Container& items) noexcept {
    if (next == kInvalidId)
        return kInvalidId;
    Id maximum = 0;
    for (const auto& item : items)
        maximum = qMax(maximum, item.id);
    if (maximum == std::numeric_limits<Id>::max()) {
        next = kInvalidId;
        return kInvalidId;
    }
    next = qMax(next, static_cast<Id>(maximum + 1));
    const Id allocated = next;
    if (allocated == std::numeric_limits<Id>::max())
        next = kInvalidId;
    else
        ++next;
    return allocated;
}

template <class Id, class Container> void observeId(Id& next, Id observed) noexcept {
    if (observed == std::numeric_limits<Id>::max()) {
        next = kInvalidId;
    } else if (next != kInvalidId && next <= observed) {
        next = static_cast<Id>(observed + 1);
    }
}

template <class Id, class Container, class Value> Status insertById(Container& items, Id& next, const Value& value) {
    if (value.id == kInvalidId)
        return Status::failure(QStringLiteral("对象 ID 必须为正整数"));
    for (const auto& item : items) {
        if (item.id == value.id)
            return Status::failure(QStringLiteral("对象 ID 重复"));
    }
    items.append(value);
    observeId<Id, Container>(next, value.id);
    return Status::success();
}

template <class Id, class Container> Status removeById(Container& items, Id id) {
    if (id == kInvalidId)
        return Status::failure(QStringLiteral("对象 ID 无效"));
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (items[i].id == id) {
            items.removeAt(i);
            return Status::success();
        }
    }
    return Status::failure(QStringLiteral("对象 ID 不存在"));
}

bool finite(QPointF point) {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool validLayerId(LayerId id) {
    return (id >= kTopCopper && id <= kBoardOutline) || id >= 100;
}

bool fixedLayerTypeMatches(const BoardLayer& layer) {
    switch (layer.id) {
    case kTopCopper:
    case kBottomCopper:
        return layer.type == BoardLayerType::Copper;
    case kTopSilk:
    case kBottomSilk:
        return layer.type == BoardLayerType::Silk;
    case kTopMask:
    case kBottomMask:
        return layer.type == BoardLayerType::Mask;
    case kBoardOutline:
        return layer.type == BoardLayerType::Outline;
    default:
        return layer.id >= 100;
    }
}

bool knownLayerType(BoardLayerType type) {
    switch (type) {
    case BoardLayerType::Copper:
    case BoardLayerType::Silk:
    case BoardLayerType::Mask:
    case BoardLayerType::Outline:
        return true;
    }
    return false;
}

bool knownSide(BoardSide side) {
    return side == BoardSide::Top || side == BoardSide::Bottom;
}

bool hasLayerType(const QHash<LayerId, BoardLayerType>& types, LayerId id, BoardLayerType type) {
    return types.contains(id) && types.value(id) == type;
}

bool hasNet(const QSet<QString>& names, const QString& name) {
    return name.isEmpty() || names.contains(name);
}

Status invalid(const QString& message) {
    return Status::failure(message);
}
} // namespace

TrackId Board::allocateTrackId() noexcept {
    return allocateId(nextTrackId, tracks);
}

ViaId Board::allocateViaId() noexcept {
    return allocateId(nextViaId, vias);
}

PourId Board::allocatePourId() noexcept {
    return allocateId(nextPourId, pours);
}

Status Board::insertTrack(const Track& track) {
    return insertById<TrackId>(tracks, nextTrackId, track);
}

Status Board::removeTrack(TrackId id) {
    return removeById(tracks, id);
}

Status Board::insertVia(const Via& via) {
    return insertById<ViaId>(vias, nextViaId, via);
}

Status Board::removeVia(ViaId id) {
    return removeById(vias, id);
}

Status Board::insertCopperPour(const CopperPour& pour) {
    return insertById<PourId>(pours, nextPourId, pour);
}

Status Board::removeCopperPour(PourId id) {
    return removeById(pours, id);
}

Status validateBoardStructure(const Board& board) {
    if (!isSimplePolygon(board.outline) || signedArea(board.outline) <= kEpsilonMil * kEpsilonMil)
        return invalid(QStringLiteral("板框必须是面积非零的逆时针简单多边形"));

    QSet<LayerGroupId> groupIds;
    for (const auto& group : board.groups) {
        if (groupIds.contains(group.id))
            return invalid(QStringLiteral("groupId 重复"));
        if (group.name.isEmpty())
            return invalid(QStringLiteral("层组名称不能为空"));
        groupIds.insert(group.id);
    }

    QHash<LayerId, BoardLayerType> layerTypes;
    for (const auto& layer : board.layers) {
        if (!validLayerId(layer.id))
            return invalid(QStringLiteral("layerId 必须为 0 至 6 或不小于 100"));
        if (layerTypes.contains(layer.id))
            return invalid(QStringLiteral("layerId 重复"));
        if (!groupIds.contains(layer.groupId))
            return invalid(QStringLiteral("layer 引用了不存在的 groupId"));
        if (layer.name.isEmpty() || !layer.color.isValid() || !knownLayerType(layer.type) ||
            !fixedLayerTypeMatches(layer))
            return invalid(QStringLiteral("layer 名称、类型或颜色非法"));
        layerTypes.insert(layer.id, layer.type);
    }

    QSet<QString> refdeses;
    for (const auto& instance : board.instances) {
        if (instance.refdes.isEmpty() || instance.footprintId.isEmpty() || refdeses.contains(instance.refdes))
            return invalid(QStringLiteral("refdes 为空或重复，或 footprintId 为空"));
        if (!finite(instance.pos) || !std::isfinite(instance.rotationDeg) || !knownSide(instance.side))
            return invalid(QStringLiteral("instance 坐标或旋转角非法"));
        refdeses.insert(instance.refdes);
    }

    QSet<TrackId> trackIds;
    for (const auto& track : board.tracks) {
        if (track.id == kInvalidId || trackIds.contains(track.id))
            return invalid(QStringLiteral("trackId 为零或重复"));
        if (!hasLayerType(layerTypes, track.layerId, BoardLayerType::Copper))
            return invalid(QStringLiteral("track 必须位于已定义的铜层"));
        if (!std::isfinite(track.width) || track.width <= 0.0 || track.points.size() < 2)
            return invalid(QStringLiteral("track 宽度必须为正且至少包含两个点"));
        for (qsizetype i = 0; i < track.points.size(); ++i) {
            if (!finite(track.points[i]))
                return invalid(QStringLiteral("track 坐标非法"));
            if (i > 0 && distancePointToSegment(track.points[i], track.points[i - 1], track.points[i - 1]) <=
                             kEpsilonMil)
                return invalid(QStringLiteral("track 相邻点不能重合"));
        }
        trackIds.insert(track.id);
    }

    QSet<ViaId> viaIds;
    for (const auto& via : board.vias) {
        if (via.id == kInvalidId || viaIds.contains(via.id))
            return invalid(QStringLiteral("viaId 为零或重复"));
        if (!finite(via.pos) || !std::isfinite(via.drill) || !std::isfinite(via.diameter) ||
            via.drill <= 0.0 || via.diameter <= 0.0)
            return invalid(QStringLiteral("via 坐标或尺寸非法"));
        if (via.fromLayer == via.toLayer || !hasLayerType(layerTypes, via.fromLayer, BoardLayerType::Copper) ||
            !hasLayerType(layerTypes, via.toLayer, BoardLayerType::Copper))
            return invalid(QStringLiteral("via 起止层必须是不同的已定义铜层"));
        viaIds.insert(via.id);
    }

    QSet<PourId> pourIds;
    for (const auto& pour : board.pours) {
        if (pour.id == kInvalidId || pourIds.contains(pour.id))
            return invalid(QStringLiteral("pourId 为零或重复"));
        if (!std::isfinite(pour.clearance) || pour.clearance < 0.0 ||
            !hasLayerType(layerTypes, pour.layerId, BoardLayerType::Copper))
            return invalid(QStringLiteral("pour clearance 或铜层非法"));
        if (!isSimplePolygon(pour.points))
            return invalid(QStringLiteral("pour 必须是面积非零的简单多边形"));
        pourIds.insert(pour.id);
    }

    QSet<QString> netNames;
    QSet<QString> assignedPins;
    for (const auto& net : board.nets) {
        if (net.name.isEmpty() || net.name == QStringLiteral("-") || netNames.contains(net.name))
            return invalid(QStringLiteral("net 名称为空、保留或重复"));
        netNames.insert(net.name);
        for (const auto& pin : net.pins) {
            if (pin.refdes.isEmpty() || !refdeses.contains(pin.refdes) || pin.pinNumber <= 0)
                return invalid(QStringLiteral("net 引用了不存在的 instance 或非法引脚编号"));
            const QString key = pin.refdes + QChar(0x1f) + QString::number(pin.pinNumber);
            if (assignedPins.contains(key))
                return invalid(QStringLiteral("同一引脚不能属于多个 net"));
            assignedPins.insert(key);
        }
    }

    for (const auto& track : board.tracks) {
        if (!hasNet(netNames, track.net))
            return invalid(QStringLiteral("track 引用了不存在的 net"));
    }
    for (const auto& via : board.vias) {
        if (!hasNet(netNames, via.net))
            return invalid(QStringLiteral("via 引用了不存在的 net"));
    }
    for (const auto& pour : board.pours) {
        if (!hasNet(netNames, pour.net))
            return invalid(QStringLiteral("pour 引用了不存在的 net"));
    }

    return Status::success();
}

} // namespace eda
