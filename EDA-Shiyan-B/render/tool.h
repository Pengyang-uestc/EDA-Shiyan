#pragma once

#include <QCursor>
#include <QKeyEvent>
#include <QPainter>
#include <QPointF>
#include <QTransform>
#include <Qt>

class DocumentContext;

struct ToolEvent {
    QPointF worldMil;
    Qt::MouseButton button = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    bool snapped = false;
};

struct CanvasView {
    QTransform worldToScreen;

    QPointF toScreen(QPointF worldMil) const {
        return worldToScreen.map(worldMil);
    }
    QPointF toWorld(QPointF screen) const {
        return worldToScreen.inverted().map(screen);
    }
    qreal scale() const {
        return worldToScreen.m11();
    }
};

class ITool {
  public:
    virtual ~ITool() = default;

    virtual void activate(DocumentContext* context) = 0;
    virtual void deactivate() = 0;

    virtual void mousePress(const ToolEvent& event) = 0;
    virtual void mouseMove(const ToolEvent& event) = 0;
    virtual void mouseRelease(const ToolEvent& event) = 0;
    virtual void keyPress(QKeyEvent* event) = 0;

    virtual void paintOverlay(QPainter* painter, const CanvasView& view) const = 0;
    virtual QCursor cursor() const = 0;
    virtual void cancel() = 0;
};
