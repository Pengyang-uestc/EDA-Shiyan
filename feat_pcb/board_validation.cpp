#include "feat_pcb/board_validation.h"

#include "core/geometry.h"
#include "core/manufacturing_rules.h"

#include <QHash>
#include <QSet>

#include <cmath>

namespace eda {
namespace {

Issue makeIssue(IssueSeverity severity, const QString& code,
                const QString& message, const QString& refdes = {},
                quint64 objectId = 0)
{
    Issue issue;
    issue.severity = severity;
    issue.code = code;
    issue.message = message;
    issue.refdes = refdes;
    issue.objectId = objectId;
    return issue;
}

bool finitePoint(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool insideOutline(const QPolygonF& outline, const QPointF& point)
{
    return finitePoint(point) && containsPoint(outline, point);
}

QVector<QPointF> padCorners(const Pad& pad)
{
    const double halfWidth = pad.size.width() / 2.0;
    const double halfHeight = pad.size.height() / 2.0;
    return {
        pad.pos + QPointF(-halfWidth, -halfHeight),
        pad.pos + QPointF(halfWidth, -halfHeight),
        pad.pos + QPointF(halfWidth, halfHeight),
        pad.pos + QPointF(-halfWidth, halfHeight),
    };
}

} // namespace

QVector<Issue> validateBoardManufacturing(const Board& board,
                                          const FootprintResolver& resolveFootprint,
                                          const FootprintValidation& validateFootprint)
{
    QVector<Issue> issues;

    const Status structure = validateBoardStructure(board);
    if (!structure.ok) {
        issues.append(makeIssue(IssueSeverity::Error, issue_code::kBoardStructure,
                                structure.message));
        return issues;
    }

    // A 的 validateBoard 会再次做结构校验并解析封装；这里用缓存包住 resolver，
    // 保证每个 footprintId 只真正加载一次，后面的实例检查复用同一份结果。
    QHash<QString, Result<Footprint>> footprintCache;
    FootprintResolver cachedResolver;
    if (resolveFootprint) {
        cachedResolver = [&](const QString& footprintId) -> Result<Footprint> {
            const auto found = footprintCache.constFind(footprintId);
            if (found != footprintCache.cend()) {
                return *found;
            }
            Result<Footprint> result = resolveFootprint(footprintId);
            footprintCache.insert(footprintId, result);
            return result;
        };
    }

    const Status references = validateBoard(board, cachedResolver);
    if (!references.ok) {
        issues.append(makeIssue(IssueSeverity::Error, issue_code::kBoardReferences,
                                references.message));
    }

    QSet<LayerId> definedLayers;
    for (const BoardLayer& layer : board.layers) {
        definedLayers.insert(layer.id);
    }
    const QVector<LayerId> standardLayers{
        kTopCopper, kBottomCopper, kTopSilk, kBottomSilk,
        kTopMask, kBottomMask, kBoardOutline};
    for (LayerId layerId : standardLayers) {
        if (!definedLayers.contains(layerId)) {
            issues.append(makeIssue(
                IssueSeverity::Warning, issue_code::kBoardLayerMissing,
                QStringLiteral("缺少标准层 layerId=%1，建议在层面板补全").arg(layerId)));
        }
    }

    QHash<int, LayerGroupId> groupByOrder;
    for (const LayerGroup& group : board.groups) {
        if (groupByOrder.contains(group.stackOrder)) {
            Issue issue = makeIssue(
                IssueSeverity::Error, issue_code::kBoardGroupOrder,
                QStringLiteral("层组 %1 与 %2 的 stackOrder 重复：%3")
                    .arg(groupByOrder.value(group.stackOrder))
                    .arg(group.id)
                    .arg(group.stackOrder));
            issue.groupId = group.id;
            issues.append(issue);
        } else {
            groupByOrder.insert(group.stackOrder, group.id);
        }
    }

    QHash<LayerGroupId, QSet<int>> layerOrders;
    for (const BoardLayer& layer : board.layers) {
        QSet<int>& orders = layerOrders[layer.groupId];
        if (orders.contains(layer.stackOrder)) {
            Issue issue = makeIssue(
                IssueSeverity::Error, issue_code::kBoardLayerOrder,
                QStringLiteral("层组 %1 内的 stackOrder %2 重复")
                    .arg(layer.groupId)
                    .arg(layer.stackOrder));
            issue.groupId = layer.groupId;
            issue.layerId = layer.id;
            issues.append(issue);
        } else {
            orders.insert(layer.stackOrder);
        }
    }

    if (!validateFootprint && !board.instances.isEmpty()) {
        issues.append(makeIssue(
            IssueSeverity::Warning, issue_code::kBoardFootprintUnchecked,
            QStringLiteral("未提供封装制造校验回调，实例的钻孔/环宽/焊盘间距未检查")));
    }

    for (const Instance& instance : board.instances) {
        if (!insideOutline(board.outline, instance.pos)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kBoardInstanceOutside,
                QStringLiteral("实例 %1 的中心不在板框内").arg(instance.refdes),
                instance.refdes));
        }

        if (!resolveFootprint) {
            continue;
        }

