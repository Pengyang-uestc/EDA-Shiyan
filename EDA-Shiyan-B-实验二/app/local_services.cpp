#include "app/local_services.h"

#include "app/document_context.h"
#include "core/io.h"
#include "core/library.h"

#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QStandardItem>

#include <utility>
#include <algorithm>

namespace {
QIcon colorIcon(const QColor& color) {
    QPixmap pixmap(14, 14);
    pixmap.fill(color);
    return QIcon(pixmap);
}

QString layerName(eda::LayerId id) {
    switch (id) {
    case eda::kTopCopper:
        return QObject::tr("顶层铜");
    case eda::kBottomCopper:
        return QObject::tr("底层铜");
    case eda::kTopSilk:
        return QObject::tr("顶层丝印");
    case eda::kBottomSilk:
        return QObject::tr("底层丝印");
    case eda::kTopMask:
        return QObject::tr("顶层阻焊");
    case eda::kBottomMask:
        return QObject::tr("底层阻焊");
    default:
        return QObject::tr("自定义层");
    }
}

QColor layerColor(eda::LayerId id) {
    switch (id) {
    case eda::kTopCopper:
        return QColor(QStringLiteral("#B64845"));
    case eda::kBottomCopper:
        return QColor(QStringLiteral("#3D70A8"));
    case eda::kTopSilk:
        return QColor(QStringLiteral("#7A6C27"));
    case eda::kBottomSilk:
        return QColor(QStringLiteral("#657443"));
    case eda::kTopMask:
        return QColor(QStringLiteral("#6B8D70"));
    case eda::kBottomMask:
        return QColor(QStringLiteral("#4D7B7F"));
    default:
        return QColor(QStringLiteral("#667077"));
    }
}
} // namespace

LocalLibraryService::LocalLibraryService(QString root) : m_root(QDir(std::move(root)).absolutePath()) {}

QAbstractItemModel* LocalLibraryService::model() const {
    return const_cast<QStandardItemModel*>(&m_model);
}

eda::Status LocalLibraryService::refresh() {
    eda::FootprintLibrary library(m_root);
    const auto listed = library.list();
    if (!listed.status.ok)
        return listed.status;
    m_model.clear();
    m_model.setHorizontalHeaderLabels({QObject::tr("封装")});
    for (const auto& entry : *listed.value) {
        auto* item = new QStandardItem(entry.name.isEmpty() ? entry.id : entry.name);
        item->setData(entry.id, ServiceRoles::IdRole);
        item->setData(entry.name, ServiceRoles::NameRole);
        item->setToolTip(QObject::tr("ID：%1\n双击打开封装").arg(entry.id));
        item->setEditable(false);
        m_model.appendRow(item);
    }
    return eda::Status::success();
}

eda::Result<eda::Footprint> LocalLibraryService::load(const QString& id) const {
    eda::FootprintLibrary library(m_root);
    return library.load(id);
}

eda::Status LocalLibraryService::save(const eda::Footprint& footprint) {
    eda::FootprintLibrary library(m_root);
    return library.save(footprint);
}

eda::Status LocalLibraryService::importFile(const QString& sourcePath) {
    const auto loaded = eda::loadFootprint(sourcePath);
    if (!loaded.status.ok || !loaded.value.has_value())
        return loaded.status;
    const auto saved = save(*loaded.value);
    if (!saved.ok)
        return saved;
    eda::FootprintLibrary library(m_root);
    return library.rebuildIndex();
}

eda::Status LocalLibraryService::remove(const QString& id) {
    eda::FootprintLibrary library(m_root);
    const auto removed = library.remove(id);
    if (!removed.ok)
        return removed;
    return library.rebuildIndex();
}

QString LocalLibraryService::rootPath() const {
    return m_root;
}

LocalLayerService::LocalLayerService() {
    refresh();
}

QAbstractItemModel* LocalLayerService::model() const {
    return const_cast<QStandardItemModel*>(&m_model);
}

void LocalLayerService::setDocumentContext(DocumentContext* context) {
    m_context = context;
}

eda::Status LocalLayerService::refresh() {
    m_model.clear();
    m_model.setHorizontalHeaderLabels({QObject::tr("层")});

    if (m_context && m_context->board()) {
        const eda::Board& board = *m_context->board();
        QHash<int, int> groupOrder;
        QHash<int, QString> groupNames;
        for (const auto& group : board.groups) {
            groupOrder.insert(group.id, group.stackOrder);
            groupNames.insert(group.id, group.name);
        }
        QVector<eda::BoardLayer> layers = board.layers;
        std::sort(layers.begin(), layers.end(), [&](const eda::BoardLayer& lhs, const eda::BoardLayer& rhs) {
            const int lhsGroup = groupOrder.value(lhs.groupId, 0);
            const int rhsGroup = groupOrder.value(rhs.groupId, 0);
            if (lhsGroup != rhsGroup)
                return lhsGroup < rhsGroup;
            if (lhs.stackOrder != rhs.stackOrder)
                return lhs.stackOrder < rhs.stackOrder;
            return lhs.id < rhs.id;
        });
        for (const auto& layer : layers) {
            const QString prefix = groupNames.value(layer.groupId);
            const QString label = prefix.isEmpty() ? layer.name : prefix + QStringLiteral(" / ") + layer.name;
            auto* item = new QStandardItem(colorIcon(layer.color), label);
            item->setData(layer.id, ServiceRoles::LayerIdRole);
            item->setData(label, ServiceRoles::NameRole);
            item->setData(layer.color, ServiceRoles::LayerColorRole);
            item->setData(static_cast<int>(layer.type), ServiceRoles::LayerTypeRole);
            item->setEditable(false);
            m_model.appendRow(item);
        }
        return eda::Status::success();
    }

    for (int id = eda::kTopCopper; id <= eda::kBottomMask; ++id) {
        auto* item = new QStandardItem(colorIcon(layerColor(id)), layerName(id));
        item->setData(id, ServiceRoles::LayerIdRole);
        item->setData(layerName(id), ServiceRoles::NameRole);
        item->setData(layerColor(id), ServiceRoles::LayerColorRole);
        item->setData(static_cast<int>(id), ServiceRoles::LayerTypeRole);
        item->setEditable(false);
        m_model.appendRow(item);
    }
    return eda::Status::success();
}

QVector<ValidationIssue> StructuralValidationService::validateFootprint(const eda::Footprint& footprint) const {
    const auto status = eda::validateStructure(footprint);
    if (status.ok)
        return {};
    return {ValidationIssue{ValidationSeverity::Error, status.message, eda::kInvalidId}};
}

QVector<ValidationIssue> StructuralValidationService::validateBoard(const eda::Board& board) const {
    const auto status = eda::validateBoardStructure(board);
    if (status.ok)
        return {};
    return {ValidationIssue{ValidationSeverity::Error, status.message, eda::kInvalidId}};
}

eda::Status seedExampleIfEmpty(LocalLibraryService& library) {
    const auto refreshed = library.refresh();
    if (!refreshed.ok)
        return refreshed;
    if (library.model()->rowCount() > 0)
        return eda::Status::success();
    const auto saved = library.save(eda::exampleFootprint());
    if (!saved.ok)
        return saved;
    return library.refresh();
}
