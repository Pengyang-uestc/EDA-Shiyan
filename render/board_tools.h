#pragma once

#include "core/io.h"
#include "render/tool.h"

class Document;

class BoardTool : public ITool {
  public:
    void activate(DocumentContext* context) override;
    void deactivate() override;
    void mouseRelease(const ToolEvent&) override {}
    void mouseMove(const ToolEvent&) override {}
    void keyPress(QKeyEvent* event) override;
    void paintOverlay(QPainter*, const CanvasView&) const override {}
    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }
    void cancel() override {}

  protected:
    Document* document = nullptr;
    bool copperLayer() const;
    bool commit(eda::Board after, const QString& label);
};

class PlaceTool : public BoardTool {
  public:
    explicit PlaceTool(eda::FootprintResolver resolver);
    void mousePress(const ToolEvent& event) override;

  private:
    eda::FootprintResolver m_resolver;
};

class RouteTool : public BoardTool {
  public:
    void mousePress(const ToolEvent& event) override;
    void mouseMove(const ToolEvent& event) override;
    void paintOverlay(QPainter* painter, const CanvasView& view) const override;
    void cancel() override;

  private:
    std::optional<QPointF> m_start;
    QPointF m_cursor;
    eda::LayerId m_layer = eda::kTopCopper;
};

class PourTool : public BoardTool {
  public:
    void mousePress(const ToolEvent& event) override;
    void mouseMove(const ToolEvent& event) override;
    void keyPress(QKeyEvent* event) override;
    void paintOverlay(QPainter* painter, const CanvasView& view) const override;
    void cancel() override;

  private:
    void finish();
    QPolygonF m_points;
    QPointF m_cursor;
    eda::LayerId m_layer = eda::kTopCopper;
};
