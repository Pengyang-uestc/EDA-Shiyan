#include "app/manufacturing_services.h"

#include "app/document_context.h"
#include "feat_fp/footprint_validation.h"
#include "feat_pcb/board_validation.h"

#include <QIcon>
#include <QPixmap>
#include <QStandardItem>
#include <QString>

namespace {

QIcon colorIcon(const QColor& color)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(color);
    return QIcon(pixmap);
}

QString standardLayerName(eda::LayerId id)
{
    switch (id) {
    case eda::kTopCopper: return QObject::tr("顶层铜");
    case eda::kBottomCopper: return QObject::tr("底层铜");
    case eda::kTopSilk: return QObject::tr("顶层丝印");
    case eda::kBottomSilk: return QObject::tr("底层丝印");
    case eda::kTopMask: return QObject::tr("顶层阻焊");
    case eda::kBottomMask: return QObject::tr("底层阻焊");
    default: return QObject::tr("自定义层");
    }
}

QColor standardLayerColor(eda::LayerId id)
{
    switch (id) {
    case eda::kTopCopper: return QColor(QStringLiteral("#B64845"));
    case eda::kBottomCopper: return QColor(QStringLiteral("#3D70A8"));
    case eda::kTopSilk: return QColor(QStringLiteral("#7A6C27"));
    case eda::kBottomSilk: return QColor(QStringLiteral("#657443"));
    case eda::kTopMask: return QColor(QStringLiteral("#6B8D70"));
    case eda::kBottomMask: return QColor(QStringLiteral("#4D7B7F"));
    default: return QColor(QStringLiteral("#667077"));
    }
}

ValidationIssue toUiIssue(const eda::Issue& issue)
{
    ValidationIssue out;
    out.severity = issue.severity == eda::IssueSeverity::Error ? ValidationSeverity::Error
                                                               : ValidationSeverity::Warning;
    // B 的 ValidationIssue 只有 message，把 C 的错误码带在前面，便于报告与定位。
    out.message = issue.code.isEmpty()
                      ? issue.message
                      : QStringLiteral("[%1] %2").arg(issue.code, issue.message);
    out.padId = issue.padId;
    return out;
}

QVector<ValidationIssue> toUiIssues(const QVector<eda::Issue>& issues)
{
    QVector<ValidationIssue> out;
    out.reserve(issues.size());
    for (const eda::Issue& issue : issues)
        out.append(toUiIssue(issue));
    return out;
}

} // namespace

ManufacturingValidationService::ManufacturingValidationService(eda::FootprintResolver resolver)
    : m_resolver(std::move(resolver))
{
}

QVector<ValidationIssue> ManufacturingValidationService::validateFootprint(
    const eda::Footprint& footprint) const
{
    return toUiIssues(eda::validateFootprint(footprint));
}

QVector<ValidationIssue> ManufacturingValidationService::validateBoard(const eda::Board& board) const
{
    eda::FootprintResolver resolver = m_resolver;
    if (!resolver) {
        resolver = [](const QString& id) {
            eda::Result<eda::Footprint> missing;
            missing.status = eda::Status::failure(QStringLiteral("没有可用的封装库：%1").arg(id));
            return missing;
        };
    }
    return toUiIssues(eda::validateBoardManufacturing(
        board, resolver,
        [](const eda::Footprint& footprint) { return eda::validateFootprint(footprint); }));
}

LayerModelService::LayerModelService() = default;

QAbstractItemModel* LayerModelService::model() const
{
    return &m_flat;
}

void LayerModelService::setDocumentContext(DocumentContext* context)
{
    m_context = context;
    refresh();
}

eda::Status LayerModelService::refresh()
{
    const eda::Board* board = m_context ? m_context->board() : nullptr;
    m_tree.setBoard(board);
    rebuildFlat();
    return eda::Status::success();
}

void LayerModelService::rebuildFlat()
{
    m_flat.clear();
    m_flat.setHorizontalHeaderLabels({QObject::tr("层")});

    if (m_tree.rowCount() == 0) {
        // 封装模式下没有 Board；与 LocalLayerService 保持一致，给标准 7 层占位。
        for (int id = eda::kTopCopper; id <= eda::kBottomMask; ++id) {
            const QColor color = standardLayerColor(id);
            auto* item = new QStandardItem(colorIcon(color), standardLayerName(id));
            item->setData(id, ServiceRoles::IdRole);
            item->setData(standardLayerName(id), ServiceRoles::NameRole);
            item->setData(id, ServiceRoles::LayerIdRole);
            item->setData(color, ServiceRoles::LayerColorRole);
            item->setData(static_cast<int>(eda::BoardLayerType::Copper), ServiceRoles::LayerTypeRole);
            item->setEditable(false);
            m_flat.appendRow(item);
        }
        return;
    }

    for (int groupRow = 0; groupRow < m_tree.rowCount(); ++groupRow) {
        const QModelIndex group = m_tree.index(groupRow, 0);
        const QString groupName = group.data(Qt::DisplayRole).toString();
        const int layers = m_tree.rowCount(group);
        for (int layerRow = 0; layerRow < layers; ++layerRow) {
            const QModelIndex layer = m_tree.index(layerRow, 0, group);
            const int id = layer.data(eda::LayerModel::LayerIdRole).toInt();
            const QString name = layer.data(Qt::DisplayRole).toString();
            const QString label = groupName.isEmpty() ? name
                                                      : groupName + QStringLiteral(" / ") + name;
            const QColor color = layer.data(eda::LayerModel::ColorRole).value<QColor>();
            auto* item = new QStandardItem(colorIcon(color), label);
            item->setData(id, ServiceRoles::IdRole);
            item->setData(label, ServiceRoles::NameRole);
            item->setData(id, ServiceRoles::LayerIdRole);
            item->setData(color, ServiceRoles::LayerColorRole);
            item->setData(layer.data(eda::LayerModel::LayerTypeRole), ServiceRoles::LayerTypeRole);
            item->setEditable(false);
            m_flat.appendRow(item);
        }
    }
}