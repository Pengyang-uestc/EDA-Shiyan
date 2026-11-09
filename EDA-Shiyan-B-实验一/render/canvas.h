#pragma once

#include "app/document_context.h"
#include "core/board.h"
#include "core/footprint.h"
#include "render/tool.h"

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <memory>
#include <optional>
#include <functional>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWheelEvent;

class CanvasWidget : public QWidget {
    Q_OBJECT

  public:
    enum class EditorMode { Select, AddSilkLine, AddSilkRectangle, AddSilkPolygon, AddSilkText };
    Q_ENUM(EditorMode)

    explicit CanvasWidget(QWidget* parent = nullptr);
    ~CanvasWidget() override;

    void setDocumentContext(DocumentContext* context);
    DocumentContext* documentContext() const;

    void setTool(std::unique_ptr<ITool> tool);
    void clearTool();
    ITool* tool() const;

    void setEditorMode(EditorMode mode);
    EditorMode editorMode() const;

    void setActiveLayer(eda::LayerId id);
    using FootprintLookup = std::function<eda::Result<eda::Footprint>(const QString&)>;
    void setFootprintResolver(FootprintLookup resolver);
    QPointF screenToWorld(QPointF screenPoint) const;
    QPointF worldToScreen(QPointF worldMil) const;
    CanvasView canvasView() const;
    QRectF canvasViewport() const;
    QRectF contentBounds() const;
    eda::PadId padAtScreen(QPointF screenPoint) const;
    ObjectRef boardObjectAtScreen(QPointF screenPoint) const;

    double zoom() const;
    void zoomIn();
    void zoomOut();
    void fitToView();
    void setRulersVisible(bool visible);

  signals:
    void cursorWorldPositionChanged(const QPointF& worldMil);
    void zoomChanged(double zoom);
    void selectionCountChanged(int count);
    void footprintEditRequested(const eda::Footprint& before, const eda::Footprint& after, const QString& text);
    void boardEditRequested(const eda::Board& before, const eda::Board& after, const QString& text);
    void textPlacementRequested(const QPointF& worldMil);
    void deleteSelectionRequested();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    QTransform worldTransform() const;
    QPointF unsnappedWorld(QPointF screenPoint) const;
    QPointF snappedWorld(QPointF screenPoint, bool* snapped = nullptr) const;
    void drawGrid(QPainter& painter, const QRectF& viewport);
    void drawRulers(QPainter& painter, const QRectF& viewport);
    void drawFootprint(QPainter& painter, const eda::Footprint& footprint, const QRectF& viewport);
    void drawBoard(QPainter& painter, const eda::Board& board, const QRectF& viewport);
    void drawBoardInstance(QPainter& painter, const eda::Board& board, const eda::Instance& instance);
    void drawBoardSelection(QPainter& painter, const eda::Board& board);
    void drawSilk(QPainter& painter, const eda::Footprint& footprint);
    void drawPad(QPainter& painter, const eda::Footprint& footprint, const eda::Pad& pad);
    void drawSelection(QPainter& painter, const eda::Footprint& footprint);
    void drawEditorPreview(QPainter& painter);
    void drawEmptyState(QPainter& painter, const QRectF& viewport);

    QPainterPath padPath(const eda::Pad& pad) const;
    QColor layerColor(eda::LayerId layer) const;
    QColor boardLayerColor(const eda::Board& board, eda::LayerId layer) const;
    QString layerName(eda::LayerId layer) const;
    QString boardLayerName(const eda::Board& board, eda::LayerId layer) const;
    bool layerVisible(eda::LayerId layer) const;
    bool selectedPad(eda::PadId id) const;
    QRectF padScreenRect(const eda::Pad& pad) const;
    QRectF footprintBounds(const eda::Footprint& footprint) const;
    QRectF boardBounds(const eda::Board& board) const;
    QRectF instanceBounds(const eda::Board& board, const eda::Instance& instance) const;
    const eda::Footprint* resolvedFootprint(const QString& id) const;
    QPointF instancePoint(const eda::Instance& instance, QPointF localMil) const;
    void finishPolygon();
    void cancelInteraction();
    void zoomAround(QPointF screenPoint, double factor);
    void updateStatusText();

    DocumentContext* m_context = nullptr;
    std::unique_ptr<ITool> m_tool;
    EditorMode m_mode = EditorMode::Select;

    double m_scale = 2.4;
    QPointF m_centerWorld{0, 0};
    QPointF m_cursorWorld{0, 0};
    QPointF m_lastMouseScreen;
    bool m_rulersVisible = true;
    bool m_hasFit = false;

    bool m_panning = false;
    bool m_spacePressed = false;
    bool m_draggingPad = false;
    bool m_draggingBoardObject = false;
    bool m_resizingPad = false;
    bool m_dragMoved = false;
    bool m_rubberBandActive = false;
    bool m_addPrimitiveActive = false;
    QPointF m_dragStartWorld;
    QSizeF m_resizeStartSize;
    QPointF m_primitiveStart;
    QPointF m_rubberStart;
    QPointF m_rubberCurrent;
    QVector<ObjectRef> m_rubberBaseSelection;
    std::optional<eda::Footprint> m_dragSnapshot;
    std::optional<eda::Footprint> m_previewFootprint;
    std::optional<eda::Board> m_boardDragSnapshot;
    std::optional<eda::Board> m_boardPreview;
    QVector<QPointF> m_polygonPoints;
    FootprintLookup m_footprintResolver;
    mutable QHash<QString, eda::Footprint> m_footprintCache;
};
