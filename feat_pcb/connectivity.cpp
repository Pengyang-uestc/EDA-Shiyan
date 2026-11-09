#include "feat_pcb/connectivity.h"

#include "core/footprint.h"
#include "core/geometry.h"
#include "feat_pcb/pad_geometry.h"
#include "feat_pcb/union_find.h"

#include <QHash>
#include <QRectF>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>

namespace eda {
namespace {

constexpr double kContactToleranceMil = 0.01;

enum class NodeKind { Pin, Track, Via, Pour };

struct PinInfo {
    QString key;
    QString refdes;
    int number = 0;
    QPointF pos;
    Pad pad;
    QVector<LayerId> layers;
};

struct Node {
    NodeKind kind = NodeKind::Pin;
    QString net;
    TrackId trackId = kInvalidId;
    ViaId viaId = kInvalidId;
    PourId pourId = kInvalidId;
};

bool layersContain(const QVector<LayerId>& layers, LayerId layer)
{
    return layers.contains(layer);
}

bool viaTouchesLayer(const Via& via, LayerId layer)
{
    if (via.fromLayer == layer || via.toLayer == layer) {
        return true;
    }
    return (via.fromLayer == kTopCopper && via.toLayer == kBottomCopper)
           || (via.fromLayer == kBottomCopper && via.toLayer == kTopCopper);
}

double padRadius(const Pad& pad)
{
    return std::max(pad.size.width(), pad.size.height()) / 2.0;
}

double distancePointToRect(QPointF point, const QRectF& rect)
{
    const double dx = std::max({rect.left() - point.x(), 0.0,
                                point.x() - rect.right()});
    const double dy = std::max({rect.top() - point.y(), 0.0,
                                point.y() - rect.bottom()});
    return std::hypot(dx, dy);
}

QRectF padRect(const PinInfo& pin)
{
    return QRectF(pin.pos.x() - pin.pad.size.width() / 2.0,
                  pin.pos.y() - pin.pad.size.height() / 2.0,
                  pin.pad.size.width(),
                  pin.pad.size.height());
}

double segmentDistance(QPointF a1, QPointF a2, QPointF b1, QPointF b2)
{
    if (segmentsIntersect(a1, a2, b1, b2, kContactToleranceMil)) {
        return 0.0;
    }
    return std::min({distancePointToSegment(a1, b1, b2),
                     distancePointToSegment(a2, b1, b2),
                     distancePointToSegment(b1, a1, a2),
                     distancePointToSegment(b2, a1, a2)});
}

bool padTouchesSegment(const PinInfo& pin, QPointF a, QPointF b,
                       double marginMil = 0.0)
{
    if (pin.pad.shape == PadShape::Circle) {
        return distancePointToSegment(pin.pos, a, b)
               <= padRadius(pin.pad) + marginMil + kContactToleranceMil;
    }

    const QRectF rect = padRect(pin).adjusted(-marginMil, -marginMil,
                                              marginMil, marginMil);
    if (rect.contains(a) || rect.contains(b)) {
        return true;
    }

    QPolygonF edges;
    edges << rect.topLeft() << rect.topRight()
          << rect.bottomRight() << rect.bottomLeft();
    for (qsizetype i = 0; i < edges.size(); ++i) {
        if (segmentsIntersect(a, b, edges[i], edges[(i + 1) % edges.size()],
                              kContactToleranceMil)) {
            return true;
        }
    }
    return false;
}

bool padTouchesPoint(const PinInfo& pin, QPointF point, double marginMil = 0.0)
{
    return padTouchesSegment(pin, point, point, marginMil);
}

bool padsTouch(const PinInfo& a, const PinInfo& b)
{
    const bool aCircle = a.pad.shape == PadShape::Circle;
    const bool bCircle = b.pad.shape == PadShape::Circle;
    const double tolerance = kContactToleranceMil;

    if (aCircle && bCircle) {
        return std::hypot(a.pos.x() - b.pos.x(), a.pos.y() - b.pos.y())
               <= padRadius(a.pad) + padRadius(b.pad) + tolerance;
    }
    if (aCircle) {
        return distancePointToRect(a.pos, padRect(b))
               <= padRadius(a.pad) + tolerance;
    }
    if (bCircle) {
        return distancePointToRect(b.pos, padRect(a))
               <= padRadius(b.pad) + tolerance;
    }
    return padRect(a).adjusted(-tolerance, -tolerance, tolerance, tolerance)
        .intersects(padRect(b).adjusted(-tolerance, -tolerance,
                                        tolerance, tolerance));
}

bool padTouchesPour(const PinInfo& pin, const QPolygonF& pour)
{
    if (containsPoint(pour, pin.pos)) {
        return true;
    }
    const double margin = padRadius(pin.pad) + kContactToleranceMil;
    for (qsizetype i = 0; i < pour.size(); ++i) {
        if (distancePointToSegment(pin.pos, pour[i],
                                   pour[(i + 1) % pour.size()])
            <= margin) {
            return true;
        }
    }
    return false;
}

bool segmentTouchesPour(const QPolygonF& pour, QPointF a, QPointF b,
                        double marginMil)
{
    if (containsPoint(pour, a) || containsPoint(pour, b)
        || containsPoint(pour, (a + b) / 2.0)) {
        return true;
    }
    for (qsizetype i = 0; i < pour.size(); ++i) {
        if (segmentDistance(a, b, pour[i], pour[(i + 1) % pour.size()])
            <= marginMil + kContactToleranceMil) {
            return true;
        }
    }
    return false;
}

bool viaTouchesPour(const Via& via, const QPolygonF& pour)
{
    if (containsPoint(pour, via.pos)) {
        return true;
    }
    const double margin = via.diameter / 2.0 + kContactToleranceMil;
    for (qsizetype i = 0; i < pour.size(); ++i) {
        if (distancePointToSegment(via.pos, pour[i],
                                   pour[(i + 1) % pour.size()])
            <= margin) {
            return true;
        }
    }
    return false;
}

bool polygonsTouch(const QPolygonF& a, const QPolygonF& b)
{
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }
    if (containsPoint(a, b[0]) || containsPoint(b, a[0])) {
        return true;
    }
    for (qsizetype i = 0; i < a.size(); ++i) {
        for (qsizetype j = 0; j < b.size(); ++j) {
            if (segmentsIntersect(a[i], a[(i + 1) % a.size()],
                                  b[j], b[(j + 1) % b.size()],
                                  kContactToleranceMil)) {
                return true;
            }
        }
    }
    return false;
}


QString pinKey(const QString& refdes, int number)
{
    return refdes + QChar(0x1f) + QString::number(number);
}

} // namespace

