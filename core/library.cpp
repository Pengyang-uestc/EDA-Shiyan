#include "library.h"
#include "io_internal.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QRegularExpression>
namespace eda {
namespace {
bool fail(QString* e, const QString& s) {
    if (e)
        *e = s;
    return false;
}
bool safeDirectory(const QString& root, QString* e) {
    QFileInfo cursor(root);
    while (true) {
        if (cursor.isSymLink())
            return fail(e, QStringLiteral("库目录不能经过符号链接"));
        auto parent = cursor.dir().absolutePath();
        if (parent == cursor.absoluteFilePath())
            break;
        cursor.setFile(parent);
    }
    return true;
}
} // namespace
FootprintLibrary::FootprintLibrary(QString root) : m_root(QDir(root).absolutePath()) {}
bool FootprintLibrary::pathFor(const QString& id, QString& path, QString* error) const {
    if (error)
        error->clear();
    // Lowercase portable IDs avoid case collisions across Windows/Linux.
    if (!QRegularExpression("^[a-z0-9][a-z0-9_-]{0,63}$").match(id).hasMatch())
        return fail(error, QStringLiteral("库 ID 必须为 1～64 位小写字母、数字、下划线或短横线，首位为字母或数字"));
    if (QRegularExpression("^(con|prn|aux|nul|com[0-9]|lpt[0-9])$").match(id).hasMatch())
        return fail(error, QStringLiteral("库 ID 使用了 Windows 保留文件名"));
    if (!safeDirectory(m_root, error))
        return false;
    path = QDir(m_root).filePath(QStringLiteral("footprints/") + id + ".fp");
    if (!safeDirectory(QFileInfo(path).absolutePath(), error))
        return false;
    if (QFileInfo(path).isSymLink())
        return fail(error, QStringLiteral("不允许将符号链接作为封装文件"));
    return true;
}
bool FootprintLibrary::loadImpl(const QString& id, Footprint& output, QString* error) const {
    QString path;
    if (!pathFor(id, path, error))
        return false;
    Footprint temp;
    if (!detail::loadFootprintImpl(path, temp, error))
        return false;
    if (temp.id != id)
        return fail(error, QStringLiteral("文件名与内部封装 ID 不一致"));
    output = std::move(temp);
    return true;
}
bool FootprintLibrary::saveImpl(const Footprint& f, QString* error) {
    QString path;
    if (!pathFor(f.id, path, error) || !detail::validateStructureImpl(f, error))
        return false;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(error, QStringLiteral("无法创建元件库目录"));
    return detail::saveFootprintImpl(path, f, error);
}
bool FootprintLibrary::removeImpl(const QString& id, QString* error) {
    QString path;
    if (!pathFor(id, path, error))
        return false;
    QFile file(path);
    if (!file.remove())
        return fail(error, file.errorString());
    return true;
}
bool FootprintLibrary::listImpl(QVector<LibraryEntry>& output, QString* error) const {
    if (error)
        error->clear();
    if (!safeDirectory(m_root, error))
        return false;
    const QFileInfo info(m_root);
    if (info.exists() && (!info.isDir() || !info.isReadable()))
        return fail(error, QStringLiteral("元件库目录不可读"));
    QVector<LibraryEntry> rows;
    for (const auto& file : QDir(QDir(m_root).filePath("footprints"))
                                .entryInfoList({"*.fp"}, QDir::Files | QDir::Hidden | QDir::System, QDir::Name)) {
        Footprint fp;
        if (!loadImpl(file.completeBaseName(), fp, error)) {
            if (error)
                *error = file.fileName() + ": " + *error;
            return false;
        }
        rows.append({fp.id, fp.name, QStringLiteral("footprints/") + file.fileName()});
    }
    output = std::move(rows);
    return true;
}
bool FootprintLibrary::rebuildIndexImpl(QString* error) const {
    QVector<LibraryEntry> entries;
    if (!listImpl(entries, error))
        return false;
    if (!QDir().mkpath(m_root))
        return fail(error, QStringLiteral("无法创建元件库目录"));
    auto path = QDir(m_root).filePath("index.idx");
    if (QFileInfo(path).isSymLink())
        return fail(error, QStringLiteral("不允许将符号链接作为库索引"));
    QByteArray bytes("EDA-LIB 1\n");
    auto quote = [](const QString& value) {
        QString escaped = value;
        escaped.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n");
        return QStringLiteral("\"") + escaped + '\"';
    };
    for (const auto& e : entries)
        bytes += QStringLiteral("footprint %1 %2 %3\n").arg(e.id, quote(e.name), quote(e.relativePath)).toUtf8();
    bytes += "end\n";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, file.errorString());
    return true;
}
} // namespace eda
