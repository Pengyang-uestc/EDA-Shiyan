#pragma once

#include "app/services.h"
#include "core/io.h"
#include <QObject>
#include <QStandardItemModel>

// C-backed services used by the GUI. The adapters keep ServiceRoles stable while
// C keeps ownership of manufacturing rules and layer model semantics.
class ManufacturingValidationService : public IValidationService {
  public:
    explicit ManufacturingValidationService(eda::FootprintResolver resolver);

    QVector<ValidationIssue> validateFootprint(const eda::Footprint& footprint) const override;
    QVector<ValidationIssue> validateBoard(const eda::Board& board) const override;

  private:
    eda::FootprintResolver m_resolver;
};

class LayerRoleAdapterModel : public QStandardItemModel {
  public:
    explicit LayerRoleAdapterModel(QObject* parent = nullptr);
};

class LayerModelService : public QObject, public ILayerService {
    Q_OBJECT
  public:
    explicit LayerModelService(QObject* parent = nullptr);

    QAbstractItemModel* model() const override;
    void setDocumentContext(DocumentContext* context) override;
    eda::Status refresh() override;

  private:
    void populateFootprintLayers();

    DocumentContext* m_context = nullptr;
    mutable LayerRoleAdapterModel m_adapter;
};
