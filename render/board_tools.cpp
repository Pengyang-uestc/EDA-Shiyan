#include "render/board_tools.h"

#include "app/document.h"
#include "feat_pcb/board_commands.h"
#include "feat_pcb/copper_pour.h"
#include "feat_pcb/layout_service.h"
#include "feat_pcb/orthogonal_router.h"

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

void RouteTool::mousePress(const ToolEvent& event) {
    if (event.button == Qt::RightButton) {
        cancel();
        return;
    }
    if (event.button != Qt::LeftButton || !copperLayer())
        return;
    if (!m_start || m_layer != document->activeLayer()) {
        m_start = m_cursor = event.worldMil;
        m_layer = document->activeLayer();
        document->requestRepaint();
        return;
    }
    const auto points = eda::orthogonalPath(*m_start, event.worldMil, eda::OrthoMode::HV);
    if (points.size() < 2)
        return;
    auto after = document->boardData();
    eda::Track track;
    track.id = after.allocateTrackId();
    track.layerId = m_layer;
    track.points = points;
    const auto status = after.insertTrack(track);
    if (!status) {
        document->showStatus(status.message);
        return;
    }
    if (commit(std::move(after), QStringLiteral("添加正交走线")))
        cancel();
}

void RouteTool::mouseMove(const ToolEvent& event) {
    m_cursor = event.worldMil;
    if (m_start && document)
        document->requestRepaint();
}

void RouteTool::paintOverlay(QPainter* painter, const CanvasView& view) const {
    if (m_start)
        preview(painter, view, QPolygonF(eda::orthogonalPath(*m_start, m_cursor, eda::OrthoMode::HV)));
}

void RouteTool::cancel() {
    m_start.reset();
    if (document)
        document->requestRepaint();
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
