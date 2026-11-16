#include "app/c_services.h"

#include "app/document_context.h"
#include "feat_fp/footprint_validation.h"
#include "feat_pcb/board_validation.h"

#include <QIcon>
#include <QPixmap>
#include <QStandardItem>

#include <algorithm>
#include <utility>

namespace {
ValidationIssue adaptIssue(const eda::Issue& issue) {
    return {issue.severity == eda::IssueSeverity::Error ? ValidationSeverity::Error : ValidationSeverity::Warning,
            issue.message, issue.padId};
}

QIcon colorIcon(const QColor& color) {
    QPixmap pixmap(14, 14);
    pixmap.fill(color);
    return QIcon(pixmap);
}

QString footprintLayerName(eda::LayerId id) {
    switch (id) {
    case eda::kTopCopper:
        return QObject::tr("Top Copper");
    case eda::kBottomCopper:
        return QObject::tr("Bottom Copper");
    case eda::kTopSilk:
        return QObject::tr("Top Silk");
    case eda::kBottomSilk:
        return QObject::tr("Bottom Silk");
    case eda::kTopMask:
        return QObject::tr("Top Mask");
    case eda::kBottomMask:
        return QObject::tr("Bottom Mask");
    default:
        return QObject::tr("Layer");
    }
}

QColor footprintLayerColor(eda::LayerId id) {
    switch (id) {
    case eda::kTopCopper:
        return QColor(QStringLiteral("#B64845"));
    case eda::kBottomCopper:
        return QColor(QStringLiteral("#3D70A8"));
    case eda::kTopSilk:
        return QColor(QStringLiteral("#756824"));
    case eda::kBottomSilk:
        return QColor(QStringLiteral("#657443"));
    case eda::kTopMask:
        return QColor(QStringLiteral("#6B8D70"));
    default:
        return QColor(QStringLiteral("#4D7B7F"));
    }
}
} // namespace

ManufacturingValidationService::ManufacturingValidationService(eda::FootprintResolver resolver)
    : m_resolver(std::move(resolver)) {}

QVector<ValidationIssue> ManufacturingValidationService::validateFootprint(const eda::Footprint& footprint) const {
    QVector<ValidationIssue> output;
    for (const eda::Issue& issue : eda::validateFootprint(footprint))
        output.append(adaptIssue(issue));
    return output;
}

QVector<ValidationIssue> ManufacturingValidationService::validateBoard(const eda::Board& board) const {
    QVector<ValidationIssue> output;
    const eda::FootprintValidation footprintValidation = [](const eda::Footprint& footprint) {
        return eda::validateFootprint(footprint);
    };
    for (const eda::Issue& issue : eda::validateBoardManufacturing(board, m_resolver, footprintValidation))
        output.append(adaptIssue(issue));
    return output;
}

LayerRoleAdapterModel::LayerRoleAdapterModel(QObject* parent) : QStandardItemModel(parent) {
    setHorizontalHeaderLabels({QObject::tr("Layer")});
}

LayerModelService::LayerModelService(QObject* parent) : QObject(parent) {
    populateFootprintLayers();
}

QAbstractItemModel* LayerModelService::model() const {
    return const_cast<LayerRoleAdapterModel*>(&m_adapter);
}

void LayerModelService::setDocumentContext(DocumentContext* context) {
    if (m_context)
        disconnect(m_context, nullptr, this, nullptr);
    m_context = context;
    if (m_context) {
        connect(m_context, &DocumentContext::modelChanged, this, &LayerModelService::refresh);
    }
    refresh();
}

