#include "render/canvas.h"

#include "core/geometry.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr double kRulerSize = 28.0;
constexpr double kMinScale = 0.15;
constexpr double kMaxScale = 40.0;
constexpr double kFitMarginMil = 100.0;

QPointF eventPosition(const QMouseEvent* event) {
    return event->position();
}

QColor withAlpha(QColor color, int alpha) {
    color.setAlpha(alpha);
    return color;
}
} // namespace

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("canvasWidget"));
    setMinimumSize(520, 360);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::ArrowCursor);
}

CanvasWidget::~CanvasWidget() {
    if (m_tool)
        m_tool->deactivate();
}

void CanvasWidget::setDocumentContext(DocumentContext* context) {
    if (m_context == context)
        return;
    if (m_context)
        disconnect(m_context, nullptr, this, nullptr);
    m_context = context;
    m_hasFit = false;
    if (m_context) {
        connect(m_context, &DocumentContext::repaintRequested, this, [this] { update(); });
        connect(m_context, &DocumentContext::modelChanged, this, [this] {
            m_footprintCache.clear();
            update();
        });
        connect(m_context, &DocumentContext::selectionChanged, this, [this] {
            updateStatusText();
            update();
        });
        connect(m_context, &DocumentContext::activeLayerChanged, this, [this] {
            updateStatusText();
            update();
        });
        connect(m_context, &DocumentContext::layerVisibilityChanged, this, [this] { update(); });
        QTimer::singleShot(0, this, [this] { fitToView(); });
    }
    update();
}

DocumentContext* CanvasWidget::documentContext() const {
    return m_context;
}