        const Result<Footprint> footprint = cachedResolver(instance.footprintId);
        if (!footprint.status.ok || !footprint.value.has_value()) {
            if (references.ok) {
                issues.append(makeIssue(
                    IssueSeverity::Error, issue_code::kBoardFootprint,
                    QStringLiteral("实例 %1 的封装 %2 无法解析：%3")
                        .arg(instance.refdes, instance.footprintId,
                             footprint.status.message),
                    instance.refdes));
            }
            continue;
        }

        if (validateFootprint) {
            const QVector<Issue> footprintIssues = validateFootprint(*footprint.value);
            for (Issue issue : footprintIssues) {
                issue.refdes = instance.refdes;
                issue.message = QStringLiteral("%1: %2")
                                    .arg(instance.refdes, issue.message);
                issues.append(issue);
            }
        }

        for (const Pad& pad : footprint.value->pads) {
            bool outside = false;
            for (const QPointF& corner : padCorners(pad)) {
                const QPointF boardPoint = footprintToBoard(
                    corner, instance.pos, instance.rotationDeg, instance.side);
                if (!insideOutline(board.outline, boardPoint)) {
                    outside = true;
                    break;
                }
            }
            if (outside) {
                issues.append(makeIssue(
                    IssueSeverity::Error, issue_code::kBoardInstanceOutside,
                    QStringLiteral("实例 %1 的焊盘 %2 超出板框")
                        .arg(instance.refdes)
                        .arg(pad.number),
                    instance.refdes));
                break;
            }
        }
    }

    for (const Track& track : board.tracks) {
        if (!std::isfinite(track.width)
            || track.width < validation::kMinTrackWidthMil - kEpsilonMil) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kTrackWidth,
                QStringLiteral("走线 %1 宽度 %2 mil 小于 %3 mil")
                    .arg(track.id)
                    .arg(track.width, 0, 'f', 3)
                    .arg(validation::kMinTrackWidthMil, 0, 'f', 3),
                {}, track.id));
        }

        if (track.net.isEmpty()) {
            issues.append(makeIssue(
                IssueSeverity::Warning, issue_code::kNetUnassigned,
                QStringLiteral("走线 %1 未分配网络").arg(track.id), {}, track.id));
        }

        bool outside = false;
        for (qsizetype i = 0; i < track.points.size(); ++i) {
            if (!insideOutline(board.outline, track.points[i])) {
                outside = true;
                break;
            }
            if (i > 0) {
                const QPointF midpoint = (track.points[i - 1] + track.points[i]) / 2.0;
                if (!insideOutline(board.outline, midpoint)) {
                    outside = true;
                    break;
                }
            }
        }
        if (outside) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kTrackOutside,
                QStringLiteral("走线 %1 的中心线超出板框（端点/中点近似）")
                    .arg(track.id),
                {}, track.id));
        }
    }

    for (const Via& via : board.vias) {
        if (!validation::drillOk(via.drill)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kViaDrill,
                QStringLiteral("过孔 %1 钻孔 %2 mil 超出 0.2–6.0 mm 范围")
                    .arg(via.id)
                    .arg(via.drill, 0, 'f', 3),
                {}, via.id));
        }
        if (!validation::annularRingOk(via.diameter, via.diameter, via.drill)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kViaAnnularRing,
                QStringLiteral("过孔 %1 环宽不足：直径 %2 mil，钻孔 %3 mil")
                    .arg(via.id)
                    .arg(via.diameter, 0, 'f', 3)
                    .arg(via.drill, 0, 'f', 3),
                {}, via.id));
        }
        if (!insideOutline(board.outline, via.pos)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kViaOutside,
                QStringLiteral("过孔 %1 中心超出板框").arg(via.id), {}, via.id));
        }
        if (via.net.isEmpty()) {
            issues.append(makeIssue(
                IssueSeverity::Warning, issue_code::kNetUnassigned,
                QStringLiteral("过孔 %1 未分配网络").arg(via.id), {}, via.id));
        }
    }

    for (const CopperPour& pour : board.pours) {
        if (!std::isfinite(pour.clearance)
            || pour.clearance < validation::kMinClearanceMil - kEpsilonMil) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kPourClearance,
                QStringLiteral("覆铜 %1 间距 %2 mil 小于 %3 mil")
                    .arg(pour.id)
                    .arg(pour.clearance, 0, 'f', 3)
                    .arg(validation::kMinClearanceMil, 0, 'f', 3),
                {}, pour.id));
        }

        bool outside = false;
        for (const QPointF& point : pour.points) {
            if (!insideOutline(board.outline, point)) {
                outside = true;
                break;
            }
        }
        if (outside) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kPourOutside,
                QStringLiteral("覆铜 %1 的顶点超出板框（顶点近似）").arg(pour.id),
                {}, pour.id));
        }
        if (pour.net.isEmpty()) {
            issues.append(makeIssue(
                IssueSeverity::Warning, issue_code::kNetUnassigned,
                QStringLiteral("覆铜 %1 未分配网络").arg(pour.id), {}, pour.id));
        }
    }

    return issues;
}

} // namespace eda