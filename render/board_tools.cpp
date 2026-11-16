#include "render/board_tools.h"

#include "app/document.h"
#include "feat_pcb/board_commands.h"
#include "feat_pcb/copper_pour.h"
#include "feat_pcb/layout_service.h"
#include "feat_pcb/orthogonal_router.h"

#include <QKeyEvent>
#include <QPainterPath>

#include <algorithm>

namespace {
void preview(QPainter* painter, const CanvasView& view, const QPolygonF& points) {
    painter->save();
    painter->setPen(QPen(QColor(35, 150, 155), 2, Qt::DashLine));
    painter->drawPolyline(view.worldToScreen.map(points));
    painter->restore();
}
}

void BoardTool::activate(DocumentContext* context) {
    document = qobject_cast<Document*>(context);
}

void BoardTool::deactivate() {
    cancel();
    document = nullptr;
}

void BoardTool::keyPress(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape)
        cancel();
}

bool BoardTool::copperLayer() const {
    if (!document || !document->board())
        return false;
    const auto& layers = document->board()->layers;
    const auto active = document->activeLayer();
    if (std::any_of(layers.begin(), layers.end(), [active](const auto& layer) {
            return layer.id == active && layer.type == eda::BoardLayerType::Copper;
        }) && document->isLayerVisible(active))
        return true;
    document->showStatus(QStringLiteral("请选择可见的铜层"));
    return false;
}

bool BoardTool::commit(eda::Board after, const QString& label) {
    const auto status = document->pushCommand(
        new ReplaceBoardCommand(document, document->boardData(), std::move(after), label));
    if (!status)
        document->showStatus(status.message);
    return status.ok;
}

PlaceTool::PlaceTool(eda::FootprintResolver resolver) : m_resolver(std::move(resolver)) {}

void PlaceTool::mousePress(const ToolEvent& event) {
    if (event.button != Qt::LeftButton || !document || !document->board())
        return;
    auto after = document->boardData();
    if (after.instances.isEmpty())
        return;
    eda::InstanceLayoutOptions options;
    options.origin = event.worldMil;
    const auto result = eda::layoutInstances(after, m_resolver, options);
    if (!result.status) {
        document->showStatus(result.status.message);
        return;
    }
    bool changed = false;
    for (qsizetype i = 0; i < after.instances.size(); ++i) {
        changed |= after.instances[i].pos != result.positions[i];
        after.instances[i].pos = result.positions[i];
    }
    if (changed)
        commit(std::move(after), QStringLiteral("自动摆放"));
}

RouteTool::RouteTool(eda::OrthoMode mode) : m_mode(mode) {}

void RouteTool::activate(DocumentContext* context) {
    BoardTool::activate(context);
    if (document) {
        m_layer = document->activeLayer();
        m_initialLayer = m_layer;
    }
    m_start.reset();
    m_before.reset();
    m_session = {};
}

bool RouteTool::ensureSession() {
    if (!document || !document->board())
        return false;
    if (!m_before.has_value()) {
        m_before = document->boardData();
        m_session = *m_before;
        m_initialLayer = m_layer;
    }
    return true;
}

eda::LayerId RouteTool::alternateCopperLayer() const {
    if (!document || !document->board())
        return -1;
    QVector<eda::BoardLayer> candidates;
    for (const eda::BoardLayer& layer : document->boardData().layers) {
        if (layer.type == eda::BoardLayerType::Copper && layer.id != m_layer && document->isLayerVisible(layer.id))
            candidates.append(layer);
    }
    std::sort(candidates.begin(), candidates.end(), [](const eda::BoardLayer& lhs, const eda::BoardLayer& rhs) {
        if (lhs.stackOrder != rhs.stackOrder)
            return lhs.stackOrder < rhs.stackOrder;
        return lhs.id < rhs.id;
    });
    if (candidates.isEmpty())
        return -1;
    const auto preferred = std::find_if(candidates.begin(), candidates.end(), [](const eda::BoardLayer& layer) {
        return layer.id == eda::kBottomCopper || layer.id == eda::kTopCopper;
    });
    return preferred != candidates.end() ? preferred->id : candidates.first().id;
}

void RouteTool::setOrthoMode(eda::OrthoMode mode) {
    m_mode = mode;
    if (document)
        document->requestRepaint();
}

eda::OrthoMode RouteTool::orthoMode() const {
    return m_mode;
}

void RouteTool::mousePress(const ToolEvent& event) {
    if (!document)
        return;
    if (event.button == Qt::RightButton) {
        finishRoute();
        return;
    }
    if (event.button != Qt::LeftButton || !copperLayer())
        return;
    if (!ensureSession())
        return;
    m_layer = document->activeLayer();
    if (!m_start) {
        m_start = m_cursor = event.worldMil;
        document->requestRepaint();
        return;
    }
    eda::RouteError error = eda::RouteError::None;
    const auto points = eda::orthogonalPath(*m_start, event.worldMil, m_mode, std::nullopt, &error);
    if (points.size() < 2) {
        if (error != eda::RouteError::None)
            document->showStatus(QStringLiteral("正交走线参数非法"));
        return;
    }
    eda::Track track;
    track.id = m_session.allocateTrackId();
    track.layerId = m_layer;
    track.points = points;
    const auto status = m_session.insertTrack(track);
    if (!status) {
        document->showStatus(status.message);
        return;
    }
    m_start = m_cursor = event.worldMil;
    document->requestRepaint();
}

void RouteTool::mouseMove(const ToolEvent& event) {
    m_cursor = event.worldMil;
    if (m_start && document)
        document->requestRepaint();
}

