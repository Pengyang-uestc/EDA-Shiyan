#include "feat_pcb/layer_service.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>

namespace eda {
namespace {

LayerGroup* findGroup(Board& board, LayerGroupId id)
{
    for (LayerGroup& group : board.groups) {
        if (group.id == id) {
            return &group;
        }
    }
    return nullptr;
}

const LayerGroup* findGroup(const Board& board, LayerGroupId id)
{
    for (const LayerGroup& group : board.groups) {
        if (group.id == id) {
            return &group;
        }
    }
    return nullptr;
}

BoardLayer* findLayer(Board& board, LayerId id)
{
    for (BoardLayer& layer : board.layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

const BoardLayer* findLayer(const Board& board, LayerId id)
{
    for (const BoardLayer& layer : board.layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

Issue makeIssue(IssueSeverity severity, const QString& code,
                const QString& message, LayerId layerId = -1,
                LayerGroupId groupId = -1)
{
    Issue issue;
    issue.severity = severity;
    issue.code = code;
    issue.message = message;
    issue.layerId = layerId;
    issue.groupId = groupId;
    return issue;
}

bool knownLayerType(BoardLayerType type)
{
    switch (type) {
    case BoardLayerType::Copper:
    case BoardLayerType::Silk:
    case BoardLayerType::Mask:
    case BoardLayerType::Outline:
        return true;
    }
    return false;
}

bool standardLayer(LayerId id)
{
    return id >= kTopCopper && id <= kBoardOutline;
}

bool isStandardGroupId(LayerGroupId id)
{
    return id >= 1 && id <= 3;
}

bool fixedTypeMatches(const BoardLayer& layer)
{
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

QString standardName(LayerId id)
{
    switch (id) {
    case kTopCopper: return QStringLiteral("Top Copper");
    case kBottomCopper: return QStringLiteral("Bottom Copper");
    case kTopSilk: return QStringLiteral("Top Silk");
    case kBottomSilk: return QStringLiteral("Bottom Silk");
    case kTopMask: return QStringLiteral("Top Mask");
    case kBottomMask: return QStringLiteral("Bottom Mask");
    case kBoardOutline: return QStringLiteral("Board Outline");
    default: return QString();
    }
}

QColor standardColor(LayerId id)
{
    switch (id) {
    case kTopCopper: return QColor(QStringLiteral("#C83232"));
    case kBottomCopper: return QColor(QStringLiteral("#3264C8"));
    case kTopSilk: return QColor(QStringLiteral("#F0F0F0"));
    case kBottomSilk: return QColor(QStringLiteral("#D0D0D0"));
    case kTopMask: return QColor(QStringLiteral("#C8A000"));
    case kBottomMask: return QColor(QStringLiteral("#A08000"));
    case kBoardOutline: return QColor(QStringLiteral("#FFFF00"));
    default: return QColor();
    }
}

BoardLayerType standardType(LayerId id)
{
    if (id == kTopCopper || id == kBottomCopper) {
        return BoardLayerType::Copper;
    }
    if (id == kTopSilk || id == kBottomSilk) {
        return BoardLayerType::Silk;
    }
    if (id == kTopMask || id == kBottomMask) {
        return BoardLayerType::Mask;
    }
    return BoardLayerType::Outline;
}

LayerGroupId standardGroup(LayerId id)
{
    if (id == kTopCopper || id == kTopSilk || id == kTopMask) {
        return 1;
    }
    if (id == kBottomCopper || id == kBottomSilk || id == kBottomMask) {
        return 2;
    }
    return 3;
}

int standardOrder(LayerId id)
{
    if (id == kTopCopper || id == kBottomCopper) {
        return 0;
    }
    if (id == kTopSilk || id == kBottomSilk) {
        return 1;
    }
    if (id == kTopMask || id == kBottomMask) {
        return 2;
    }
    return 0;
}

bool layerUsed(const Board& board, LayerId id)
{
    for (const Track& track : board.tracks) {
        if (track.layerId == id) {
            return true;
        }
    }
    for (const Via& via : board.vias) {
        if (via.fromLayer == id || via.toLayer == id) {
            return true;
        }
    }
    for (const CopperPour& pour : board.pours) {
        if (pour.layerId == id) {
            return true;
        }
    }
    return false;
}

bool groupHasLayers(const Board& board, LayerGroupId id)
{
    for (const BoardLayer& layer : board.layers) {
        if (layer.groupId == id) {
            return true;
        }
    }
    return false;
}

bool groupOrderUsed(const Board& board, int order, LayerGroupId except)
{
    for (const LayerGroup& group : board.groups) {
        if (group.id != except && group.stackOrder == order) {
            return true;
        }
    }
    return false;
}

bool layerOrderUsed(const Board& board, LayerGroupId groupId, int order,
                    LayerId except)
{
    for (const BoardLayer& layer : board.layers) {
        if (layer.groupId == groupId && layer.id != except
            && layer.stackOrder == order) {
            return true;
        }
    }
    return false;
}

bool layerNameUsed(const Board& board, LayerGroupId groupId,
                   const QString& name, LayerId except)
{
    for (const BoardLayer& layer : board.layers) {
        if (layer.groupId == groupId && layer.id != except
            && layer.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

Status LayerService::ensureDefaultLayers(Board& board)
{
    const LayerGroup* top = findGroup(board, 1);
    const LayerGroup* bottom = findGroup(board, 2);
    const LayerGroup* mechanical = findGroup(board, 3);
    if ((top && (top->name != QStringLiteral("Top") || top->stackOrder != 0))
        || (bottom && (bottom->name != QStringLiteral("Bottom")
                        || bottom->stackOrder != 1))
        || (mechanical && (mechanical->name != QStringLiteral("Mechanical")
                            || mechanical->stackOrder != 2))) {
        return Status::failure(QStringLiteral("标准层组定义冲突"));
    }

    for (LayerId id = kTopCopper; id <= kBoardOutline; ++id) {
        const BoardLayer* layer = findLayer(board, id);
        if (layer && (layer->groupId != standardGroup(id)
                      || layer->type != standardType(id))) {
            return Status::failure(QStringLiteral("标准层定义冲突：%1").arg(id));
        }
    }

    if (!findGroup(board, 1)) {
        board.groups.append(LayerGroup{1, QStringLiteral("Top"), 0});
    }
    if (!findGroup(board, 2)) {
        board.groups.append(LayerGroup{2, QStringLiteral("Bottom"), 1});
    }
    if (!findGroup(board, 3)) {
        board.groups.append(LayerGroup{3, QStringLiteral("Mechanical"), 2});
    }

    for (LayerId id = kTopCopper; id <= kBoardOutline; ++id) {
        if (findLayer(board, id)) {
            continue;
        }
        BoardLayer layer;
        layer.id = id;
        layer.groupId = standardGroup(id);
        layer.name = standardName(id);
        layer.type = standardType(id);
        layer.color = standardColor(id);
        layer.stackOrder = standardOrder(id);
        board.layers.append(layer);
    }
    return Status::success();
}

Result<LayerGroupId> LayerService::addGroup(Board& board, const QString& name,
                                            int stackOrder)
{
    if (name.isEmpty()) {
        return {Status::failure(QStringLiteral("层组名称不能为空")), std::nullopt};
    }
    if (stackOrder < 0 || groupOrderUsed(board, stackOrder, -1)) {
        return {Status::failure(QStringLiteral("层组 stackOrder 非法或重复")),
                std::nullopt};
    }
    for (const LayerGroup& group : board.groups) {
        if (group.name == name) {
            return {Status::failure(QStringLiteral("层组名称重复")), std::nullopt};
        }
    }

    LayerGroupId next = 1;
    for (const LayerGroup& group : board.groups) {
        if (group.id >= next) {
            if (group.id == std::numeric_limits<LayerGroupId>::max()) {
                return {Status::failure(QStringLiteral("层组 ID 已耗尽")), std::nullopt};
            }
            next = group.id + 1;
        }
    }
    board.groups.append(LayerGroup{next, name, stackOrder});
    return {Status::success(), next};
}

Status LayerService::removeGroup(Board& board, LayerGroupId id)
{
    if (isStandardGroupId(id)) {
        return Status::failure(QStringLiteral("不能删除标准层组 %1").arg(id));
    }
    if (groupHasLayers(board, id)) {
        return Status::failure(QStringLiteral("层组 %1 仍有层，不能删除")
                                   .arg(id));
    }
    for (qsizetype i = 0; i < board.groups.size(); ++i) {
        if (board.groups[i].id == id) {
            board.groups.removeAt(i);
            return Status::success();
        }
    }
    return Status::failure(QStringLiteral("未找到层组 %1").arg(id));
}

Result<LayerId> LayerService::addLayer(Board& board, LayerGroupId groupId,
                                       const QString& name,
                                       BoardLayerType type,
                                       const QColor& color, int stackOrder)
{
    if (!findGroup(board, groupId)) {
        return {Status::failure(QStringLiteral("层组不存在")), std::nullopt};
    }
    if (name.isEmpty() || !color.isValid() || !knownLayerType(type)) {
        return {Status::failure(QStringLiteral("层名称、颜色或类型非法")), std::nullopt};
    }
    if (stackOrder < 0) {
        return {Status::failure(QStringLiteral("stackOrder 不能为负")), std::nullopt};
    }
    if (layerNameUsed(board, groupId, name, -1)) {
        return {Status::failure(QStringLiteral("同一层组内层名称重复")), std::nullopt};
    }
    if (layerOrderUsed(board, groupId, stackOrder, -1)) {
        return {Status::failure(QStringLiteral("同一层组内 stackOrder 重复")),
                std::nullopt};
    }

    LayerId next = 100;
    for (const BoardLayer& layer : board.layers) {
        if (layer.id >= 100) {
            if (layer.id == std::numeric_limits<LayerId>::max()) {
                return {Status::failure(QStringLiteral("层 ID 已耗尽")), std::nullopt};
            }
            next = std::max(next, layer.id + 1);
        }
    }

    BoardLayer layer;
    layer.id = next;
    layer.groupId = groupId;
    layer.name = name;
    layer.type = type;
    layer.color = color;
    layer.stackOrder = stackOrder;
    board.layers.append(layer);
    return {Status::success(), next};
}

Status LayerService::removeLayer(Board& board, LayerId id)
{
    if (standardLayer(id)) {
        return Status::failure(QStringLiteral("不能删除标准层 %1").arg(id));
    }
    if (layerUsed(board, id)) {
        return Status::failure(QStringLiteral("层 %1 仍被走线/过孔/覆铜引用")
                                   .arg(id));
    }
    for (qsizetype i = 0; i < board.layers.size(); ++i) {
        if (board.layers[i].id == id) {
            board.layers.removeAt(i);
            return Status::success();
        }
    }
    return Status::failure(QStringLiteral("未找到层 %1").arg(id));
}

Status LayerService::renameLayer(Board& board, LayerId id, const QString& name)
{
    BoardLayer* layer = findLayer(board, id);
    if (!layer) {
        return Status::failure(QStringLiteral("未找到层 %1").arg(id));
    }
    if (standardLayer(id)) {
        return Status::failure(QStringLiteral("标准层名称不可修改"));
    }
    if (name.isEmpty() || layerNameUsed(board, layer->groupId, name, id)) {
        return Status::failure(QStringLiteral("层名称非法或重复"));
    }
    layer->name = name;
    return Status::success();
}

Status LayerService::setLayerColor(Board& board, LayerId id, const QColor& color)
{
    BoardLayer* layer = findLayer(board, id);
    if (!layer) {
        return Status::failure(QStringLiteral("未找到层 %1").arg(id));
    }
    if (!color.isValid()) {
        return Status::failure(QStringLiteral("颜色非法"));
    }
    layer->color = color;
    return Status::success();
}

Status LayerService::setLayerStackOrder(Board& board, LayerId id,
                                        int stackOrder)
{
    BoardLayer* layer = findLayer(board, id);
    if (!layer) {
        return Status::failure(QStringLiteral("未找到层 %1").arg(id));
    }
    if (stackOrder < 0
        || layerOrderUsed(board, layer->groupId, stackOrder, id)) {
        return Status::failure(QStringLiteral("stackOrder 非法或重复"));
    }
    layer->stackOrder = stackOrder;
    return Status::success();
}

Status LayerService::setGroupStackOrder(Board& board, LayerGroupId id,
                                        int stackOrder)
{
    LayerGroup* group = findGroup(board, id);
    if (!group) {
        return Status::failure(QStringLiteral("未找到层组 %1").arg(id));
    }
    if (stackOrder < 0 || groupOrderUsed(board, stackOrder, id)) {
        return Status::failure(QStringLiteral("层组 stackOrder 非法或重复"));
    }
    group->stackOrder = stackOrder;
    return Status::success();
}

QVector<Issue> LayerService::validate(const Board& board)
{
    QVector<Issue> issues;
    QSet<LayerGroupId> groupIds;
    QSet<int> groupOrders;
    QSet<LayerId> layerIds;
    QHash<LayerGroupId, QSet<int>> layerOrders;

    for (const LayerGroup& group : board.groups) {
        if (group.id <= 0 || groupIds.contains(group.id)) {
            issues.append(makeIssue(IssueSeverity::Error,
                                    issue_code::kLayerStructure,
                                    QStringLiteral("层组 ID 为零或重复"),
                                    -1, group.id));
        } else {
            groupIds.insert(group.id);
        }
        if (group.name.isEmpty()) {
            issues.append(makeIssue(IssueSeverity::Error,
                                    issue_code::kLayerStructure,
                                    QStringLiteral("层组名称为空"),
                                    -1, group.id));
        }
        if (group.stackOrder < 0) {
            issues.append(makeIssue(IssueSeverity::Error,
                                    issue_code::kLayerOrder,
                                    QStringLiteral("层组 stackOrder 不能为负"),
                                    -1, group.id));
        }
        if (groupOrders.contains(group.stackOrder)) {
            issues.append(makeIssue(IssueSeverity::Error,
                                    issue_code::kLayerOrder,
                                    QStringLiteral("层组 stackOrder 重复"),
                                    -1, group.id));
        } else {
            groupOrders.insert(group.stackOrder);
        }
    }

    for (const BoardLayer& layer : board.layers) {
        if ((layer.id < 0 || (layer.id > 6 && layer.id < 100))
            || layerIds.contains(layer.id)) {
            issues.append(makeIssue(IssueSeverity::Error,
                                    issue_code::kLayerStructure,
                                    QStringLiteral("层 ID 非法或重复"),
                                    layer.id, layer.groupId));
        } else {
            layerIds.insert(layer.id);
        }
        if (!groupIds.contains(layer.groupId)) {
            issues.append(makeIssue(IssueSeverity::Error,
                                    issue_code::kLayerStructure,
                                    QStringLiteral("层引用了不存在的层组"),
                                    layer.id, layer.groupId));
        }
        if (layer.name.isEmpty() || !layer.color.isValid()
            || !knownLayerType(layer.type) || !fixedTypeMatches(layer)) {
            issues.append(makeIssue(IssueSeverity::Error,
                                    issue_code::kLayerStructure,
                                    QStringLiteral("层名称、颜色或类型非法"),
                                    layer.id, layer.groupId));
        }
        if (layer.stackOrder < 0) {
            issues.append(makeIssue(IssueSeverity::Error,
                                    issue_code::kLayerOrder,
                                    QStringLiteral("层 stackOrder 不能为负"),
                                    layer.id, layer.groupId));
        }
        QSet<int>& orders = layerOrders[layer.groupId];
        if (orders.contains(layer.stackOrder)) {
            issues.append(makeIssue(IssueSeverity::Error,
                                    issue_code::kLayerOrder,
                                    QStringLiteral("同一层组内 stackOrder 重复"),
                                    layer.id, layer.groupId));
        } else {
            orders.insert(layer.stackOrder);
        }
    }

    for (LayerId id = kTopCopper; id <= kBoardOutline; ++id) {
        if (!layerIds.contains(id)) {
            const bool required = id == kTopCopper || id == kBottomCopper
                                  || id == kBoardOutline;
            issues.append(makeIssue(required ? IssueSeverity::Error
                                             : IssueSeverity::Warning,
                                    issue_code::kLayerStructure,
                                    QStringLiteral("缺少标准层 %1").arg(id),
                                    id));
        }
    }

    return issues;
}

} // namespace eda