void CanvasWidget::setTool(std::unique_ptr<ITool> tool) {
    if (m_tool)
        m_tool->deactivate();
    m_tool = std::move(tool);
    if (m_tool) {
        m_tool->activate(m_context);
        setCursor(m_tool->cursor());
    } else {
        setCursor(m_mode == EditorMode::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    }
    update();
}

void CanvasWidget::clearTool() {
    setTool(nullptr);
}

ITool* CanvasWidget::tool() const {
    return m_tool.get();
}

void CanvasWidget::setEditorMode(EditorMode mode) {
    if (m_mode == mode)
        return;
    cancelInteraction();
    m_mode = mode;
    setCursor(mode == EditorMode::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

CanvasWidget::EditorMode CanvasWidget::editorMode() const {
    return m_mode;
}

void CanvasWidget::setActiveLayer(eda::LayerId id) {
    if (m_context)
        m_context->setActiveLayer(id);
}

void CanvasWidget::setFootprintResolver(FootprintLookup resolver) {
    m_footprintResolver = std::move(resolver);
    m_footprintCache.clear();
    update();
}

QRectF CanvasWidget::canvasViewport() const {
    const double left = m_rulersVisible ? kRulerSize : 0.0;
    const double top = m_rulersVisible ? kRulerSize : 0.0;
    return QRectF(rect()).adjusted(left, top, -1.0, -1.0);
}

QTransform CanvasWidget::worldTransform() const {
    const QRectF viewport = canvasViewport();
    const QPointF screenCenter = viewport.center();
    return QTransform(m_scale, 0.0, 0.0, -m_scale, screenCenter.x() - m_scale * m_centerWorld.x(),
                      screenCenter.y() + m_scale * m_centerWorld.y());
}

CanvasView CanvasWidget::canvasView() const {
    CanvasView view;
    view.worldToScreen = worldTransform();
    return view;
}

QPointF CanvasWidget::screenToWorld(QPointF screenPoint) const {
    return worldTransform().inverted().map(screenPoint);
}

QPointF CanvasWidget::worldToScreen(QPointF worldMil) const {
    return worldTransform().map(worldMil);
}

QPointF CanvasWidget::unsnappedWorld(QPointF screenPoint) const {
    return screenToWorld(screenPoint);
}

QPointF CanvasWidget::snappedWorld(QPointF screenPoint, bool* snapped) const {
    const QPointF raw = unsnappedWorld(screenPoint);
    if (!m_context || !m_context->snapEnabled()) {
        if (snapped)
            *snapped = false;
        return raw;
    }
    if (snapped)
        *snapped = true;
    return m_context->snap(raw);
}

double CanvasWidget::zoom() const {
    return m_scale;
}

void CanvasWidget::zoomIn() {
    zoomAround(canvasViewport().center(), 1.24);
}

void CanvasWidget::zoomOut() {
    zoomAround(canvasViewport().center(), 1.0 / 1.24);
}

void CanvasWidget::zoomAround(QPointF screenPoint, double factor) {
    const QPointF anchor = screenToWorld(screenPoint);
    const double newScale = std::clamp(m_scale * factor, kMinScale, kMaxScale);
    if (qFuzzyCompare(newScale, m_scale))
        return;
    m_scale = newScale;
    const QPointF after = screenToWorld(screenPoint);
    m_centerWorld += anchor - after;
    emit zoomChanged(m_scale);
    updateStatusText();
    update();
}

QRectF CanvasWidget::footprintBounds(const eda::Footprint& footprint) const {
    bool initialized = false;
    QRectF bounds;
    auto include = [&](const QRectF& rect) {
        if (!initialized) {
            bounds = rect;
            initialized = true;
        } else {
            bounds = bounds.united(rect);
        }
    };
    for (const auto& pad : footprint.pads) {
        const double extent =
            qMax(pad.size.width(), pad.size.height()) / 2.0 + qMax(pad.maskMargin, pad.clearance) + 2.0;
        include(QRectF(pad.pos.x() - extent, pad.pos.y() - extent, extent * 2.0, extent * 2.0));
    }
    for (const auto& hole : footprint.holes) {
        const double extent = hole.drill / 2.0 + hole.keepout + 2.0;
        include(QRectF(hole.pos.x() - extent, hole.pos.y() - extent, extent * 2.0, extent * 2.0));
    }
    for (const auto& primitive : footprint.silk) {
        if (!primitive)
            continue;
        switch (primitive->kind()) {
        case eda::PrimitiveKind::Line: {
            const auto& line = dynamic_cast<const eda::SilkLine&>(*primitive);
            include(QRectF(line.a, line.b).normalized());
            break;
        }
        case eda::PrimitiveKind::Arc: {
            const auto& arc = dynamic_cast<const eda::SilkArc&>(*primitive);
            include(
                QRectF(arc.center.x() - arc.radius, arc.center.y() - arc.radius, arc.radius * 2.0, arc.radius * 2.0));
            break;
        }
        case eda::PrimitiveKind::Rect:
            include(dynamic_cast<const eda::SilkRect&>(*primitive).rect.normalized());
            break;
        case eda::PrimitiveKind::Polygon:
            include(dynamic_cast<const eda::SilkPolygon&>(*primitive).points.boundingRect());
            break;
        case eda::PrimitiveKind::Text: {
            const auto& text = dynamic_cast<const eda::SilkText&>(*primitive);
            include(
                QRectF(text.pos.x(), text.pos.y(), text.height * qMax<qsizetype>(1, text.text.size()), text.height));
            break;
        }
        }
    }
    if (!initialized)
        return QRectF(-400, -300, 800, 600);
    return bounds.adjusted(-kFitMarginMil, -kFitMarginMil, kFitMarginMil, kFitMarginMil);
}

const eda::Footprint* CanvasWidget::resolvedFootprint(const QString& id) const {
    auto found = m_footprintCache.find(id);
    if (found != m_footprintCache.end())
        return &found.value();
    if (!m_footprintResolver)
        return nullptr;
    auto result = m_footprintResolver(id);
    if (!result.status.ok || !result.value.has_value())
        return nullptr;
    auto inserted = m_footprintCache.insert(id, std::move(*result.value));
    return &inserted.value();
}

QPointF CanvasWidget::instancePoint(const eda::Instance& instance, QPointF localMil) const {
    return eda::footprintToBoard(localMil, instance.pos, instance.rotationDeg, instance.side);
}

QRectF CanvasWidget::instanceBounds(const eda::Board& board, const eda::Instance& instance) const {
    Q_UNUSED(board)
    const eda::Footprint* footprint = resolvedFootprint(instance.footprintId);
    if (!footprint)
        return QRectF(instance.pos - QPointF(80, 80), QSizeF(160, 160));
    const QRectF local =
        footprintBounds(*footprint).adjusted(kFitMarginMil, kFitMarginMil, -kFitMarginMil, -kFitMarginMil);
    QPolygonF corners;
    corners << instancePoint(instance, local.topLeft()) << instancePoint(instance, local.topRight())
            << instancePoint(instance, local.bottomRight()) << instancePoint(instance, local.bottomLeft());
    return corners.boundingRect().adjusted(-20, -20, 20, 20);
}

QRectF CanvasWidget::boardBounds(const eda::Board& board) const {
    bool initialized = false;
    QRectF bounds;
    auto include = [&](const QRectF& rect) {
        if (!initialized) {
            bounds = rect;
            initialized = true;
        } else {
            bounds = bounds.united(rect);
        }
    };
    if (board.outline.size() >= 3)
        include(board.outline.boundingRect());
    for (const auto& track : board.tracks) {
        for (const auto& point : track.points)
            include(QRectF(point - QPointF(track.width, track.width), QSizeF(track.width * 2.0, track.width * 2.0)));
    }
    for (const auto& via : board.vias)
        include(QRectF(via.pos - QPointF(via.diameter, via.diameter), QSizeF(via.diameter * 2.0, via.diameter * 2.0)));
    for (const auto& pour : board.pours)
        include(pour.points.boundingRect());
    for (const auto& instance : board.instances)
        include(instanceBounds(board, instance));
    if (!initialized)
        return QRectF(-400, -300, 800, 600);
    return bounds.adjusted(-kFitMarginMil, -kFitMarginMil, kFitMarginMil, kFitMarginMil);
}

QRectF CanvasWidget::contentBounds() const {
    if (m_context && m_context->board())
        return boardBounds(*m_context->board());
    return m_context && m_context->footprint() ? footprintBounds(*m_context->footprint())
                                               : QRectF(-400, -300, 800, 600);
}

void CanvasWidget::fitToView() {
    const QRectF viewport = canvasViewport();
    if (viewport.width() <= 0 || viewport.height() <= 0)
        return;
    const QRectF bounds = contentBounds();
    if (bounds.width() <= 0 || bounds.height() <= 0)
        return;
    const double sx = viewport.width() / bounds.width();
    const double sy = viewport.height() / bounds.height();
    m_scale = std::clamp(qMin(sx, sy), kMinScale, kMaxScale);
    m_centerWorld = bounds.center();
    m_hasFit = true;
    emit zoomChanged(m_scale);
    updateStatusText();
    update();
}

void CanvasWidget::setRulersVisible(bool visible) {
    if (m_rulersVisible == visible)
        return;
    m_rulersVisible = visible;
    update();
}

QColor CanvasWidget::layerColor(eda::LayerId layer) const {
    switch (layer) {
    case eda::kTopCopper:
        return QColor(QStringLiteral("#B64845"));
    case eda::kBottomCopper:
        return QColor(QStringLiteral("#3D70A8"));
    case eda::kTopSilk:
        return QColor(QStringLiteral("#756824"));
    case eda::kBottomSilk:
        return QColor(QStringLiteral("#657443"));
    case eda::kTopMask:
        return QColor(QStringLiteral("#6B8D70"));
    case eda::kBottomMask:
        return QColor(QStringLiteral("#4D7B7F"));
    default:
        return QColor(QStringLiteral("#555B63"));
    }
}

QColor CanvasWidget::boardLayerColor(const eda::Board& board, eda::LayerId layer) const {
    for (const auto& item : board.layers) {
        if (item.id == layer && item.color.isValid())
            return item.color;
    }
    return layerColor(layer);
}

QString CanvasWidget::layerName(eda::LayerId layer) const {
    switch (layer) {
    case eda::kTopCopper:
        return tr("顶层铜");
    case eda::kBottomCopper:
        return tr("底层铜");
    case eda::kTopSilk:
        return tr("顶层丝印");
    case eda::kBottomSilk:
        return tr("底层丝印");
    case eda::kTopMask:
        return tr("顶层阻焊");
    case eda::kBottomMask:
        return tr("底层阻焊");
    default:
        return tr("自定义层");
    }
}

QString CanvasWidget::boardLayerName(const eda::Board& board, eda::LayerId layer) const {
    for (const auto& item : board.layers) {
        if (item.id == layer)
            return item.name;
    }
    return layerName(layer);
}

bool CanvasWidget::layerVisible(eda::LayerId layer) const {
    return !m_context || m_context->isLayerVisible(layer);
}

bool CanvasWidget::selectedPad(eda::PadId id) const {
    if (!m_context)
        return false;
    const auto refs = m_context->selection();
    return std::any_of(refs.begin(), refs.end(),
                       [id](const ObjectRef& ref) { return ref.type == ObjectType::Pad && ref.padId == id; });
}

QRectF CanvasWidget::padScreenRect(const eda::Pad& pad) const {
    const QPointF center = worldToScreen(pad.pos);
    return QRectF(center.x() - pad.size.width() * m_scale / 2.0, center.y() - pad.size.height() * m_scale / 2.0,
                  pad.size.width() * m_scale, pad.size.height() * m_scale);
}

QPainterPath CanvasWidget::padPath(const eda::Pad& pad) const {
    const QRectF rect = padScreenRect(pad);
    QPainterPath path;
    switch (pad.shape) {
    case eda::PadShape::Circle:
        path.addEllipse(rect);
        break;
    case eda::PadShape::Rectangle:
        path.addRect(rect);
        break;
    case eda::PadShape::Oblong:
        path.addRoundedRect(rect, qMin(rect.width(), rect.height()) / 2.0, qMin(rect.width(), rect.height()) / 2.0);
        break;
    case eda::PadShape::RoundRect:
        path.addRoundedRect(rect, pad.cornerRadius * m_scale, pad.cornerRadius * m_scale);
        break;
    }
    return path;
}

eda::PadId CanvasWidget::padAtScreen(QPointF screenPoint) const {
    if (!m_context || !m_context->footprint() || !canvasViewport().contains(screenPoint))
        return eda::kInvalidId;
    const auto& pads = m_context->footprint()->pads;
    for (auto it = pads.crbegin(); it != pads.crend(); ++it) {
        if (!layerVisible(it->layer))
            continue;
        if (padScreenRect(*it).adjusted(-4, -4, 4, 4).contains(screenPoint))
            return it->id;
    }
    return eda::kInvalidId;
}

ObjectRef CanvasWidget::boardObjectAtScreen(QPointF screenPoint) const {
    if (!m_context || !m_context->board() || !canvasViewport().contains(screenPoint))
        return {};
    const eda::Board& board = *m_context->board();
    for (auto it = board.instances.crbegin(); it != board.instances.crend(); ++it) {
        if (instanceBounds(board, *it).contains(screenToWorld(screenPoint)))
            return ObjectRef{ObjectType::Instance, eda::kInvalidId, it->refdes, 0};
    }
    const QPointF world = screenToWorld(screenPoint);
    for (auto it = board.vias.crbegin(); it != board.vias.crend(); ++it) {
        if ((layerVisible(it->fromLayer) || layerVisible(it->toLayer)) &&
            QLineF(world, it->pos).length() <= it->diameter / 2.0 + 4.0 / m_scale)
            return ObjectRef{ObjectType::Via, eda::kInvalidId, {}, it->id};
    }
    for (auto it = board.tracks.crbegin(); it != board.tracks.crend(); ++it) {
        if (!layerVisible(it->layerId))
            continue;
        for (qsizetype i = 1; i < it->points.size(); ++i) {
            if (eda::distancePointToSegment(world, it->points[i - 1], it->points[i]) <= it->width / 2.0 + 4.0 / m_scale)
                return ObjectRef{ObjectType::Track, eda::kInvalidId, {}, it->id};
        }
    }
    for (auto it = board.pours.crbegin(); it != board.pours.crend(); ++it) {
        if (layerVisible(it->layerId) && eda::containsPoint(it->points, world, 1.0 / m_scale))
            return ObjectRef{ObjectType::Pour, eda::kInvalidId, {}, it->id};
    }
    return {};
}

void CanvasWidget::drawGrid(QPainter& painter, const QRectF& viewport) {
    painter.save();
    painter.setClipRect(viewport);
    painter.fillRect(viewport, QColor(QStringLiteral("#F7F8F5")));

    const QRectF visibleWorld = worldTransform().inverted().mapRect(viewport);
    double grid = m_context ? m_context->gridMil() : 100.0;
    while (grid * m_scale < 12.0)
        grid *= 2.0;
    const double majorGrid = grid * 5.0;

    const qint64 minX = static_cast<qint64>(std::floor(visibleWorld.left() / grid));
    const qint64 maxX = static_cast<qint64>(std::ceil(visibleWorld.right() / grid));
    const qint64 minY = static_cast<qint64>(std::floor(visibleWorld.top() / grid));
    const qint64 maxY = static_cast<qint64>(std::ceil(visibleWorld.bottom() / grid));
    if (maxX - minX < 2000 && maxY - minY < 2000) {
        for (qint64 i = minX; i <= maxX; ++i) {
            const double x = i * grid;
            const bool major = qFuzzyIsNull(std::fmod(std::abs(x), majorGrid));
            painter.setPen(
                QPen(major ? QColor(QStringLiteral("#D7DAD3")) : QColor(QStringLiteral("#E7E9E3")), major ? 1.0 : 0.7));
            const QPointF top = worldToScreen({x, visibleWorld.top()});
            const QPointF bottom = worldToScreen({x, visibleWorld.bottom()});
            painter.drawLine(top, bottom);
        }
        for (qint64 i = minY; i <= maxY; ++i) {
            const double y = i * grid;
            const bool major = qFuzzyIsNull(std::fmod(std::abs(y), majorGrid));
            painter.setPen(
                QPen(major ? QColor(QStringLiteral("#D7DAD3")) : QColor(QStringLiteral("#E7E9E3")), major ? 1.0 : 0.7));
            const QPointF left = worldToScreen({visibleWorld.left(), y});
            const QPointF right = worldToScreen({visibleWorld.right(), y});
            painter.drawLine(left, right);
        }
    }

    const QPointF origin = worldToScreen({0, 0});
    painter.setPen(QPen(QColor(QStringLiteral("#8C959B")), 1.0));
    painter.drawLine(QPointF(viewport.left(), origin.y()), QPointF(viewport.right(), origin.y()));
    painter.drawLine(QPointF(origin.x(), viewport.top()), QPointF(origin.x(), viewport.bottom()));
    painter.restore();
}

void CanvasWidget::drawRulers(QPainter& painter, const QRectF& viewport) {
    if (!m_rulersVisible)
        return;
    painter.save();
    painter.fillRect(QRectF(0, 0, width(), kRulerSize), QColor(QStringLiteral("#ECEFF0")));
    painter.fillRect(QRectF(0, 0, kRulerSize, height()), QColor(QStringLiteral("#E7EAEC")));
    painter.fillRect(QRectF(0, 0, kRulerSize, kRulerSize), QColor(QStringLiteral("#DDE2E5")));
    painter.setPen(QColor(QStringLiteral("#7B848A")));
    QFont rulerFont = painter.font();
    rulerFont.setPixelSize(10);
    painter.setFont(rulerFont);

    double step = 100.0;
    while (step * m_scale < 70.0)
        step *= 2.0;
    const QRectF visibleWorld = worldTransform().inverted().mapRect(viewport);
    const qint64 minX = static_cast<qint64>(std::floor(visibleWorld.left() / step));
    const qint64 maxX = static_cast<qint64>(std::ceil(visibleWorld.right() / step));
    for (qint64 i = minX; i <= maxX; ++i) {
        const double x = i * step;
        const QPointF screen = worldToScreen({x, 0});
        if (screen.x() < viewport.left() || screen.x() > viewport.right())
            continue;
        painter.drawLine(QPointF(screen.x(), kRulerSize - 6), QPointF(screen.x(), kRulerSize));
        painter.drawText(QPointF(screen.x() + 3, 12), QString::number(x, 'g', 6));
    }
    const qint64 minY = static_cast<qint64>(std::floor(visibleWorld.top() / step));
    const qint64 maxY = static_cast<qint64>(std::ceil(visibleWorld.bottom() / step));
    for (qint64 i = minY; i <= maxY; ++i) {
        const double y = i * step;
        const QPointF screen = worldToScreen({0, y});
        if (screen.y() < viewport.top() || screen.y() > viewport.bottom())
            continue;
        painter.drawLine(QPointF(kRulerSize - 6, screen.y()), QPointF(kRulerSize, screen.y()));
        painter.save();
        painter.translate(12, screen.y() - 3);
        painter.rotate(-90);
        painter.drawText(QPointF(0, 0), QString::number(y, 'g', 6));
        painter.restore();
    }
    painter.drawText(QPointF(7, 17), QStringLiteral("mil"));
    painter.restore();
}

void CanvasWidget::drawSilk(QPainter& painter, const eda::Footprint& footprint) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& primitive : footprint.silk) {
        if (!primitive || !layerVisible(primitive->layer))
            continue;
        const QColor color = layerColor(primitive->layer);
        const double penWidth = qMax(1.0, primitive->width * m_scale);
        QPen pen(color, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(primitive->filled ? QBrush(withAlpha(color, 36)) : Qt::NoBrush);
        switch (primitive->kind()) {
        case eda::PrimitiveKind::Line: {
            const auto& line = dynamic_cast<const eda::SilkLine&>(*primitive);
            painter.drawLine(worldToScreen(line.a), worldToScreen(line.b));
            break;
        }
        case eda::PrimitiveKind::Arc: {
            const auto& arc = dynamic_cast<const eda::SilkArc&>(*primitive);
            const QPointF center = worldToScreen(arc.center);
            const double radius = arc.radius * m_scale;
            const QRectF box(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
            QPainterPath path;
            path.arcMoveTo(box, arc.startAngle);
            path.arcTo(box, arc.startAngle, arc.spanAngle);
            painter.drawPath(path);
            break;
        }
        case eda::PrimitiveKind::Rect: {
            const auto& rect = dynamic_cast<const eda::SilkRect&>(*primitive);
            const QRectF worldRect = rect.rect.normalized();
            painter.drawRect(
                QRectF(worldToScreen(worldRect.bottomLeft()), worldToScreen(worldRect.topRight())).normalized());
            break;
        }
        case eda::PrimitiveKind::Polygon: {
            const auto& polygon = dynamic_cast<const eda::SilkPolygon&>(*primitive);
            QPolygonF screenPolygon;
            for (const auto& point : polygon.points)
                screenPolygon << worldToScreen(point);
            painter.drawPolygon(screenPolygon);
            break;
        }
        case eda::PrimitiveKind::Text: {
            const auto& text = dynamic_cast<const eda::SilkText&>(*primitive);
            QFont font = painter.font();
            font.setPixelSize(std::clamp(static_cast<int>(std::round(text.height * m_scale)), 7, 96));
            painter.setFont(font);
            painter.setPen(color);
            painter.save();
            painter.translate(worldToScreen(text.pos));
            painter.rotate(-text.rotation);
            painter.drawText(QPointF(0, 0), text.text);
            painter.restore();
            break;
        }
        }
    }
    painter.restore();
}

void CanvasWidget::drawPad(QPainter& painter, const eda::Footprint& footprint, const eda::Pad& pad) {
    if (!layerVisible(pad.layer))
        return;
    const QPainterPath path = padPath(pad);
    const QColor color = layerColor(pad.layer);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (layerVisible(eda::kTopMask) || layerVisible(eda::kBottomMask)) {
        QPainterPath maskPath = padPath(pad);
        const QRectF bounds = maskPath.boundingRect().adjusted(-pad.maskMargin * m_scale, -pad.maskMargin * m_scale,
                                                               pad.maskMargin * m_scale, pad.maskMargin * m_scale);
        painter.setPen(QPen(withAlpha(layerColor(eda::kTopMask), 150), 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds, 3, 3);
    }

    painter.setPen(QPen(color.darker(135), qMax(1.0, 1.2)));
    painter.setBrush(withAlpha(color, 160));
    painter.drawPath(path);

    if (pad.drill > 0) {
        const QPointF center = worldToScreen(pad.pos);
        const double radius = pad.drill * m_scale / 2.0;
        painter.setPen(QPen(QColor(QStringLiteral("#596168")), 1.0));
        painter.setBrush(QColor(QStringLiteral("#F7F8F5")));
        painter.drawEllipse(center, radius, radius);
    }

    painter.restore();

    if (m_scale >= 0.65) {
        painter.save();
        QFont font = painter.font();
        font.setPixelSize(std::clamp(static_cast<int>(std::round(11 + m_scale)), 9, 20));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor(QStringLiteral("#FFFDF7")));
        const QString label = QString::number(pad.number);
        const QFontMetricsF metrics(font);
        const QPointF center = worldToScreen(pad.pos);
        painter.drawText(
            QRectF(center.x() - 24, center.y() - 16, 48, 32), Qt::AlignCenter,
            metrics.horizontalAdvance(label) <= 44 ? label : (footprint.pins.isEmpty() ? QStringLiteral("•") : label));
        painter.restore();
    }
}

void CanvasWidget::drawBoardInstance(QPainter& painter, const eda::Board& board, const eda::Instance& instance) {
    const eda::Footprint* footprint = resolvedFootprint(instance.footprintId);
    const QRectF bounds = instanceBounds(board, instance);
    if (!footprint) {
        painter.save();
        painter.setPen(QPen(QColor(QStringLiteral("#B14C4C")), 1.4, Qt::DashLine));
        painter.setBrush(QColor(177, 76, 76, 20));
        painter.drawRect(QRectF(worldToScreen(bounds.topLeft()), worldToScreen(bounds.bottomRight())).normalized());
        painter.drawText(worldToScreen(instance.pos) + QPointF(8, -8), instance.footprintId);
        painter.restore();
        return;
    }

    for (const auto& hole : footprint->holes) {
        if (!layerVisible(eda::kTopCopper) && !layerVisible(eda::kBottomCopper))
            continue;
        const QPointF center = worldToScreen(instancePoint(instance, hole.pos));
        const double radius = qMax(1.0, hole.drill * m_scale / 2.0);
        painter.save();
        painter.setPen(QPen(QColor(QStringLiteral("#4D565D")), 1.0));
        painter.setBrush(hole.plated ? QColor(QStringLiteral("#B99651")) : QColor(QStringLiteral("#EAEDE9")));
        painter.drawEllipse(center, radius, radius);
        painter.setBrush(QColor(QStringLiteral("#F7F8F5")));
        painter.drawEllipse(center, radius * 0.45, radius * 0.45);
        painter.restore();
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& primitive : footprint->silk) {
        if (!primitive)
            continue;
        const eda::LayerId layer = eda::mapFootprintLayerToBoard(primitive->layer, instance.side);
        if (!layerVisible(layer))
            continue;
        const QColor color = boardLayerColor(board, layer);
        painter.setPen(QPen(color, qMax(1.0, primitive->width * m_scale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(primitive->filled ? QBrush(withAlpha(color, 36)) : Qt::NoBrush);
        switch (primitive->kind()) {
        case eda::PrimitiveKind::Line: {
            const auto& line = dynamic_cast<const eda::SilkLine&>(*primitive);
            painter.drawLine(worldToScreen(instancePoint(instance, line.a)),
                             worldToScreen(instancePoint(instance, line.b)));
            break;
        }
        case eda::PrimitiveKind::Arc: {
            const auto& arc = dynamic_cast<const eda::SilkArc&>(*primitive);
            QPainterPath path;
            constexpr int segments = 32;
            for (int i = 0; i <= segments; ++i) {
                constexpr double pi = 3.1415926535897932384626433832795;
                const double angle = (arc.startAngle + arc.spanAngle * i / segments) * pi / 180.0;
                const QPointF local = arc.center + QPointF(std::cos(angle) * arc.radius, std::sin(angle) * arc.radius);
                const QPointF screen = worldToScreen(instancePoint(instance, local));
                if (i == 0)
                    path.moveTo(screen);
                else
                    path.lineTo(screen);
            }
            painter.drawPath(path);
            break;
        }
        case eda::PrimitiveKind::Rect: {
            const auto& rect = dynamic_cast<const eda::SilkRect&>(*primitive);
            const QRectF worldRect = rect.rect.normalized();
            QPolygonF polygon;
            polygon << worldToScreen(instancePoint(instance, worldRect.topLeft()))
                    << worldToScreen(instancePoint(instance, worldRect.topRight()))
                    << worldToScreen(instancePoint(instance, worldRect.bottomRight()))
                    << worldToScreen(instancePoint(instance, worldRect.bottomLeft()));
            painter.drawPolygon(polygon);
            break;
        }
        case eda::PrimitiveKind::Polygon: {
            const auto& polygon = dynamic_cast<const eda::SilkPolygon&>(*primitive);
            QPolygonF screenPolygon;
            for (const auto& point : polygon.points)
                screenPolygon << worldToScreen(instancePoint(instance, point));
            painter.drawPolygon(screenPolygon);
            break;
        }
        case eda::PrimitiveKind::Text: {
            const auto& text = dynamic_cast<const eda::SilkText&>(*primitive);
            QFont font = painter.font();
            font.setPixelSize(std::clamp(static_cast<int>(std::round(text.height * m_scale)), 7, 96));
            painter.setFont(font);
            painter.setPen(color);
            painter.save();
            painter.translate(worldToScreen(instancePoint(instance, text.pos)));
            double textAngle = -instance.rotationDeg - text.rotation;
            if (instance.side == eda::BoardSide::Bottom) {
                while (textAngle > 90.0)
                    textAngle -= 180.0;
                while (textAngle <= -90.0)
                    textAngle += 180.0;
            }
            painter.rotate(textAngle);
            painter.drawText(QPointF(0, 0), text.text);
            painter.restore();
            break;
        }
        }
    }
    painter.restore();

    for (const auto& pad : footprint->pads) {
        const eda::LayerId layer = eda::mapFootprintLayerToBoard(pad.layer, instance.side);
        if (!layerVisible(layer))
            continue;
        const QColor color = boardLayerColor(board, layer);
        const QPointF center = worldToScreen(instancePoint(instance, pad.pos));
        const double width = pad.size.width() * m_scale;
        const double height = pad.size.height() * m_scale;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(center);
        painter.rotate(-instance.rotationDeg);
        if (instance.side == eda::BoardSide::Bottom)
            painter.scale(-1, 1);
        QPainterPath path;
        const QRectF rect(-width / 2.0, -height / 2.0, width, height);
        switch (pad.shape) {
        case eda::PadShape::Circle:
            path.addEllipse(rect);
            break;
        case eda::PadShape::Rectangle:
            path.addRect(rect);
            break;
        case eda::PadShape::Oblong:
            path.addRoundedRect(rect, qMin(width, height) / 2.0, qMin(width, height) / 2.0);
            break;
        case eda::PadShape::RoundRect:
            path.addRoundedRect(rect, pad.cornerRadius * m_scale, pad.cornerRadius * m_scale);
            break;
        }
        painter.setPen(QPen(color.darker(130), 1.0));
        painter.setBrush(withAlpha(color, 175));
        painter.drawPath(path);
        if (pad.drill > 0) {
            painter.setPen(QPen(QColor(QStringLiteral("#566068")), 0.9));
            painter.setBrush(QColor(QStringLiteral("#F7F8F5")));
            painter.drawEllipse(QPointF(0, 0), pad.drill * m_scale / 2.0, pad.drill * m_scale / 2.0);
        }
        painter.restore();
        if (m_scale >= 0.7) {
            painter.save();
            QFont font = painter.font();
            font.setPixelSize(9);
            painter.setFont(font);
            painter.setPen(QColor(QStringLiteral("#FFFDF7")));
            painter.drawText(QRectF(center.x() - 9, center.y() - 7, 18, 14), Qt::AlignCenter,
                             QString::number(pad.number));
            painter.restore();
        }
    }

    painter.save();
    QFont font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(instance.side == eda::BoardSide::Bottom ? QColor(QStringLiteral("#2F5C88"))
                                                           : QColor(QStringLiteral("#8B3533")));
    const QPointF label = worldToScreen(bounds.topLeft());
    painter.drawText(label + QPointF(4, -4), instance.refdes);
    painter.restore();
}

void CanvasWidget::drawBoardSelection(QPainter& painter, const eda::Board& board) {
    if (!m_context)
        return;
    const auto refs = m_context->selection();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(QStringLiteral("#087E8B")), 2.0, Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);
    for (const auto& ref : refs) {
        if (ref.type == ObjectType::Instance) {
            for (const auto& instance : board.instances) {
                if (instance.refdes == ref.refdes) {
                    const QRectF rect = QRectF(worldToScreen(instanceBounds(board, instance).topLeft()),
                                               worldToScreen(instanceBounds(board, instance).bottomRight()))
                                            .normalized()
                                            .adjusted(-3, -3, 3, 3);
                    painter.drawRect(rect);
                    break;
                }
            }
        } else if (ref.type == ObjectType::Track) {
            for (const auto& track : board.tracks) {
                if (track.id != ref.objectId)
                    continue;
                QPolygonF path;
                for (const auto& point : track.points)
                    path << worldToScreen(point);
                painter.drawPolyline(path);
                break;
            }
        } else if (ref.type == ObjectType::Via) {
            for (const auto& via : board.vias) {
                if (via.id != ref.objectId)
                    continue;
                painter.drawEllipse(worldToScreen(via.pos), via.diameter * m_scale / 2.0 + 4.0,
                                    via.diameter * m_scale / 2.0 + 4.0);
                break;
            }
        } else if (ref.type == ObjectType::Pour) {
            for (const auto& pour : board.pours) {
                if (pour.id != ref.objectId)
                    continue;
                QPolygonF polygon;
                for (const auto& point : pour.points)
                    polygon << worldToScreen(point);
                painter.drawPolygon(polygon);
                break;
            }
        }
    }
    painter.restore();
}

void CanvasWidget::drawBoard(QPainter& painter, const eda::Board& board, const QRectF& viewport) {
    Q_UNUSED(viewport)
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPolygonF outline;
    for (const auto& point : board.outline)
        outline << worldToScreen(point);
    if (outline.size() >= 3) {
        painter.setPen(QPen(QColor(QStringLiteral("#33473C")), 2.0));
        painter.setBrush(QColor(QStringLiteral("#EFF4EC")));
        painter.drawPolygon(outline);
    } else {
        const QRectF fallback =
            QRectF(worldToScreen(QPointF(-500, -400)), worldToScreen(QPointF(500, 400))).normalized();
        painter.setPen(QPen(QColor(QStringLiteral("#87918C")), 1.2, Qt::DashLine));
        painter.setBrush(QColor(QStringLiteral("#F3F5F2")));
        painter.drawRect(fallback);
    }

    QVector<eda::CopperPour> pours = board.pours;
    std::sort(pours.begin(), pours.end(),
              [](const eda::CopperPour& lhs, const eda::CopperPour& rhs) { return lhs.priority < rhs.priority; });
    for (const auto& pour : pours) {
        if (!layerVisible(pour.layerId))
            continue;
        QPolygonF polygon;
        for (const auto& point : pour.points)
            polygon << worldToScreen(point);
        const QColor color = boardLayerColor(board, pour.layerId);
        painter.setPen(QPen(withAlpha(color, 180), 1.0, Qt::DashLine));
        painter.setBrush(withAlpha(color, 34));
        painter.drawPolygon(polygon);
    }

    QVector<eda::BoardLayer> layers = board.layers;
    std::sort(layers.begin(), layers.end(),
              [](const eda::BoardLayer& lhs, const eda::BoardLayer& rhs) { return lhs.stackOrder > rhs.stackOrder; });
    for (const auto& layer : layers) {
        if (layer.type != eda::BoardLayerType::Copper || !layerVisible(layer.id))
            continue;
        for (const auto& track : board.tracks) {
            if (track.layerId != layer.id)
                continue;
            QPainterPath path;
            for (qsizetype i = 0; i < track.points.size(); ++i) {
                const QPointF screen = worldToScreen(track.points[i]);
                if (i == 0)
                    path.moveTo(screen);
                else
                    path.lineTo(screen);
            }
            painter.setPen(QPen(boardLayerColor(board, layer.id), qMax(1.0, track.width * m_scale), Qt::SolidLine,
                                Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }
    }

    for (const auto& instance : board.instances) {
        if (instance.side == eda::BoardSide::Bottom)
            drawBoardInstance(painter, board, instance);
    }
    for (const auto& instance : board.instances) {
        if (instance.side == eda::BoardSide::Top)
            drawBoardInstance(painter, board, instance);
    }

    for (const auto& via : board.vias) {
        if (!layerVisible(via.fromLayer) && !layerVisible(via.toLayer))
            continue;
        const QPointF center = worldToScreen(via.pos);
        const QColor color = boardLayerColor(board, via.fromLayer);
        painter.setPen(QPen(color.darker(125), 1.0));
        painter.setBrush(withAlpha(color, 190));
        painter.drawEllipse(center, via.diameter * m_scale / 2.0, via.diameter * m_scale / 2.0);
        painter.setPen(QPen(QColor(QStringLiteral("#515B61")), 0.9));
        painter.setBrush(QColor(QStringLiteral("#F7F8F5")));
        painter.drawEllipse(center, via.drill * m_scale / 2.0, via.drill * m_scale / 2.0);
    }

    drawBoardSelection(painter, board);
    painter.restore();
}

void CanvasWidget::drawFootprint(QPainter& painter, const eda::Footprint& footprint, const QRectF& viewport) {
    if (!footprint.pads.isEmpty() || !footprint.holes.isEmpty() || !footprint.silk.empty()) {
        drawSilk(painter, footprint);
        for (const auto& hole : footprint.holes) {
            if (!layerVisible(eda::kTopCopper) && !layerVisible(eda::kBottomCopper))
                continue;
            const QPointF center = worldToScreen(hole.pos);
            const double radius = hole.drill * m_scale / 2.0;
            const double keepout = (hole.drill / 2.0 + hole.keepout) * m_scale;
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(QColor(QStringLiteral("#A56D3D")), 1.2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(center, keepout, keepout);
            painter.setPen(QPen(QColor(QStringLiteral("#505860")), 1.2));
            painter.setBrush(hole.plated ? QColor(QStringLiteral("#C7A25A")) : QColor(QStringLiteral("#E9EBE7")));
            painter.drawEllipse(center, radius, radius);
            if (hole.plated)
                painter.setBrush(QColor(QStringLiteral("#F7F8F5")));
            painter.drawEllipse(center, radius * 0.48, radius * 0.48);
            painter.restore();
        }
        for (const auto& pad : footprint.pads)
            drawPad(painter, footprint, pad);
        drawSelection(painter, footprint);
    } else {
        drawEmptyState(painter, viewport);
    }
}

void CanvasWidget::drawSelection(QPainter& painter, const eda::Footprint& footprint) {
    if (!m_context)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& pad : footprint.pads) {
        if (!selectedPad(pad.id))
            continue;
        const QRectF rect = padScreenRect(pad).adjusted(-4, -4, 4, 4);
        painter.setPen(QPen(QColor(QStringLiteral("#087E8B")), 2.0, Qt::SolidLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect, 3, 3);
        const QPointF corners[4] = {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
        painter.setBrush(QColor(QStringLiteral("#F7F8F5")));
        for (const auto& point : corners)
            painter.drawRect(QRectF(point.x() - 2.5, point.y() - 2.5, 5, 5));
    }
    painter.restore();
}

void CanvasWidget::drawEditorPreview(QPainter& painter) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(QStringLiteral("#087E8B")), 1.6, Qt::DashLine));
    painter.setBrush(QColor(8, 126, 139, 28));

    if (m_mode == EditorMode::AddSilkLine && m_addPrimitiveActive) {
        painter.drawLine(worldToScreen(m_primitiveStart), worldToScreen(m_cursorWorld));
    } else if (m_mode == EditorMode::AddSilkRectangle && m_addPrimitiveActive) {
        painter.drawRect(QRectF(worldToScreen(m_primitiveStart), worldToScreen(m_cursorWorld)).normalized());
    } else if (m_mode == EditorMode::AddSilkPolygon && !m_polygonPoints.isEmpty()) {
        QPolygonF points;
        for (const auto& point : m_polygonPoints)
            points << worldToScreen(point);
        points << worldToScreen(m_cursorWorld);
        painter.drawPolyline(points);
        for (const auto& point : m_polygonPoints)
            painter.drawEllipse(worldToScreen(point), 3, 3);
    }

    if (m_rubberBandActive) {
        const QRectF band = QRectF(worldToScreen(m_rubberStart), worldToScreen(m_rubberCurrent)).normalized();
        painter.setPen(QPen(QColor(QStringLiteral("#087E8B")), 1.2, Qt::DashLine));
        painter.setBrush(QColor(8, 126, 139, 20));
        painter.drawRect(band);
    }

    if (m_mode != EditorMode::Select) {
        const QPointF cursor = worldToScreen(m_cursorWorld);
        painter.setPen(QPen(QColor(QStringLiteral("#087E8B")), 1.0));
        painter.drawLine(QPointF(cursor.x() - 8, cursor.y()), QPointF(cursor.x() + 8, cursor.y()));
        painter.drawLine(QPointF(cursor.x(), cursor.y() - 8), QPointF(cursor.x(), cursor.y() + 8));
    }
    painter.restore();
}

void CanvasWidget::drawEmptyState(QPainter& painter, const QRectF& viewport) {
    painter.save();
    painter.setPen(QColor(QStringLiteral("#A2A9AD")));
    QFont font = painter.font();
    font.setPixelSize(15);
    painter.setFont(font);
    painter.drawText(viewport.adjusted(30, 30, -30, -30), Qt::AlignCenter,
                     tr("空封装\n使用“插入”菜单添加焊盘、安装孔或丝印"));
    painter.setPen(QColor(QStringLiteral("#B7BDBF")));
    painter.drawEllipse(worldToScreen({0, 0}), 4, 4);
    painter.restore();
}

void CanvasWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#E5E8E8")));
    const QRectF viewport = canvasViewport();
    drawGrid(painter, viewport);
    painter.save();
    painter.setClipRect(viewport);
    if (m_context) {
        if (m_context->board()) {
            const eda::Board& board = m_boardPreview.has_value() ? *m_boardPreview : *m_context->board();
            drawBoard(painter, board, viewport);
        } else if (m_context->footprint()) {
            const eda::Footprint& footprint =
                m_previewFootprint.has_value() ? *m_previewFootprint : *m_context->footprint();
            drawFootprint(painter, footprint, viewport);
        }
    } else {
        drawEmptyState(painter, viewport);
    }
    drawEditorPreview(painter);
    if (m_tool)
        m_tool->paintOverlay(&painter, canvasView());
    painter.restore();
    drawRulers(painter, viewport);

    painter.save();
    painter.setPen(QPen(QColor(QStringLiteral("#C6CCCE")), 1.0));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    if (m_context) {
        painter.setPen(QColor(QStringLiteral("#677177")));
        QFont statusFont = painter.font();
        statusFont.setPixelSize(10);
        painter.setFont(statusFont);
        painter.drawText(QRectF(viewport.right() - 360, viewport.bottom() - 20, 350, 16), Qt::AlignRight,
                         QStringLiteral("%1  ·  %2 mil")
                             .arg(m_context->board() ? boardLayerName(*m_context->board(), m_context->activeLayer())
                                                     : layerName(m_context->activeLayer()))
                             .arg(m_context->gridMil(), 0, 'f', 0));
    }
    painter.restore();
}

void CanvasWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!m_hasFit) {
        QTimer::singleShot(0, this, [this] { fitToView(); });
    }
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() == 0) {
        event->ignore();
        return;
    }
    const double factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
    zoomAround(event->position(), factor);
    event->accept();
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    m_lastMouseScreen = eventPosition(event);
    if (!canvasViewport().contains(m_lastMouseScreen)) {
        event->ignore();
        return;
    }
    const QPointF rawWorld = unsnappedWorld(m_lastMouseScreen);
    m_cursorWorld = rawWorld;
    if (m_tool) {
        bool snapped = false;
        ToolEvent toolEvent{m_context ? m_context->snap(rawWorld) : rawWorld, event->button(), event->modifiers(),
                            snapped};
        if (m_context)
            toolEvent.snapped = m_context->snapEnabled();
        m_tool->mousePress(toolEvent);
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spacePressed)) {
        m_panning = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && m_mode == EditorMode::AddSilkPolygon) {
        finishPolygon();
        event->accept();
        return;
    }

    bool snapped = false;
    const QPointF world = snappedWorld(m_lastMouseScreen, &snapped);
    m_cursorWorld = world;
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (m_mode == EditorMode::AddSilkLine || m_mode == EditorMode::AddSilkRectangle) {
        m_primitiveStart = world;
        m_addPrimitiveActive = true;
        m_cursorWorld = world;
        update();
        event->accept();
        return;
    }
    if (m_mode == EditorMode::AddSilkPolygon) {
        if (m_polygonPoints.size() >= 3) {
            const QPointF firstScreen = worldToScreen(m_polygonPoints.first());
            if (QLineF(firstScreen, m_lastMouseScreen).length() <= 8.0) {
                finishPolygon();
                event->accept();
                return;
            }
        }
        m_polygonPoints.append(world);
        m_cursorWorld = world;
        update();
        event->accept();
        return;
    }
    if (m_mode == EditorMode::AddSilkText) {
        emit textPlacementRequested(world);
        event->accept();
        return;
    }

    if (m_context && m_context->board()) {
        const ObjectRef hit = boardObjectAtScreen(m_lastMouseScreen);
        if (hit.type != ObjectType::None) {
            QVector<ObjectRef> refs = m_context->selection();
            const bool alreadySelected = std::find(refs.begin(), refs.end(), hit) != refs.end();
            if (event->modifiers() & Qt::ControlModifier) {
                if (alreadySelected)
                    refs.erase(std::remove(refs.begin(), refs.end(), hit), refs.end());
                else
                    refs.append(hit);
                m_context->setSelection(refs);
            } else if (!alreadySelected) {
                m_context->setSelection({hit});
            }
            m_draggingBoardObject = true;
            m_dragMoved = false;
            m_dragStartWorld = rawWorld;
            m_boardDragSnapshot.reset();
            m_boardPreview.reset();
            event->accept();
            return;
        }

        m_rubberBandActive = true;
        m_rubberStart = m_lastMouseScreen;
        m_rubberCurrent = m_lastMouseScreen;
        m_rubberBaseSelection =
            (event->modifiers() & Qt::ControlModifier) ? m_context->selection() : QVector<ObjectRef>{};
        if (!(event->modifiers() & Qt::ControlModifier))
            m_context->setSelection({});
        update();
        event->accept();
        return;
    }

    const eda::PadId hit = padAtScreen(m_lastMouseScreen);
    if (hit != eda::kInvalidId) {
        QVector<ObjectRef> refs = m_context ? m_context->selection() : QVector<ObjectRef>{};
        const bool alreadySelected = std::any_of(refs.begin(), refs.end(), [hit](const ObjectRef& ref) {
            return ref.type == ObjectType::Pad && ref.padId == hit;
        });
        if (event->modifiers() & Qt::ControlModifier) {
            if (alreadySelected) {
                refs.erase(std::remove_if(
                               refs.begin(), refs.end(),
                               [hit](const ObjectRef& ref) { return ref.type == ObjectType::Pad && ref.padId == hit; }),
                           refs.end());
            } else {
                refs.append(ObjectRef{ObjectType::Pad, hit, {}, 0});
            }
            m_context->setSelection(refs);
        } else if (!alreadySelected) {
            m_context->setSelection({ObjectRef{ObjectType::Pad, hit, {}, 0}});
        }
        if (!(event->modifiers() & Qt::ControlModifier) && alreadySelected) {
            const auto& pads = m_context->footprint()->pads;
            const auto found =
                std::find_if(pads.begin(), pads.end(), [hit](const eda::Pad& pad) { return pad.id == hit; });
            if (found != pads.end() && QLineF(m_lastMouseScreen, padScreenRect(*found).bottomRight()).length() <= 8.0) {
                m_resizingPad = true;
                m_dragMoved = false;
                m_dragStartWorld = rawWorld;
                m_resizeStartSize = found->size;
                m_dragSnapshot.reset();
                m_previewFootprint.reset();
                event->accept();
                return;
            }
        }
        m_draggingPad = true;
        m_dragMoved = false;
        m_dragStartWorld = rawWorld;
        m_dragSnapshot.reset();
        m_previewFootprint.reset();
        event->accept();
        return;
    }

    m_rubberBandActive = true;
    m_rubberStart = m_lastMouseScreen;
    m_rubberCurrent = m_lastMouseScreen;
    m_rubberBaseSelection =
        (event->modifiers() & Qt::ControlModifier) && m_context ? m_context->selection() : QVector<ObjectRef>{};
    if (m_context && !(event->modifiers() & Qt::ControlModifier))
        m_context->setSelection({});
    update();
    event->accept();
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPointF screen = eventPosition(event);
    const QPointF rawWorld = unsnappedWorld(screen);
    m_cursorWorld = m_context ? m_context->snap(rawWorld) : rawWorld;
    emit cursorWorldPositionChanged(m_cursorWorld);

    if (m_tool) {
        ToolEvent toolEvent{m_cursorWorld, Qt::NoButton, event->modifiers(),
                            m_context ? m_context->snapEnabled() : false};
        m_tool->mouseMove(toolEvent);
        update();
        return;
    }

    if (m_panning) {
        const QPointF delta = screen - m_lastMouseScreen;
        m_centerWorld += QPointF(-delta.x() / m_scale, delta.y() / m_scale);
        m_lastMouseScreen = screen;
        updateStatusText();
        update();
        return;
    }

    if (m_draggingBoardObject && (event->buttons() & Qt::LeftButton) && m_context && m_context->board()) {
        if (!m_dragMoved && QLineF(screen, worldToScreen(m_dragStartWorld)).length() > 2.0)
            m_dragMoved = true;
        if (m_dragMoved) {
            if (!m_boardDragSnapshot.has_value()) {
                m_boardDragSnapshot = *m_context->board();
                m_boardPreview = *m_boardDragSnapshot;
            }
            const QPointF delta = m_context->snap(rawWorld) - m_context->snap(m_dragStartWorld);
            eda::Board preview = *m_boardDragSnapshot;
            const auto refs = m_context->selection();
            for (auto& instance : preview.instances) {
                if (std::any_of(refs.begin(), refs.end(), [&](const ObjectRef& ref) {
                        return ref.type == ObjectType::Instance && ref.refdes == instance.refdes;
                    }))
                    instance.pos += delta;
            }
            for (auto& track : preview.tracks) {
                if (std::none_of(refs.begin(), refs.end(), [&](const ObjectRef& ref) {
                        return ref.type == ObjectType::Track && ref.objectId == track.id;
                    }))
                    continue;
                for (auto& point : track.points)
                    point += delta;
            }
            for (auto& via : preview.vias) {
                if (std::any_of(refs.begin(), refs.end(), [&](const ObjectRef& ref) {
                        return ref.type == ObjectType::Via && ref.objectId == via.id;
                    }))
                    via.pos += delta;
            }
            for (auto& pour : preview.pours) {
                if (std::none_of(refs.begin(), refs.end(), [&](const ObjectRef& ref) {
                        return ref.type == ObjectType::Pour && ref.objectId == pour.id;
                    }))
                    continue;
                for (auto& point : pour.points)
                    point += delta;
            }
            m_boardPreview = std::move(preview);
            update();
        }
    }

    if ((m_draggingPad || m_resizingPad) && (event->buttons() & Qt::LeftButton) && m_context &&
        m_context->footprint()) {
        if (!m_dragMoved && QLineF(screen, worldToScreen(m_dragStartWorld)).length() > 2.0)
            m_dragMoved = true;
        if (m_dragMoved) {
            if (!m_dragSnapshot.has_value()) {
                m_dragSnapshot = *m_context->footprint();
                m_previewFootprint = *m_dragSnapshot;
            }
            QPointF delta = m_context->snap(rawWorld) - m_context->snap(m_dragStartWorld);
            eda::Footprint preview = *m_dragSnapshot;
            const auto refs = m_context->selection();
            for (auto& pad : preview.pads) {
                const bool selected = std::any_of(refs.begin(), refs.end(), [&](const ObjectRef& ref) {
                    return ref.type == ObjectType::Pad && ref.padId == pad.id;
                });
                if (m_resizingPad && selected) {
                    pad.size = QSizeF(qMax(0.001, m_resizeStartSize.width() + 2.0 * delta.x()),
                                      qMax(0.001, m_resizeStartSize.height() + 2.0 * delta.y()));
                } else if (selected) {
                    pad.pos += delta;
                }
            }
            m_previewFootprint = std::move(preview);
            update();
        }
    }

    if (m_rubberBandActive) {
        m_rubberCurrent = screen;
        update();
    }
    if (m_addPrimitiveActive)
        update();

    updateStatusText();
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    const QPointF screen = eventPosition(event);
    if (m_tool) {
        const QPointF raw = unsnappedWorld(screen);
        ToolEvent toolEvent{m_context ? m_context->snap(raw) : raw, event->button(), event->modifiers(),
                            m_context ? m_context->snapEnabled() : false};
        m_tool->mouseRelease(toolEvent);
        event->accept();
        return;
    }
    if (m_panning) {
        m_panning = false;
        setCursor(m_mode == EditorMode::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        event->accept();
        return;
    }
    if (m_draggingBoardObject) {
        if (m_dragMoved && m_boardDragSnapshot.has_value() && m_boardPreview.has_value())
            emit boardEditRequested(*m_boardDragSnapshot, *m_boardPreview, tr("移动 PCB 对象"));
        m_draggingBoardObject = false;
        m_dragMoved = false;
        m_boardDragSnapshot.reset();
        m_boardPreview.reset();
        update();
        event->accept();
        return;
    }
    if (m_draggingPad || m_resizingPad) {
        if (m_dragMoved && m_dragSnapshot.has_value() && m_previewFootprint.has_value())
            emit footprintEditRequested(*m_dragSnapshot, *m_previewFootprint,
                                        m_resizingPad ? tr("调整焊盘尺寸") : tr("移动焊盘"));
        m_draggingPad = false;
        m_resizingPad = false;
        m_dragMoved = false;
        m_dragSnapshot.reset();
        m_previewFootprint.reset();
        update();
        event->accept();
        return;
    }
    if (m_rubberBandActive) {
        const QRectF band = QRectF(m_rubberStart, screen).normalized();
        QVector<ObjectRef> refs = m_rubberBaseSelection;
        if (m_context && m_context->board()) {
            const eda::Board& board = *m_context->board();
            auto appendRef = [&](const ObjectRef& ref) {
                if (std::find(refs.begin(), refs.end(), ref) == refs.end())
                    refs.append(ref);
            };
            for (const auto& instance : board.instances) {
                const QRectF bounds = instanceBounds(board, instance);
                const QRectF screenBounds =
                    QRectF(worldToScreen(bounds.topLeft()), worldToScreen(bounds.bottomRight())).normalized();
                if (screenBounds.intersects(band))
                    appendRef(ObjectRef{ObjectType::Instance, eda::kInvalidId, instance.refdes, 0});
            }
            for (const auto& track : board.tracks) {
                QPolygonF path;
                for (const auto& point : track.points)
                    path << worldToScreen(point);
                if (path.boundingRect().intersects(band))
                    appendRef(ObjectRef{ObjectType::Track, eda::kInvalidId, {}, track.id});
            }
            for (const auto& via : board.vias) {
                if (band.contains(worldToScreen(via.pos)))
                    appendRef(ObjectRef{ObjectType::Via, eda::kInvalidId, {}, via.id});
            }
            for (const auto& pour : board.pours) {
                QPolygonF polygon;
                for (const auto& point : pour.points)
                    polygon << worldToScreen(point);
                if (polygon.boundingRect().intersects(band))
                    appendRef(ObjectRef{ObjectType::Pour, eda::kInvalidId, {}, pour.id});
            }
            m_context->setSelection(refs);
        } else if (m_context && m_context->footprint()) {
            for (const auto& pad : m_context->footprint()->pads) {
                if (!padScreenRect(pad).intersects(band))
                    continue;
                const ObjectRef ref{ObjectType::Pad, pad.id, {}, 0};
                if (std::find(refs.begin(), refs.end(), ref) == refs.end())
                    refs.append(ref);
            }
            m_context->setSelection(refs);
        }
        m_rubberBandActive = false;
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_addPrimitiveActive && m_context && m_context->footprint()) {
        const eda::Footprint before = *m_context->footprint();
        eda::Footprint after = before;
        const QPointF end = m_context->snap(unsnappedWorld(screen));
        if (m_mode == EditorMode::AddSilkLine && QLineF(m_primitiveStart, end).length() > 0.01) {
            auto line = std::make_unique<eda::SilkLine>();
            line->layer = m_context->activeLayer();
            line->a = m_primitiveStart;
            line->b = end;
            after.silk.push_back(std::move(line));
            emit footprintEditRequested(before, after, tr("添加丝印线"));
        } else if (m_mode == EditorMode::AddSilkRectangle) {
            const QRectF rect = QRectF(m_primitiveStart, end).normalized();
            if (rect.width() > 0.01 && rect.height() > 0.01) {
                auto shape = std::make_unique<eda::SilkRect>();
                shape->layer = m_context->activeLayer();
                shape->rect = rect;
                after.silk.push_back(std::move(shape));
                emit footprintEditRequested(before, after, tr("添加丝印矩形"));
            }
        }
        m_addPrimitiveActive = false;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (m_mode == EditorMode::AddSilkPolygon && event->button() == Qt::LeftButton) {
        finishPolygon();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::finishPolygon() {
    if (m_polygonPoints.size() >= 3 && m_context && m_context->footprint()) {
        const eda::Footprint before = *m_context->footprint();
        eda::Footprint after = before;
        auto polygon = std::make_unique<eda::SilkPolygon>();
        polygon->layer = m_context->activeLayer();
        polygon->points = QPolygonF(m_polygonPoints);
        after.silk.push_back(std::move(polygon));
        emit footprintEditRequested(before, after, tr("添加丝印多边形"));
    }
    m_polygonPoints.clear();
    update();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (m_tool) {
        m_tool->keyPress(event);
        if (event->isAccepted())
            return;
    }
    if (event->key() == Qt::Key_Escape) {
        cancelInteraction();
        setEditorMode(EditorMode::Select);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit deleteSelectionRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space) {
        m_spacePressed = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F) {
        fitToView();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        m_spacePressed = false;
        if (!m_panning)
            setCursor(m_mode == EditorMode::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CanvasWidget::leaveEvent(QEvent* event) {
    if (m_mode != EditorMode::Select && !m_tool)
        setCursor(Qt::CrossCursor);
    QWidget::leaveEvent(event);
}

void CanvasWidget::cancelInteraction() {
    if (m_tool)
        m_tool->cancel();
    m_panning = false;
    m_draggingPad = false;
    m_draggingBoardObject = false;
    m_resizingPad = false;
    m_dragMoved = false;
    m_rubberBandActive = false;
    m_addPrimitiveActive = false;
    m_polygonPoints.clear();
    m_dragSnapshot.reset();
    m_previewFootprint.reset();
    m_boardDragSnapshot.reset();
    m_boardPreview.reset();
    update();
}

void CanvasWidget::updateStatusText() {
    emit cursorWorldPositionChanged(m_cursorWorld);
    emit selectionCountChanged(m_context ? static_cast<int>(m_context->selection().size()) : 0);
}
