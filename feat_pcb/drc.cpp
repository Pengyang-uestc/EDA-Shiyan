#include "feat_pcb/drc.h"

#include "core/geometry.h"
#include "feat_pcb/pad_geometry.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <limits>

namespace eda {
namespace {

struct PadInstance {
    QString refdes;
    Pad pad;
    QPointF instancePos;
    double rotationDeg = 0.0;
    BoardSide side = BoardSide::Top;
    QVector<LayerId> layers;
    QString net;
};

QString pinKey(const QString& refdes, int number)
{
    return refdes + QChar(0x1f) + QString::number(number);
}

bool sameAssignedNet(const QString& a, const QString& b)
{
    return !a.isEmpty() && a == b;
}

bool viaTouchesCopperLayer(const Via& via, LayerId layer)
{
    if (via.fromLayer == layer || via.toLayer == layer) {
        return true;
    }
    const bool throughTopBottom =
        (via.fromLayer == kTopCopper && via.toLayer == kBottomCopper)
        || (via.fromLayer == kBottomCopper && via.toLayer == kTopCopper);
    return throughTopBottom;
}

bool layersShare(const QVector<LayerId>& a, const QVector<LayerId>& b)
{
    for (LayerId layer : a) {
        if (b.contains(layer)) {
            return true;
        }
    }
    return false;
}

DrcViolation makeViolation(const QString& code, const QString& message,
                           double actual, double required)
{
    DrcViolation violation;
    violation.code = code;
    violation.message = message;
    violation.actualMil = actual;
    violation.requiredMil = required;
    return violation;
}

} // namespace

DrcResult runDrc(const Board& board, const FootprintResolver& resolveFootprint,
                 const DrcRules& rules)
{
    DrcResult result;

    if (!resolveFootprint && !board.instances.isEmpty()) {
        result.errors.append(QStringLiteral("未提供封装查找器"));
    }

    QVector<LayerId> copperLayers;
    for (const BoardLayer& layer : board.layers) {
        if (layer.type == BoardLayerType::Copper) {
            copperLayers.append(layer.id);
        }
    }

    QHash<QString, QString> pinNet;
    for (const Net& net : board.nets) {
        for (const PinRef& pin : net.pins) {
            pinNet.insert(pinKey(pin.refdes, pin.pinNumber), net.name);
        }
    }

    QVector<PadInstance> pads;
    QHash<QString, Footprint> footprintCache;
    for (const Instance& instance : board.instances) {
        Footprint footprint;
        const auto cached = footprintCache.constFind(instance.footprintId);
        if (cached != footprintCache.cend()) {
            footprint = *cached;
        } else if (resolveFootprint) {
            const Result<Footprint> resolved = resolveFootprint(instance.footprintId);
            if (!resolved.status.ok || !resolved.value.has_value()) {
                result.errors.append(QStringLiteral("实例 %1 的封装 %2 无法解析：%3")
                                         .arg(instance.refdes, instance.footprintId,
                                              resolved.status.message));
                continue;
            }
            footprint = *resolved.value;
            footprintCache.insert(instance.footprintId, footprint);
        } else {
            continue;
        }

        QHash<PadId, QString> padNets;
        for (const Pin& pin : footprint.pins) {
            const QString net = pinNet.value(pinKey(instance.refdes, pin.number));
            if (!net.isEmpty()) {
                padNets.insert(pin.padId, net);
            }
        }

        for (const Pad& pad : footprint.pads) {
            PadInstance info;
            info.refdes = instance.refdes;
            info.pad = pad;
            info.instancePos = instance.pos;
            info.rotationDeg = instance.rotationDeg;
            info.side = instance.side;
            info.layers = padCopperLayers(pad, instance.side, copperLayers);
            info.net = padNets.value(pad.id);
            pads.append(info);
        }
    }

    if (rules.checkPadTrack) {
        for (const PadInstance& pad : pads) {
            for (const Track& track : board.tracks) {
                bool layerMatch = false;
                for (LayerId layer : pad.layers) {
                    if (layer == track.layerId) {
                        layerMatch = true;
                        break;
                    }
                }
                if (!layerMatch || sameAssignedNet(pad.net, track.net)) {
                    continue;
                }

                double minimum = std::numeric_limits<double>::infinity();
                for (qsizetype i = 0; i + 1 < track.points.size(); ++i) {
                    const double copperEdge = std::max(
                        0.0,
                        distanceSegmentToPad(track.points[i], track.points[i + 1],
                                             pad.pad, pad.instancePos,
                                             pad.rotationDeg, pad.side)
                            - track.width / 2.0);
                    minimum = std::min(minimum, copperEdge);
                }
                if (!std::isfinite(minimum)) {
                    continue;
                }

                const double required = std::max(rules.minClearanceMil,
                                                 pad.pad.clearance);
                if (minimum + kEpsilonMil < required) {
                    DrcViolation violation = makeViolation(
                        drc_code::kPadTrackClearance,
                        QStringLiteral("实例 %1 的焊盘 %2 与走线 %3 间距 %4 mil，小于 %5 mil")
                            .arg(pad.refdes)
                            .arg(pad.pad.number)
                            .arg(track.id)
                            .arg(minimum, 0, 'f', 3)
                            .arg(required, 0, 'f', 3),
                        minimum, required);
                    violation.refdes = pad.refdes;
                    violation.netA = pad.net;
                    violation.netB = track.net;
                    violation.padId = pad.pad.id;
                    violation.trackIdA = track.id;
                    result.violations.append(violation);
                }
            }
        }
    }

    if (rules.checkTrackTrack) {
        for (qsizetype i = 0; i < board.tracks.size(); ++i) {
            const Track& a = board.tracks[i];
            for (qsizetype j = i + 1; j < board.tracks.size(); ++j) {
                const Track& b = board.tracks[j];
                if (a.layerId != b.layerId
                    || sameAssignedNet(a.net, b.net)) {
                    continue;
                }

                double minimum = std::numeric_limits<double>::infinity();
                for (qsizetype ai = 0; ai + 1 < a.points.size(); ++ai) {
                    for (qsizetype bi = 0; bi + 1 < b.points.size(); ++bi) {
                        const double center = distanceSegmentToSegment(
                            a.points[ai], a.points[ai + 1],
                            b.points[bi], b.points[bi + 1]);
                        minimum = std::min(minimum,
                                           std::max(0.0, center
                                                             - (a.width + b.width) / 2.0));
                    }
                }
                if (!std::isfinite(minimum)) {
                    continue;
                }

                const double required = rules.minClearanceMil;
                if (minimum + kEpsilonMil < required) {
                    DrcViolation violation = makeViolation(
                        drc_code::kTrackTrackClearance,
                        QStringLiteral("走线 %1 与 %2 铜体间距 %3 mil，小于 %4 mil")
                            .arg(a.id)
                            .arg(b.id)
                            .arg(minimum, 0, 'f', 3)
                            .arg(required, 0, 'f', 3),
                        minimum, required);
                    violation.trackIdA = a.id;
                    violation.trackIdB = b.id;
                    violation.netA = a.net;
                    violation.netB = b.net;
                    result.violations.append(violation);
                }
            }
        }
    }

    if (rules.checkPadPad) {
        for (qsizetype i = 0; i < pads.size(); ++i) {
            for (qsizetype j = i + 1; j < pads.size(); ++j) {
                const PadInstance& a = pads[i];
                const PadInstance& b = pads[j];
                if (!layersShare(a.layers, b.layers)
                    || sameAssignedNet(a.net, b.net)) {
                    continue;
                }

                const double required = std::max(
                    {rules.minClearanceMil, a.pad.clearance, b.pad.clearance});
                const QRectF boundsA = padOutlineOnBoard(
                    a.pad, a.instancePos, a.rotationDeg, a.side).boundingRect();
                const QRectF boundsB = padOutlineOnBoard(
                    b.pad, b.instancePos, b.rotationDeg, b.side).boundingRect();
                if (!boundsMayBeWithin(boundsA, boundsB, required)) {
                    continue;
                }
                const double actual = distancePadToPad(
                    a.pad, a.instancePos, a.rotationDeg, a.side,
                    b.pad, b.instancePos, b.rotationDeg, b.side);
                if (actual + kEpsilonMil < required) {
                    DrcViolation violation = makeViolation(
                        drc_code::kPadPadClearance,
                        QStringLiteral("实例 %1 的焊盘 %2 与实例 %3 的焊盘 %4 间距 %5 mil，小于 %6 mil")
                            .arg(a.refdes)
                            .arg(a.pad.number)
                            .arg(b.refdes)
                            .arg(b.pad.number)
                            .arg(actual, 0, 'f', 3)
                            .arg(required, 0, 'f', 3),
                        actual, required);
                    violation.refdes = a.refdes;
                    violation.refdesB = b.refdes;
                    violation.netA = a.net;
                    violation.netB = b.net;
                    violation.padId = a.pad.id;
                    violation.relatedPadId = b.pad.id;
                    result.violations.append(violation);
                }
            }
        }
    }

    if (rules.checkPadVia) {
        for (const PadInstance& pad : pads) {
            for (const Via& via : board.vias) {
                bool layerMatch = false;
                for (LayerId layer : pad.layers) {
                    if (viaTouchesCopperLayer(via, layer)) {
                        layerMatch = true;
                        break;
                    }
                }
                if (!layerMatch || sameAssignedNet(pad.net, via.net)) {
                    continue;
                }

                const double required = std::max(rules.minClearanceMil,
                                                 pad.pad.clearance);
                const double actual = std::max(
                    0.0,
                    distancePointToPad(via.pos, pad.pad, pad.instancePos,
                                       pad.rotationDeg, pad.side)
                        - via.diameter / 2.0);
                if (actual + kEpsilonMil < required) {
                    DrcViolation violation = makeViolation(
                        drc_code::kPadViaClearance,
                        QStringLiteral("实例 %1 的焊盘 %2 与过孔 %3 间距 %4 mil，小于 %5 mil")
                            .arg(pad.refdes)
                            .arg(pad.pad.number)
                            .arg(via.id)
                            .arg(actual, 0, 'f', 3)
                            .arg(required, 0, 'f', 3),
                        actual, required);
                    violation.refdes = pad.refdes;
                    violation.netA = pad.net;
                    violation.netB = via.net;
                    violation.padId = pad.pad.id;
                    violation.viaId = via.id;
                    result.violations.append(violation);
                }
            }
        }
    }

    if (rules.checkPadPour) {
        for (const PadInstance& pad : pads) {
            for (const CopperPour& pour : board.pours) {
                if (!pad.layers.contains(pour.layerId)
                    || sameAssignedNet(pad.net, pour.net)) {
                    continue;
                }
                const double required = std::max(
                    {rules.minClearanceMil, pour.clearance, pad.pad.clearance});
                const QRectF padBounds = padOutlineOnBoard(
                    pad.pad, pad.instancePos, pad.rotationDeg, pad.side).boundingRect();
                if (!boundsMayBeWithin(padBounds, pour.points.boundingRect(), required)) {
                    continue;
                }
                const double actual = distancePolygonToPolygon(
                    padOutlineOnBoard(pad.pad, pad.instancePos, pad.rotationDeg,
                                      pad.side),
                    pour.points);
                if (actual + kEpsilonMil < required) {
                    DrcViolation violation = makeViolation(
                        drc_code::kPadPourClearance,
                        QStringLiteral("实例 %1 的焊盘 %2 与覆铜 %3 间距 %4 mil，小于 %5 mil")
                            .arg(pad.refdes)
                            .arg(pad.pad.number)
                            .arg(pour.id)
                            .arg(actual, 0, 'f', 3)
                            .arg(required, 0, 'f', 3),
                        actual, required);
                    violation.refdes = pad.refdes;
                    violation.netA = pad.net;
                    violation.netB = pour.net;
                    violation.padId = pad.pad.id;
                    violation.pourId = pour.id;
                    result.violations.append(violation);
                }
            }
        }
    }

    if (rules.checkTrackPour) {
        for (const Track& track : board.tracks) {
            for (const CopperPour& pour : board.pours) {
                if (track.layerId != pour.layerId
                    || sameAssignedNet(track.net, pour.net)) {
                    continue;
                }
                const double required = std::max(rules.minClearanceMil,
                                                 pour.clearance);
                double minimum = std::numeric_limits<double>::infinity();
                for (qsizetype i = 0; i + 1 < track.points.size(); ++i) {
                    minimum = std::min(
                        minimum,
                        std::max(0.0,
                                 distanceSegmentToPolygon(track.points[i],
                                                          track.points[i + 1],
                                                          pour.points)
                                     - track.width / 2.0));
                }
                if (std::isfinite(minimum) && minimum + kEpsilonMil < required) {
                    DrcViolation violation = makeViolation(
                        drc_code::kTrackPourClearance,
                        QStringLiteral("走线 %1 与覆铜 %2 间距 %3 mil，小于 %4 mil")
                            .arg(track.id)
                            .arg(pour.id)
                            .arg(minimum, 0, 'f', 3)
                            .arg(required, 0, 'f', 3),
                        minimum, required);
                    violation.trackIdA = track.id;
                    violation.pourId = pour.id;
                    violation.netA = track.net;
                    violation.netB = pour.net;
                    result.violations.append(violation);
                }
            }
        }
    }

    if (rules.checkViaPour) {
        for (const Via& via : board.vias) {
            for (const CopperPour& pour : board.pours) {
                if (!viaTouchesCopperLayer(via, pour.layerId)
                    || sameAssignedNet(via.net, pour.net)) {
                    continue;
                }
                const double required = std::max(rules.minClearanceMil,
                                                 pour.clearance);
                const double actual = std::max(
                    0.0, distancePointToPolygon(via.pos, pour.points)
                             - via.diameter / 2.0);
                if (actual + kEpsilonMil < required) {
                    DrcViolation violation = makeViolation(
                        drc_code::kViaPourClearance,
                        QStringLiteral("过孔 %1 与覆铜 %2 间距 %3 mil，小于 %4 mil")
                            .arg(via.id)
                            .arg(pour.id)
                            .arg(actual, 0, 'f', 3)
                            .arg(required, 0, 'f', 3),
                        actual, required);
                    violation.viaId = via.id;
                    violation.pourId = pour.id;
                    violation.netA = via.net;
                    violation.netB = pour.net;
                    result.violations.append(violation);
                }
            }
        }
    }

    if (rules.checkPourPour) {
        for (qsizetype i = 0; i < board.pours.size(); ++i) {
            for (qsizetype j = i + 1; j < board.pours.size(); ++j) {
                const CopperPour& a = board.pours[i];
                const CopperPour& b = board.pours[j];
                if (a.layerId != b.layerId || sameAssignedNet(a.net, b.net)) {
                    continue;
                }
                const double required = std::max(
                    {rules.minClearanceMil, a.clearance, b.clearance});
                if (!boundsMayBeWithin(a.points.boundingRect(),
                                       b.points.boundingRect(), required)) {
                    continue;
                }
                const double actual = distancePolygonToPolygon(a.points, b.points);
                if (actual + kEpsilonMil < required) {
                    DrcViolation violation = makeViolation(
                        drc_code::kPourPourClearance,
                        QStringLiteral("覆铜 %1 与覆铜 %2 间距 %3 mil，小于 %4 mil")
                            .arg(a.id)
                            .arg(b.id)
                            .arg(actual, 0, 'f', 3)
                            .arg(required, 0, 'f', 3),
                        actual, required);
                    violation.pourId = a.id;
                    violation.relatedPourId = b.id;
                    violation.netA = a.net;
                    violation.netB = b.net;
                    result.violations.append(violation);
                }
            }
        }
    }

    if (rules.checkViaTrack) {
        for (const Via& via : board.vias) {
            for (const Track& track : board.tracks) {
                if (!viaTouchesCopperLayer(via, track.layerId)) {
                    continue;
                }
                if (sameAssignedNet(via.net, track.net)) {
                    continue;
                }

                double minimum = std::numeric_limits<double>::infinity();
                for (qsizetype i = 0; i + 1 < track.points.size(); ++i) {
                    const double center = distancePointToSegment(
                        via.pos, track.points[i], track.points[i + 1]);
                    minimum = std::min(minimum,
                                       std::max(0.0, center
                                                         - via.diameter / 2.0
                                                         - track.width / 2.0));
                }
                if (!std::isfinite(minimum)) {
                    continue;
                }

                const double required = rules.minClearanceMil;
                if (minimum + kEpsilonMil < required) {
                    DrcViolation violation = makeViolation(
                        drc_code::kViaTrackClearance,
                        QStringLiteral("过孔 %1 与走线 %2 铜体间距 %3 mil，小于 %4 mil")
                            .arg(via.id)
                            .arg(track.id)
                            .arg(minimum, 0, 'f', 3)
                            .arg(required, 0, 'f', 3),
                        minimum, required);
                    violation.viaId = via.id;
                    violation.trackIdA = track.id;
                    violation.netA = via.net;
                    violation.netB = track.net;
                    result.violations.append(violation);
                }
            }
        }
    }

    return result;
}

} // namespace eda