#include "feat_pcb/layer_model.h"

#include <QStandardItem>

#include <algorithm>

namespace eda {
namespace {

QString typeName(BoardLayerType type)
{
    switch (type) {
    case BoardLayerType::Copper: return QStringLiteral("Copper");
    case BoardLayerType::Silk: return QStringLiteral("Silk");
    case BoardLayerType::Mask: return QStringLiteral("Mask");
    case BoardLayerType::Outline: return QStringLiteral("Outline");
    }
    return QString();
}

QList<QStandardItem*> makeGroupRow(const LayerGroup& group)
{
    QStandardItem* name = new QStandardItem(group.name);
    QStandardItem* type = new QStandardItem(QStringLiteral("组"));
    QStandardItem* color = new QStandardItem(QString());
    QStandardItem* order = new QStandardItem(QString::number(group.stackOrder));
    for (QStandardItem* item : {name, type, color, order}) {
        item->setEditable(false);
        item->setData(-1, LayerModel::LayerIdRole);
        item->setData(group.id, LayerModel::GroupIdRole);
        item->setData(true, LayerModel::IsGroupRole);
        item->setData(group.stackOrder, LayerModel::StackOrderRole);
    }
    return {name, type, color, order};
}

QList<QStandardItem*> makeLayerRow(const BoardLayer& layer)
{
    QStandardItem* name = new QStandardItem(layer.name);
    QStandardItem* type = new QStandardItem(typeName(layer.type));
    QStandardItem* color = new QStandardItem(layer.color.name());
    QStandardItem* order = new QStandardItem(QString::number(layer.stackOrder));
    for (QStandardItem* item : {name, type, color, order}) {
        item->setEditable(false);
        item->setData(layer.id, LayerModel::LayerIdRole);
        item->setData(layer.groupId, LayerModel::GroupIdRole);
        item->setData(static_cast<int>(layer.type), LayerModel::LayerTypeRole);
        item->setData(layer.color, LayerModel::ColorRole);
        item->setData(layer.stackOrder, LayerModel::StackOrderRole);
        item->setData(false, LayerModel::IsGroupRole);
    }
    color->setData(layer.color, Qt::DecorationRole);
    return {name, type, color, order};
}

} // namespace

LayerModel::LayerModel(QObject* parent)
    : QStandardItemModel(parent)
{
    setHorizontalHeaderLabels(
        {QStringLiteral("层"), QStringLiteral("类型"),
         QStringLiteral("颜色"), QStringLiteral("顺序")});
}

void LayerModel::setBoard(const Board* board)
{
    m_board = board;
    refresh();
}

void LayerModel::refresh()
{
    clear();
    setHorizontalHeaderLabels(
        {QStringLiteral("层"), QStringLiteral("类型"),
         QStringLiteral("颜色"), QStringLiteral("顺序")});
    if (!m_board) {
        return;
    }

    QVector<LayerGroup> groups = m_board->groups;
    std::sort(groups.begin(), groups.end(), [](const LayerGroup& a,
                                               const LayerGroup& b) {
        if (a.stackOrder != b.stackOrder) {
            return a.stackOrder < b.stackOrder;
        }
        return a.id < b.id;
    });

    for (const LayerGroup& group : groups) {
        QList<QStandardItem*> groupRow = makeGroupRow(group);
        QVector<BoardLayer> layers;
        for (const BoardLayer& layer : m_board->layers) {
            if (layer.groupId == group.id) {
                layers.append(layer);
            }
        }
        std::sort(layers.begin(), layers.end(), [](const BoardLayer& a,
                                                   const BoardLayer& b) {
            if (a.stackOrder != b.stackOrder) {
                return a.stackOrder < b.stackOrder;
            }
            return a.id < b.id;
        });
        for (const BoardLayer& layer : layers) {
            groupRow[0]->appendRow(makeLayerRow(layer));
        }
        invisibleRootItem()->appendRow(groupRow);
    }
}

QModelIndex LayerModel::indexForLayer(LayerId id) const
{
    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex groupIndex = index(row, 0);
        const int children = rowCount(groupIndex);
        for (int child = 0; child < children; ++child) {
            const QModelIndex layerIndex = index(child, 0, groupIndex);
            if (layerIndex.data(LayerIdRole).toInt() == id) {
                return layerIndex;
            }
        }
    }
    return {};
}

LayerId LayerModel::layerId(const QModelIndex& index) const
{
    if (!index.isValid() || index.data(IsGroupRole).toBool()) {
        return -1;
    }
    return index.data(LayerIdRole).toInt();
}

} // namespace eda