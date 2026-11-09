#pragma once

#include "app/services.h"

#include <QStandardItemModel>

// Local adapters keep the GUI runnable before C supplies manufacturing services.
// They are injected through the same interfaces and can be replaced without changing panels.
class LocalLibraryService : public ILibraryService {
  public:
    explicit LocalLibraryService(QString root);

    QAbstractItemModel* model() const override;
    eda::Status refresh() override;
    eda::Result<eda::Footprint> load(const QString& id) const override;
    eda::Status save(const eda::Footprint& footprint) override;
    eda::Status importFile(const QString& sourcePath) override;
    eda::Status remove(const QString& id) override;
    QString rootPath() const override;

  private:
    QString m_root;
    mutable QStandardItemModel m_model;
};

class LocalLayerService : public ILayerService {
  public:
    LocalLayerService();

    QAbstractItemModel* model() const override;
    void setDocumentContext(DocumentContext* context) override;
    eda::Status refresh() override;

  private:
    DocumentContext* m_context = nullptr;
    mutable QStandardItemModel m_model;
};

class StructuralValidationService : public IValidationService {
  public:
    QVector<ValidationIssue> validateFootprint(const eda::Footprint& footprint) const override;
    QVector<ValidationIssue> validateBoard(const eda::Board& board) const override;
};

// UI thread; creates the example once when the injected library is empty.
eda::Status seedExampleIfEmpty(LocalLibraryService& library);
