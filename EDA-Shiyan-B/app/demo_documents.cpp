#include "app/demo_documents.h"

eda::Board exampleBoard() {
    eda::Board board;
    board.groups = {
        {1, QStringLiteral("Top"), 0},
        {2, QStringLiteral("Bottom"), 1},
    };
    board.layers = {
        {eda::kTopCopper, 1, QStringLiteral("Top Copper"), eda::BoardLayerType::Copper,
         QColor(QStringLiteral("#B64845")), 0},
        {eda::kBottomCopper, 2, QStringLiteral("Bottom Copper"), eda::BoardLayerType::Copper,
         QColor(QStringLiteral("#3D70A8")), 0},
        {eda::kTopSilk, 1, QStringLiteral("Top Silk"), eda::BoardLayerType::Silk, QColor(QStringLiteral("#756824")), 1},
        {eda::kBottomSilk, 2, QStringLiteral("Bottom Silk"), eda::BoardLayerType::Silk,
         QColor(QStringLiteral("#657443")), 1},
        {eda::kTopMask, 1, QStringLiteral("Top Mask"), eda::BoardLayerType::Mask, QColor(QStringLiteral("#6B8D70")), 2},
        {eda::kBottomMask, 2, QStringLiteral("Bottom Mask"), eda::BoardLayerType::Mask,
         QColor(QStringLiteral("#4D7B7F")), 2},
        {eda::kBoardOutline, 1, QStringLiteral("Board Outline"), eda::BoardLayerType::Outline,
         QColor(QStringLiteral("#33473C")), 3},
    };
    board.outline << QPointF(0, 0) << QPointF(2400, 0) << QPointF(2400, 1400) << QPointF(0, 1400);
    board.instances = {
        {QStringLiteral("U1"), QStringLiteral("example"), QPointF(520, 520), 0.0, eda::BoardSide::Top},
        {QStringLiteral("U2"), QStringLiteral("example"), QPointF(1700, 760), 180.0, eda::BoardSide::Bottom},
    };
    board.tracks = {
        {1,
         eda::kTopCopper,
         12.0,
         QStringLiteral("N001"),
         {QPointF(520, 520), QPointF(980, 520), QPointF(980, 860), QPointF(1450, 860)}},
        {2,
         eda::kBottomCopper,
         14.0,
         QStringLiteral("GND"),
         {QPointF(1700, 760), QPointF(1700, 1120), QPointF(520, 1120)}},
    };
    board.vias = {
        {1, QPointF(980, 520), 24.0, 44.0, eda::kTopCopper, eda::kBottomCopper, QStringLiteral("N001")},
    };
    board.pours = {
        {1, eda::kTopCopper, QStringLiteral("GND"), 12.0, 0,
         QPolygonF() << QPointF(120, 120) << QPointF(2280, 120) << QPointF(2280, 1280) << QPointF(120, 1280)},
    };
    board.nets = {
        {QStringLiteral("N001"), {{QStringLiteral("U1"), 1}, {QStringLiteral("U1"), 2}}},
        {QStringLiteral("GND"),
         {{QStringLiteral("U1"), 3}, {QStringLiteral("U1"), 4}, {QStringLiteral("U2"), 1}, {QStringLiteral("U2"), 2}}},
    };
    board.nextTrackId = 3;
    board.nextViaId = 2;
    board.nextPourId = 2;
    return board;
}