eda::Status LayerModelService::refresh() {
    if (m_context && m_context->board()) {
        const eda::Board& board = *m_context->board();
        m_adapter.clear();
        m_adapter.setHorizontalHeaderLabels({QObject::tr("Layer")});
        QVector<eda::LayerGroup> groups = board.groups;
        std::sort(groups.begin(), groups.end(), [](const eda::LayerGroup& lhs, const eda::LayerGroup& rhs) {
            if (lhs.stackOrder != rhs.stackOrder)
                return lhs.stackOrder < rhs.stackOrder;
            return lhs.id < rhs.id;
        });
        for (const eda::LayerGroup& group : groups) {
            auto* name = new QStandardItem(group.name);
            auto* type = new QStandardItem(QObject::tr("Group"));
            auto* color = new QStandardItem();
            auto* order = new QStandardItem(QString::number(group.stackOrder));
            for (QStandardItem* item : {name, type, color, order}) {
                item->setEditable(false);
                item->setData(QStringLiteral("group-%1").arg(group.id), ServiceRoles::IdRole);
                item->setData(group.name, ServiceRoles::NameRole);
                item->setData(-1, ServiceRoles::LayerIdRole);
                item->setData(group.id, ServiceRoles::GroupIdRole);
                item->setData(group.stackOrder, ServiceRoles::StackOrderRole);
                item->setData(true, ServiceRoles::IsGroupRole);
            }
            QVector<eda::BoardLayer> layers;
            for (const eda::BoardLayer& layer : board.layers) {
                if (layer.groupId == group.id)
                    layers.append(layer);
            }
            std::sort(layers.begin(), layers.end(), [](const eda::BoardLayer& lhs, const eda::BoardLayer& rhs) {
                if (lhs.stackOrder != rhs.stackOrder)
                    return lhs.stackOrder < rhs.stackOrder;
                return lhs.id < rhs.id;
            });
            for (const eda::BoardLayer& layer : layers) {
                auto* layerName = new QStandardItem(colorIcon(layer.color), layer.name);
                auto* layerType = new QStandardItem(QString::number(static_cast<int>(layer.type)));
                auto* layerColor = new QStandardItem(layer.color.name());
                auto* layerOrder = new QStandardItem(QString::number(layer.stackOrder));
                for (QStandardItem* item : {layerName, layerType, layerColor, layerOrder}) {
                    item->setEditable(false);
                    item->setData(QStringLiteral("layer-%1").arg(layer.id), ServiceRoles::IdRole);
                    item->setData(layer.name, ServiceRoles::NameRole);
                    item->setData(layer.id, ServiceRoles::LayerIdRole);
                    item->setData(layer.color, ServiceRoles::LayerColorRole);
                    item->setData(static_cast<int>(layer.type), ServiceRoles::LayerTypeRole);
                    item->setData(group.id, ServiceRoles::GroupIdRole);
                    item->setData(layer.stackOrder, ServiceRoles::StackOrderRole);
                    item->setData(false, ServiceRoles::IsGroupRole);
                }
                name->appendRow({layerName, layerType, layerColor, layerOrder});
            }
            m_adapter.invisibleRootItem()->appendRow({name, type, color, order});
        }
    } else {
        populateFootprintLayers();
    }
    return eda::Status::success();
}

void LayerModelService::populateFootprintLayers() {
    m_adapter.clear();
    m_adapter.setHorizontalHeaderLabels({QObject::tr("Layer")});
    for (int id = eda::kTopCopper; id <= eda::kBottomMask; ++id) {
        auto* name = new QStandardItem(colorIcon(footprintLayerColor(id)), footprintLayerName(id));
        auto* type = new QStandardItem(QString::number(id));
        auto* color = new QStandardItem(QString());
        auto* order = new QStandardItem(QString::number(id));
        for (QStandardItem* item : {name, type, color, order}) {
            item->setEditable(false);
            item->setData(QStringLiteral("layer-%1").arg(id), ServiceRoles::IdRole);
            item->setData(footprintLayerName(id), ServiceRoles::NameRole);
            item->setData(id, ServiceRoles::LayerIdRole);
            item->setData(-1, ServiceRoles::GroupIdRole);
            item->setData(static_cast<int>(id), ServiceRoles::LayerTypeRole);
            item->setData(footprintLayerColor(id), ServiceRoles::LayerColorRole);
            item->setData(id, ServiceRoles::StackOrderRole);
            item->setData(false, ServiceRoles::IsGroupRole);
        }
        m_adapter.invisibleRootItem()->appendRow({name, type, color, order});
    }
}
