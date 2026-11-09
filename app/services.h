#pragma once

#include "core/board.h"
#include "core/footprint.h"
#include "core/status.h"

#include <QAbstractItemModel>
#include <QString>
#include <QVector>

class DocumentContext;

namespace ServiceRoles {
enum Role {
    IdRole = Qt::UserRole + 1,
    NameRole,
    LayerIdRole,
    LayerColorRole,
    LayerTypeRole,
};
}

enum class ValidationSeverity { Error, Warning };

struct ValidationIssue {
    ValidationSeverity severity = ValidationSeverity::Error;
    QString message;
    eda::PadId padId = eda::kInvalidId;
};

// UI thread. Panels only consume these interfaces; concrete C services are injected by app bootstrap.
class ILibraryService {
  public:
    virtual ~ILibraryService() = default;
    virtual QAbstractItemModel* model() const = 0;
    virtual eda::Status refresh() = 0;
    virtual eda::Result<eda::Footprint> load(const QString& id) const = 0;
    virtual eda::Status save(const eda::Footprint& footprint) = 0;
    virtual eda::Status importFile(const QString& sourcePath) = 0;
    virtual eda::Status remove(const QString& id) = 0;
    virtual QString rootPath() const = 0;
};

class ILayerService {
  public:
    virtual ~ILayerService() = default;
    virtual QAbstractItemModel* model() const = 0;
    virtual void setDocumentContext(DocumentContext* context) = 0;
    virtual eda::Status refresh() = 0;
};

class IValidationService {
  public:
    virtual ~IValidationService() = default;
    virtual QVector<ValidationIssue> validateFootprint(const eda::Footprint& footprint) const = 0;
    virtual QVector<ValidationIssue> validateBoard(const eda::Board& board) const = 0;
};