bool ConnectivityResult::allNetsConnected() const
{
    if (!shortedNets.isEmpty()) {
        return false;
    }
    for (const NetConnectivity& net : nets) {
        if (!net.connected) {
            return false;
        }
    }
    return true;
}

ConnectivityResult analyzeConnectivity(const Board& board,
                                       const FootprintResolver& resolveFootprint)
{
    ConnectivityResult result;

    QHash<QString, QString> pinNet;
    for (const Net& net : board.nets) {
        for (const PinRef& pin : net.pins) {
            pinNet.insert(pinKey(pin.refdes, pin.pinNumber), net.name);
        }
    }

    QVector<LayerId> copperLayers;
    for (const BoardLayer& layer : board.layers) {
        if (layer.type == BoardLayerType::Copper) {
            copperLayers.append(layer.id);
        }
    }

    QVector<PinInfo> pins;
    QVector<int> pinNodeIndices;
    QVector<Node> nodes;
    QHash<QString, Footprint> footprintCache;

    for (const Instance& instance : board.instances) {
        Footprint footprint;
        const auto cached = footprintCache.constFind(instance.footprintId);
        if (cached != footprintCache.cend()) {
            footprint = *cached;
        } else {
            if (!resolveFootprint) {
                result.errors.append(QStringLiteral("未提供封装查找器：%1")
                                         .arg(instance.footprintId));
                continue;
            }
            const Result<Footprint> resolved = resolveFootprint(instance.footprintId);
            if (!resolved.status.ok || !resolved.value.has_value()) {
                result.errors.append(QStringLiteral("实例 %1 的封装 %2 无法解析：%3")
                                         .arg(instance.refdes, instance.footprintId,
                                              resolved.status.message));
                continue;
            }
            footprint = *resolved.value;
            footprintCache.insert(instance.footprintId, footprint);
        }

        for (const Pin& footprintPin : footprint.pins) {
            const Pad* pad = footprint.findPad(footprintPin.padId);
            if (!pad) {
                result.errors.append(QStringLiteral("实例 %1 的引脚 %2 找不到焊盘")
                                         .arg(instance.refdes)
                                         .arg(footprintPin.number));
                continue;
            }

            PinInfo info;
            info.key = pinKey(instance.refdes, footprintPin.number);
            info.refdes = instance.refdes;
            info.number = footprintPin.number;
            info.pos = footprintToBoard(pad->pos, instance.pos,
                                        instance.rotationDeg, instance.side);
            info.pad = *pad;
            info.layers = padCopperLayers(*pad, instance.side, copperLayers);
            pins.append(info);

            Node node;
            node.kind = NodeKind::Pin;
            node.net = pinNet.value(info.key);
            pinNodeIndices.append(nodes.size());
            nodes.append(node);
        }
    }

    QHash<TrackId, int> trackNodes;
    for (const Track& track : board.tracks) {
        Node node;
        node.kind = NodeKind::Track;
        node.net = track.net;
        node.trackId = track.id;
        trackNodes.insert(track.id, nodes.size());
        nodes.append(node);
    }

    QHash<ViaId, int> viaNodes;
    for (const Via& via : board.vias) {
        Node node;
        node.kind = NodeKind::Via;
        node.net = via.net;
        node.viaId = via.id;
        viaNodes.insert(via.id, nodes.size());
        nodes.append(node);
    }

    QHash<PourId, int> pourNodes;
    for (const CopperPour& pour : board.pours) {
        Node node;
        node.kind = NodeKind::Pour;
        node.net = pour.net;
        node.pourId = pour.id;
        pourNodes.insert(pour.id, nodes.size());
        nodes.append(node);
    }

    if (trackNodes.size() != board.tracks.size()
        || viaNodes.size() != board.vias.size()
        || pourNodes.size() != board.pours.size()) {
        result.errors.append(QStringLiteral("Board 中存在重复的对象 ID，无法建立连通图"));
        return result;
    }

    UnionFind dsu(nodes.size());

    for (qsizetype pinIndex = 0; pinIndex < pins.size(); ++pinIndex) {
        const PinInfo& pin = pins[pinIndex];

        for (const Track& track : board.tracks) {
            if (!layersContain(pin.layers, track.layerId)) {
                continue;
            }
            bool touches = false;
            for (qsizetype i = 0; i + 1 < track.points.size(); ++i) {
                if (padTouchesSegment(pin, track.points[i], track.points[i + 1],
                                      track.width / 2.0)) {
                    touches = true;
                    break;
                }
            }
            if (touches) {
                (void)dsu.unite(pinNodeIndices[pinIndex], trackNodes.value(track.id));
            }
        }

        for (const Via& via : board.vias) {
            bool layerMatch = false;
            for (LayerId layer : pin.layers) {
                if (viaTouchesLayer(via, layer)) {
                    layerMatch = true;
                    break;
                }
            }
            if (layerMatch && padTouchesPoint(pin, via.pos, via.diameter / 2.0)) {
                (void)dsu.unite(pinNodeIndices[pinIndex], viaNodes.value(via.id));
            }
        }

        for (const CopperPour& pour : board.pours) {
            if (layersContain(pin.layers, pour.layerId)
                && padTouchesPour(pin, pour.points)) {
                (void)dsu.unite(pinNodeIndices[pinIndex], pourNodes.value(pour.id));
            }
        }

        for (qsizetype otherIndex = pinIndex + 1; otherIndex < pins.size(); ++otherIndex) {
            const PinInfo& other = pins[otherIndex];
            bool layerMatch = false;
            for (LayerId layer : pin.layers) {
                if (layersContain(other.layers, layer)) {
                    layerMatch = true;
                    break;
                }
            }
            if (layerMatch && padsTouch(pin, other)) {
                (void)dsu.unite(pinNodeIndices[pinIndex],
                                pinNodeIndices[otherIndex]);
            }
        }
    }

    for (qsizetype i = 0; i < board.tracks.size(); ++i) {
        const Track& a = board.tracks[i];
        for (qsizetype j = i + 1; j < board.tracks.size(); ++j) {
            const Track& b = board.tracks[j];
            if (a.layerId != b.layerId) {
                continue;
            }
            bool touches = false;
            for (qsizetype ai = 0; ai + 1 < a.points.size() && !touches; ++ai) {
                for (qsizetype bi = 0; bi + 1 < b.points.size(); ++bi) {
                    if (segmentDistance(a.points[ai], a.points[ai + 1],
                                        b.points[bi], b.points[bi + 1])
                        <= (a.width + b.width) / 2.0 + kContactToleranceMil) {
                        touches = true;
                        break;
                    }
                }
            }
            if (touches) {
                (void)dsu.unite(trackNodes.value(a.id), trackNodes.value(b.id));
            }
        }
    }

    for (const Track& track : board.tracks) {
        for (const Via& via : board.vias) {
            if (!viaTouchesLayer(via, track.layerId)) {
                continue;
            }
            bool touches = false;
            for (qsizetype i = 0; i + 1 < track.points.size(); ++i) {
                if (distancePointToSegment(via.pos, track.points[i],
                                           track.points[i + 1])
                    <= track.width / 2.0 + via.diameter / 2.0
                           + kContactToleranceMil) {
                    touches = true;
                    break;
                }
            }
            if (touches) {
                (void)dsu.unite(trackNodes.value(track.id), viaNodes.value(via.id));
            }
        }

        for (const CopperPour& pour : board.pours) {
            if (track.layerId != pour.layerId) {
                continue;
            }
            bool touches = false;
            for (qsizetype i = 0; i + 1 < track.points.size(); ++i) {
                if (segmentTouchesPour(pour.points, track.points[i],
                                       track.points[i + 1], track.width / 2.0)) {
                    touches = true;
                    break;
                }
            }
            if (touches) {
                (void)dsu.unite(trackNodes.value(track.id), pourNodes.value(pour.id));
            }
        }
    }

    for (const Via& via : board.vias) {
        for (const CopperPour& pour : board.pours) {
            if ((via.fromLayer == pour.layerId || via.toLayer == pour.layerId)
                && viaTouchesPour(via, pour.points)) {
                (void)dsu.unite(viaNodes.value(via.id), pourNodes.value(pour.id));
            }
        }
    }

    for (qsizetype i = 0; i < board.pours.size(); ++i) {
        for (qsizetype j = i + 1; j < board.pours.size(); ++j) {
            const CopperPour& a = board.pours[i];
            const CopperPour& b = board.pours[j];
            if (a.layerId == b.layerId && polygonsTouch(a.points, b.points)) {
                (void)dsu.unite(pourNodes.value(a.id), pourNodes.value(b.id));
            }
        }
    }

    QHash<int, QSet<QString>> componentNets;
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        if (!nodes[i].net.isEmpty()) {
            componentNets[dsu.find(int(i))].insert(nodes[i].net);
        }
    }
    QSet<QString> shorted;
    for (auto it = componentNets.cbegin(); it != componentNets.cend(); ++it) {
        if (it.value().size() > 1) {
            for (const QString& name : it.value()) {
                shorted.insert(name);
            }
        }
    }
    result.shortedNets = shorted.values();
    std::sort(result.shortedNets.begin(), result.shortedNets.end());

    QHash<QString, QVector<int>> netPins;
    for (qsizetype i = 0; i < pins.size(); ++i) {
        const QString net = pinNet.value(pins[i].key);
        if (!net.isEmpty()) {
            netPins[net].append(int(i));
        }
    }

    for (const Net& net : board.nets) {
        NetConnectivity status;
        status.name = net.name;
        const QVector<int> pinIndices = netPins.value(net.name);
        QSet<int> roots;
        for (int pinIndex : pinIndices) {
            roots.insert(dsu.find(pinNodeIndices[pinIndex]));
        }
        status.componentCount = roots.size();
        status.connected = pinIndices.size() == net.pins.size()
                           && status.componentCount == 1;
        result.nets.append(status);
    }

    return result;
}

} // namespace eda