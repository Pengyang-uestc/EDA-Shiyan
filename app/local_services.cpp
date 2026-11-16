#include "app/local_services.h"

#include "core/io.h"
#include "core/library.h"

#include <QDir>
#include <QStandardItem>

#include <utility>

namespace {
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
