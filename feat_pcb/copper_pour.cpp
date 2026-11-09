#include "feat_pcb/copper_pour.h"

#include "core/geometry.h"
#include "core/manufacturing_rules.h"

#include <QSet>

#include <cmath>

namespace eda {
namespace {

const CopperPour* findPour(const Board& board, PourId id)
{
    for (const CopperPour& pour : board.pours) {
        if (pour.id == id) {
            return &pour;
        }
    }
    return nullptr;
}

CopperPour* findPour(Board& board, PourId id)
{
    for (CopperPour& pour : board.pours) {
        if (pour.id == id) {
            return &pour;
        }
    }
    return nullptr;
}

bool isCopperLayer(const Board& board, LayerId layerId)
{
    for (const BoardLayer& layer : board.layers) {
        if (layer.id == layerId && layer.type == BoardLayerType::Copper) {
            return true;
        }
    }
    return false;
}

bool netExists(const Board& board, const QString& net)
{
    if (net.isEmpty()) {
        return true;
    }
    if (net == QStringLiteral("-")) {
        return false;
    }
    for (const Net& item : board.nets) {
        if (item.name == net) {
            return true;
        }
    }
    return false;
}

Issue makeIssue(const QString& code, const QString& message, PourId id)
{
    Issue issue;
    issue.severity = IssueSeverity::Error;
    issue.code = code;
    issue.message = message;
    issue.objectId = id;
    return issue;
}

QVector<Issue> validatePour(const Board& board, const CopperPour& pour)
{
    QVector<Issue> issues;
    if (pour.points.size() < 3 || !isSimplePolygon(pour.points)) {
        issues.append(makeIssue(issue_code::kPourPolygon,
                                QStringLiteral("覆铜 %1 的多边形不是简单多边形")
                                    .arg(QString::number(pour.id)),
                                pour.id));
    }
    if (!isCopperLayer(board, pour.layerId)) {
        issues.append(makeIssue(issue_code::kPourLayer,
                                QStringLiteral("覆铜 %1 不在已定义的铜层上")
                                    .arg(QString::number(pour.id)),
                                pour.id));
    }
    if (!netExists(board, pour.net)) {
        issues.append(makeIssue(issue_code::kPourNet,
                                QStringLiteral("覆铜 %1 引用了不存在的网络 %2")
                                    .arg(QString::number(pour.id), pour.net),
                                pour.id));
    }
    if (!std::isfinite(pour.clearance)
        || pour.clearance < validation::kMinClearanceMil) {
        issues.append(makeIssue(issue_code::kPourClearance,
                                QStringLiteral("覆铜 %1 间距 %2 mil 小于 %3 mil")
                                    .arg(QString::number(pour.id))
                                    .arg(pour.clearance, 0, 'f', 3)
                                    .arg(validation::kMinClearanceMil, 0, 'f', 3),
                                pour.id));
    }
    if (pour.priority < 0) {
        issues.append(makeIssue(issue_code::kPourPriority,
                                QStringLiteral("覆铜 %1 优先级不能为负")
                                    .arg(QString::number(pour.id)),
                                pour.id));
    }
    if (board.outline.size() >= 3) {
        bool outside = false;
        for (const QPointF& point : pour.points) {
            if (!containsPoint(board.outline, point)) {
                outside = true;
                break;
            }
        }
        if (!outside) {
            for (qsizetype i = 0; i < pour.points.size() && !outside; ++i) {
                for (qsizetype j = 0; j < board.outline.size(); ++j) {
                    if (segmentsIntersect(pour.points[i],
                                          pour.points[(i + 1) % pour.points.size()],
                                          board.outline[j],
                                          board.outline[(j + 1) % board.outline.size()],
                                          kEpsilonMil)) {
                        outside = true;
                        break;
                    }
                }
            }
        }
        if (outside) {
            issues.append(makeIssue(issue_code::kPourOutside,
                                    QStringLiteral("覆铜 %1 超出板框")
                                        .arg(QString::number(pour.id)),
                                    pour.id));
        }
    }
    return issues;
}

Status firstError(const QVector<Issue>& issues)
{
    return issues.isEmpty() ? Status::success()
                            : Status::failure(issues.first().message);
}

} // namespace

Result<PourId> CopperPourService::add(Board& board,
                                      const PourCreateOptions& options)
{
    const PourId previousNextId = board.nextPourId;
    CopperPour pour;
    pour.id = board.allocatePourId();
    if (pour.id == kInvalidId) {
        return {Status::failure(QStringLiteral("覆铜 ID 分配失败")), std::nullopt};
    }
    pour.points = options.polygon;
    pour.layerId = options.layerId;
    pour.net = options.net;
    pour.clearance = options.clearance;
    pour.priority = options.priority;

    const Status validation = firstError(validatePour(board, pour));
    if (!validation.ok) {
        board.nextPourId = previousNextId;
        return {validation, std::nullopt};
    }
    const Status inserted = board.insertCopperPour(pour);
    if (!inserted.ok) {
        board.nextPourId = previousNextId;
        return {inserted, std::nullopt};
    }
    return {Status::success(), pour.id};
}

Status CopperPourService::remove(Board& board, PourId id)
{
    return board.removeCopperPour(id);
}

Status CopperPourService::setPolygon(Board& board, PourId id,
                                     const QPolygonF& polygon)
{
    CopperPour* pour = findPour(board, id);
    if (!pour) {
        return Status::failure(QStringLiteral("未找到覆铜 %1").arg(id));
    }
    CopperPour candidate = *pour;
    candidate.points = polygon;
    const Status validation = firstError(validatePour(board, candidate));
    if (!validation.ok) {
        return validation;
    }
    pour->points = polygon;
    return Status::success();
}

Status CopperPourService::setLayer(Board& board, PourId id, LayerId layerId)
{
    CopperPour* pour = findPour(board, id);
    if (!pour) {
        return Status::failure(QStringLiteral("未找到覆铜 %1").arg(id));
    }
    CopperPour candidate = *pour;
    candidate.layerId = layerId;
    const Status validation = firstError(validatePour(board, candidate));
    if (!validation.ok) {
        return validation;
    }
    pour->layerId = layerId;
    return Status::success();
}

Status CopperPourService::setNet(Board& board, PourId id, const QString& net)
{
    CopperPour* pour = findPour(board, id);
    if (!pour) {
        return Status::failure(QStringLiteral("未找到覆铜 %1").arg(id));
    }
    CopperPour candidate = *pour;
    candidate.net = net;
    const Status validation = firstError(validatePour(board, candidate));
    if (!validation.ok) {
        return validation;
    }
    pour->net = net;
    return Status::success();
}

Status CopperPourService::setClearance(Board& board, PourId id,
                                       double clearanceMil)
{
    CopperPour* pour = findPour(board, id);
    if (!pour) {
        return Status::failure(QStringLiteral("未找到覆铜 %1").arg(id));
    }
    CopperPour candidate = *pour;
    candidate.clearance = clearanceMil;
    const Status validation = firstError(validatePour(board, candidate));
    if (!validation.ok) {
        return validation;
    }
    pour->clearance = clearanceMil;
    return Status::success();
}

Status CopperPourService::setPriority(Board& board, PourId id, int priority)
{
    CopperPour* pour = findPour(board, id);
    if (!pour) {
        return Status::failure(QStringLiteral("未找到覆铜 %1").arg(id));
    }
    CopperPour candidate = *pour;
    candidate.priority = priority;
    const Status validation = firstError(validatePour(board, candidate));
    if (!validation.ok) {
        return validation;
    }
    pour->priority = priority;
    return Status::success();
}

Status CopperPourService::rebuild(const Board& board, PourId id)
{
    const CopperPour* pour = findPour(board, id);
    if (!pour) {
        return Status::failure(QStringLiteral("未找到覆铜 %1").arg(id));
    }
    return firstError(validatePour(board, *pour));
}

QVector<Issue> CopperPourService::validate(const Board& board)
{
    QVector<Issue> issues;
    QSet<PourId> ids;
    for (const CopperPour& pour : board.pours) {
        if (pour.id == kInvalidId || ids.contains(pour.id)) {
            issues.append(makeIssue(issue_code::kPourId,
                                    QStringLiteral("覆铜 ID 为零或重复：%1")
                                        .arg(QString::number(pour.id)),
                                    pour.id));
        } else {
            ids.insert(pour.id);
        }
        issues += validatePour(board, pour);
    }
    return issues;
}

} // namespace eda