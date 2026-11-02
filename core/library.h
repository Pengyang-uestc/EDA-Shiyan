#pragma once
#include "io.h"
namespace eda {
struct LibraryEntry {
    QString id, name, relativePath;
};
class FootprintLibrary {
  public:
    explicit FootprintLibrary(QString root);
    // UI thread; caller owns returned values; no model mutation; Chinese Status errors.
    [[nodiscard]] Result<QVector<LibraryEntry>> list() const;
    [[nodiscard]] Result<Footprint> load(const QString& id) const;
    [[nodiscard]] Status save(const Footprint& footprint);
    [[nodiscard]] Status remove(const QString& id);
    [[nodiscard]] Status rebuildIndex() const;

  private:
    bool listImpl(QVector<LibraryEntry>&, QString*) const;
    bool loadImpl(const QString&, Footprint&, QString*) const;
    bool saveImpl(const Footprint&, QString*);
    bool removeImpl(const QString&, QString*);
    bool rebuildIndexImpl(QString*) const;
    QString m_root;
    bool pathFor(const QString& id, QString& path, QString* error) const;
};
} // namespace eda
