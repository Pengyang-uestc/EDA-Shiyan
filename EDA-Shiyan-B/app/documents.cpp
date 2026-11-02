#include "app/document.h"

#include "app/commands.h"

#include <QFileInfo>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
bool sameRefs(const QVector<ObjectRef>& lhs, const QVector<ObjectRef>& rhs) {
    return lhs == rhs;
}
} // namespace

Document::Document(QObject* parent) : DocumentContext(parent) {
    eda::Footprint blank;
    blank.id = QStringLiteral("untitled");
    blank.name = QStringLiteral("未命名封装");
    m_footprint = std::move(blank);
    for (int layer = eda::kTopCopper; layer <= eda::kBottomMask; ++layer)
        m_visibleLayers.insert(layer);

    connect(&m_undoStack, &QUndoStack::indexChanged, this, [this] {
        notifyModelChanged();
        emit undoAvailabilityChanged(canUndo(), canRedo());
        emit cleanChanged(m_undoStack.isClean());
    });
    connect(&m_undoStack, &QUndoStack::cleanChanged, this, [this](bool clean) { emit cleanChanged(clean); });
}

eda::Footprint* Document::footprint() {
    return m_mode == Mode::Footprint ? &m_footprint : nullptr;
}

eda::Board* Document::board() {
    return m_mode == Mode::Board ? &m_board : nullptr;
}

eda::LayerId Document::activeLayer() const {
    return m_activeLayer;
}

void Document::setActiveLayer(eda::LayerId id) {
    if (m_activeLayer == id)
        return;
    m_activeLayer = id;
    emit activeLayerChanged(id);
    emit repaintRequested();
}

bool Document::isLayerVisible(eda::LayerId id) const {
    return m_visibleLayers.contains(id);
}

void Document::setLayerVisible(eda::LayerId id, bool visible) {
    const bool old = m_visibleLayers.contains(id);
    if (old == visible)
        return;
    if (visible)
        m_visibleLayers.insert(id);
    else
        m_visibleLayers.remove(id);
    emit layerVisibilityChanged(id, visible);
    emit repaintRequested();
}

double Document::gridMil() const {
    return m_gridMil;
}

void Document::setGridMil(double mil) {
    if (!std::isfinite(mil) || mil <= 0)
        return;
    m_gridMil = mil;
    emit repaintRequested();
}

bool Document::snapEnabled() const {
    return m_snapEnabled;
}

void Document::setSnapEnabled(bool enabled) {
    if (m_snapEnabled == enabled)
        return;
    m_snapEnabled = enabled;
    emit repaintRequested();
}

QPointF Document::snap(QPointF worldMil) const {
    if (!m_snapEnabled || m_gridMil <= 0)
        return worldMil;
    return {std::round(worldMil.x() / m_gridMil) * m_gridMil, std::round(worldMil.y() / m_gridMil) * m_gridMil};
}

QVector<ObjectRef> Document::selection() const {
    return m_selection;
}

void Document::setSelection(const QVector<ObjectRef>& refs) {
    if (sameRefs(m_selection, refs))
        return;
    m_selection = refs;
    emit selectionChanged();
    emit repaintRequested();
}

eda::Status Document::pushCommand(EdaCommand* command) {
    if (!command)
        return eda::Status::failure(QStringLiteral("内部错误：空命令"));
    const auto check = command->canApply();
    if (!check.ok) {
        delete command;
        showStatus(check.message);
        return check;
    }
    m_undoStack.push(command);
    return eda::Status::success();
}

void Document::requestRepaint() {
    emit repaintRequested();
}

void Document::showStatus(const QString& text) {
    emit statusMessageRequested(text);
}

void Document::installFootprint(eda::Footprint footprint, const QString& path) {
    m_mode = Mode::Footprint;
    m_footprint = std::move(footprint);
    m_filePath = path;
    m_undoStack.clear();
    m_undoStack.setClean();
    m_selection.clear();
    emit documentReset();
    emit documentPathChanged(m_filePath);
    emit selectionChanged();
    emit modelChanged();
    emit repaintRequested();
    emit undoAvailabilityChanged(false, false);
}

void Document::installBoard(eda::Board board, const QString& path) {
    m_mode = Mode::Board;
    m_board = std::move(board);
    m_filePath = path;
    m_undoStack.clear();
    m_undoStack.setClean();
    m_selection.clear();
    m_visibleLayers.clear();
    for (const auto& layer : m_board.layers)
        m_visibleLayers.insert(layer.id);
    m_activeLayer = eda::kTopCopper;
    const auto copper = std::find_if(m_board.layers.begin(), m_board.layers.end(), [](const eda::BoardLayer& layer) {
        return layer.type == eda::BoardLayerType::Copper;
    });
    if (copper != m_board.layers.end())
        m_activeLayer = copper->id;
    emit documentReset();
    emit documentPathChanged(m_filePath);
    emit activeLayerChanged(m_activeLayer);
    emit selectionChanged();
    emit modelChanged();
    emit repaintRequested();
    emit undoAvailabilityChanged(false, false);
}

const eda::Footprint& Document::footprintData() const {
    return m_footprint;
}

const eda::Board& Document::boardData() const {
    return m_board;
}

Document::Mode Document::mode() const {
    return m_mode;
}

QString Document::filePath() const {
    return m_filePath;
}

void Document::setFilePath(const QString& path) {
    if (m_filePath == path)
        return;
    m_filePath = path;
    emit documentPathChanged(path);
}

bool Document::canUndo() const {
    return m_undoStack.canUndo();
}

bool Document::canRedo() const {
    return m_undoStack.canRedo();
}

bool Document::isClean() const {
    return m_undoStack.isClean();
}

void Document::markClean() {
    m_undoStack.setClean();
}

void Document::undo() {
    m_undoStack.undo();
}

void Document::redo() {
    m_undoStack.redo();
}

void Document::clearUndoHistory() {
    m_undoStack.clear();
    m_undoStack.setClean();
}

void Document::notifyModelChanged() {
    pruneSelection();
    emit modelChanged();
    emit repaintRequested();
    emit undoAvailabilityChanged(canUndo(), canRedo());
}

void Document::pruneSelection() {
    QVector<ObjectRef> valid;
    valid.reserve(m_selection.size());
    for (const auto& ref : m_selection) {
        if (m_mode == Mode::Footprint) {
            if (ref.type == ObjectType::Pad && m_footprint.findPad(ref.padId))
                valid.append(ref);
            continue;
        }
        if (ref.type == ObjectType::Instance) {
            const bool found = std::any_of(m_board.instances.begin(), m_board.instances.end(),
                                           [&](const eda::Instance& item) { return item.refdes == ref.refdes; });
            if (found)
                valid.append(ref);
        } else if (ref.type == ObjectType::Track) {
            const bool found = std::any_of(m_board.tracks.begin(), m_board.tracks.end(),
                                           [&](const eda::Track& item) { return item.id == ref.objectId; });
            if (found)
                valid.append(ref);
        } else if (ref.type == ObjectType::Via) {
            const bool found = std::any_of(m_board.vias.begin(), m_board.vias.end(),
                                           [&](const eda::Via& item) { return item.id == ref.objectId; });
            if (found)
                valid.append(ref);
        } else if (ref.type == ObjectType::Pour) {
            const bool found = std::any_of(m_board.pours.begin(), m_board.pours.end(),
                                           [&](const eda::CopperPour& item) { return item.id == ref.objectId; });
            if (found)
                valid.append(ref);
        }
    }
    if (!sameRefs(valid, m_selection)) {
        m_selection = std::move(valid);
        emit selectionChanged();
    }
}