void RouteTool::keyPress(QKeyEvent* event) {
    if (event->key() == Qt::Key_V) {
        placeVia();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        finishRoute();
        event->accept();
        return;
    }
    BoardTool::keyPress(event);
}

void RouteTool::placeVia() {
    if (!document || !ensureSession())
        return;
    const QPointF position = m_start.value_or(m_cursor);
    const eda::LayerId target = alternateCopperLayer();
    if (target < 0) {
        document->showStatus(QStringLiteral("需要至少两个可见铜层才能放置过孔"));
        return;
    }
    eda::Via via;
    via.id = m_session.allocateViaId();
    via.pos = position;
    via.drill = 24.0;
    via.diameter = 44.0;
    via.fromLayer = m_layer;
    via.toLayer = target;
    const auto status = m_session.insertVia(via);
    if (!status) {
        document->showStatus(status.message);
        return;
    }
    m_layer = target;
    m_start = position;
    m_cursor = position;
    document->setActiveLayer(target);
    document->requestRepaint();
    document->showStatus(QStringLiteral("已放置过孔并切换到目标铜层"));
}

void RouteTool::finishRoute() {
    if (!document || !m_before.has_value()) {
        cancel();
        return;
    }
    const bool changed = m_session.tracks.size() != m_before->tracks.size() ||
                         m_session.vias.size() != m_before->vias.size();
    if (!changed) {
        cancel();
        return;
    }
    if (commit(std::move(m_session), QStringLiteral("完成多层正交布线")))
        cancel();
}

void RouteTool::paintOverlay(QPainter* painter, const CanvasView& view) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (const eda::Track& track : m_session.tracks) {
        QPainterPath path;
        for (qsizetype i = 0; i < track.points.size(); ++i) {
            const QPointF screen = view.toScreen(track.points[i]);
            if (i == 0)
                path.moveTo(screen);
            else
                path.lineTo(screen);
        }
        painter->setPen(QPen(QColor(8, 126, 139), qMax(2.0, track.width * view.scale()), Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(path);
    }
    for (const eda::Via& via : m_session.vias) {
        painter->setPen(QPen(QColor(8, 126, 139), 2.0));
        painter->setBrush(QColor(8, 126, 139, 80));
        painter->drawEllipse(view.toScreen(via.pos), qMax(3.0, via.diameter * view.scale() / 2.0),
                             qMax(3.0, via.diameter * view.scale() / 2.0));
    }
    if (m_start) {
        const auto pending = eda::orthogonalPath(*m_start, m_cursor, m_mode);
        if (pending.size() >= 2)
            preview(painter, view, QPolygonF(pending));
    }
    painter->restore();
}

void RouteTool::cancel() {
    m_start.reset();
    m_before.reset();
    m_session = {};
    if (document)
        document->requestRepaint();
}

eda::LayerId ViaTool::alternateCopperLayer() const {
    if (!document || !document->board())
        return -1;
    const eda::LayerId active = document->activeLayer();
    for (const eda::BoardLayer& layer : document->boardData().layers) {
        if (layer.type == eda::BoardLayerType::Copper && layer.id != active && document->isLayerVisible(layer.id))
            return layer.id;
    }
    return -1;
}

void ViaTool::mousePress(const ToolEvent& event) {
    if (event.button != Qt::LeftButton || !document || !copperLayer())
        return;
    const eda::LayerId target = alternateCopperLayer();
    if (target < 0) {
        document->showStatus(QStringLiteral("需要至少两个可见铜层才能放置过孔"));
        return;
    }
    eda::Board after = document->boardData();
    eda::Via via;
    via.id = after.allocateViaId();
    via.pos = event.worldMil;
    via.drill = 24.0;
    via.diameter = 44.0;
    via.fromLayer = document->activeLayer();
    via.toLayer = target;
    const auto status = after.insertVia(via);
    if (!status) {
        document->showStatus(status.message);
        return;
    }
    if (commit(std::move(after), QStringLiteral("放置过孔"))) {
        document->setActiveLayer(target);
        document->setSelection({ObjectRef{ObjectType::Via, eda::kInvalidId, {}, via.id}});
    }
}

void PourTool::mousePress(const ToolEvent& event) {
    if (event.button == Qt::RightButton) {
        finish();
        return;
    }
    if (event.button != Qt::LeftButton || !copperLayer())
        return;
    if (m_layer != document->activeLayer())
        m_points.clear();
    m_layer = document->activeLayer();
    if (m_points.isEmpty() || m_points.back() != event.worldMil)
        m_points.append(event.worldMil);
    m_cursor = event.worldMil;
    document->requestRepaint();
}

void PourTool::mouseMove(const ToolEvent& event) {
    m_cursor = event.worldMil;
    if (!m_points.isEmpty() && document)
        document->requestRepaint();
}

void PourTool::keyPress(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        finish();
    else
        BoardTool::keyPress(event);
}

void PourTool::paintOverlay(QPainter* painter, const CanvasView& view) const {
    if (!m_points.isEmpty()) {
        auto points = m_points;
        preview(painter, view, points << m_cursor << m_points.front());
    }
}

void PourTool::finish() {
    if (m_points.size() < 3 || !copperLayer() || m_layer != document->activeLayer())
        return;
    auto after = document->boardData();
    eda::PourCreateOptions options;
    options.layerId = m_layer;
    options.polygon = m_points;
    const auto result = eda::CopperPourService::add(after, options);
    if (!result.status) {
        document->showStatus(result.status.message);
        return;
    }
    if (commit(std::move(after), QStringLiteral("添加覆铜区域")))
        cancel();
}

void PourTool::cancel() {
    m_points.clear();
    if (document)
        document->requestRepaint();
}
