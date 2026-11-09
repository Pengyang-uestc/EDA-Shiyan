#pragma once

#include "status.h"
#include "types.h"

namespace eda {

enum class BoardLayerType { Copper, Silk, Mask, Outline };
enum class BoardSide { Top, Bottom };

struct LayerGroup {
    LayerGroupId id = 0;
    QString name;
    int stackOrder = 0;
};

struct BoardLayer {
    LayerId id = kTopCopper;
    LayerGroupId groupId = 0;
    QString name;
    BoardLayerType type = BoardLayerType::Copper;
    QColor color;
    int stackOrder = 0;
};

struct Instance {
    QString refdes;
    QString footprintId;
    QPointF pos;
    double rotationDeg = 0.0;
    BoardSide side = BoardSide::Top;
};

struct Track {
    TrackId id = kInvalidId;
    LayerId layerId = kTopCopper;
    double width = 12.0; // mil
    QString net;         // empty means unassigned
    QVector<QPointF> points;
};

struct Via {
    ViaId id = kInvalidId;
    QPointF pos;
    double drill = 24.0;    // mil
    double diameter = 40.0; // mil
    LayerId fromLayer = kTopCopper;
    LayerId toLayer = kBottomCopper;
    QString net; // empty means unassigned
};

struct CopperPour {
    PourId id = kInvalidId;
    LayerId layerId = kTopCopper;
    QString net; // empty means unassigned
    double clearance = 10.0; // mil
    int priority = 0;
    QPolygonF points;
};

struct PinRef {
    QString refdes;
    int pinNumber = 0;
};

struct Net {
    QString name;
    QVector<PinRef> pins;
};

struct Board {
    QPolygonF outline; // board-world coordinates; counter-clockwise
    QVector<LayerGroup> groups;
    QVector<BoardLayer> layers;
    QVector<Instance> instances;
    QVector<Track> tracks;
    QVector<Via> vias;
    QVector<CopperPour> pours;
    QVector<Net> nets;

    TrackId nextTrackId = 1;
    ViaId nextViaId = 1;
    PourId nextPourId = 1;

    TrackId allocateTrackId() noexcept;
    ViaId allocateViaId() noexcept;
    PourId allocatePourId() noexcept;

    [[nodiscard]] Status insertTrack(const Track& track);
    [[nodiscard]] Status removeTrack(TrackId id);
    [[nodiscard]] Status insertVia(const Via& via);
    [[nodiscard]] Status removeVia(ViaId id);
    [[nodiscard]] Status insertCopperPour(const CopperPour& pour);
    [[nodiscard]] Status removeCopperPour(PourId id);
};

// Structural checks only; manufacturing rules belong to C.
[[nodiscard]] Status validateBoardStructure(const Board& board);

} // namespace eda
