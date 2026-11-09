#pragma once

#include "core/status.h"
#include "core/types.h"

#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

namespace eda {
struct Board;
struct Footprint;
} // namespace eda

class EdaCommand;

enum class ObjectType { None, Pad, Pin, Instance, Track, Via, Pour };

struct ObjectRef {
    ObjectType type = ObjectType::None;
    eda::PadId padId = eda::kInvalidId;
    QString refdes;
    quint64 objectId = 0;

    bool operator==(const ObjectRef& other) const {
        return type == other.type && padId == other.padId && refdes == other.refdes && objectId == other.objectId;
    }
};

// UI thread. This object owns session state only; document data lives in Document.
class DocumentContext : public QObject {
    Q_OBJECT

  public:
    explicit DocumentContext(QObject* parent = nullptr);
    ~DocumentContext() override;

    virtual eda::Footprint* footprint() = 0;
    virtual eda::Board* board() = 0;

    virtual eda::LayerId activeLayer() const = 0;
    virtual void setActiveLayer(eda::LayerId id) = 0;
    virtual bool isLayerVisible(eda::LayerId id) const = 0;
    virtual void setLayerVisible(eda::LayerId id, bool visible) = 0;

    virtual double gridMil() const = 0;
    virtual void setGridMil(double mil) = 0;
    virtual bool snapEnabled() const = 0;
    virtual void setSnapEnabled(bool enabled) = 0;
    virtual QPointF snap(QPointF worldMil) const = 0;

    virtual QVector<ObjectRef> selection() const = 0;
    virtual void setSelection(const QVector<ObjectRef>& refs) = 0;

    // Ownership transfer. On failure the implementation deletes command and leaves the stack unchanged.
    virtual eda::Status pushCommand(EdaCommand* command) = 0;
    virtual void requestRepaint() = 0;
    virtual void showStatus(const QString& text) = 0;

  signals:
    void activeLayerChanged(eda::LayerId id);
    void layerVisibilityChanged(eda::LayerId id, bool visible);
    void selectionChanged();
    void modelChanged();
    void repaintRequested();
    void statusMessageRequested(const QString& text);
};
