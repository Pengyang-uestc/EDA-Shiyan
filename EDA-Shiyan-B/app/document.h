#pragma once

#include "app/document_context.h"
#include "core/board.h"
#include "core/footprint.h"

#include <QSet>
#include <QString>
#include <QUndoStack>

// UI thread. Owns one footprint document and its undo history.
class Document : public DocumentContext {
    Q_OBJECT

  public:
    enum class Mode { Footprint, Board };

    explicit Document(QObject* parent = nullptr);

    eda::Footprint* footprint() override;
    eda::Board* board() override;

    eda::LayerId activeLayer() const override;
    void setActiveLayer(eda::LayerId id) override;
    bool isLayerVisible(eda::LayerId id) const override;
    void setLayerVisible(eda::LayerId id, bool visible) override;

    double gridMil() const override;
    void setGridMil(double mil) override;
    bool snapEnabled() const override;
    void setSnapEnabled(bool enabled) override;
    QPointF snap(QPointF worldMil) const override;

    QVector<ObjectRef> selection() const override;
    void setSelection(const QVector<ObjectRef>& refs) override;

    eda::Status pushCommand(EdaCommand* command) override;
    void requestRepaint() override;
    void showStatus(const QString& text) override;

    // New/load bypass undo and clear history only after successful construction.
    void installFootprint(eda::Footprint footprint, const QString& path = {});
    void installBoard(eda::Board board, const QString& path = {});
    const eda::Footprint& footprintData() const;
    const eda::Board& boardData() const;
    Mode mode() const;
    QString filePath() const;
    void setFilePath(const QString& path);

    bool canUndo() const;
    bool canRedo() const;
    bool isClean() const;
    void markClean();
    void undo();
    void redo();
    void clearUndoHistory();

    // Called by commands after changing the owned model.
    void notifyModelChanged();

  signals:
    void documentReset();
    void documentPathChanged(const QString& path);
    void undoAvailabilityChanged(bool canUndo, bool canRedo);
    void cleanChanged(bool clean);

  private:
    void pruneSelection();
    eda::Footprint m_footprint;
    eda::Board m_board;
    Mode m_mode = Mode::Footprint;
    QUndoStack m_undoStack;
    QVector<ObjectRef> m_selection;
    QSet<eda::LayerId> m_visibleLayers;
    eda::LayerId m_activeLayer = eda::kTopCopper;
    double m_gridMil = 100.0;
    bool m_snapEnabled = true;
    QString m_filePath;
};
