#pragma once

#include "types.h"

#include <QString>

namespace eda {

enum class IssueSeverity { Error, Warning };

struct Issue {
    IssueSeverity severity = IssueSeverity::Error;
    QString code;
    QString message;
    PadId padId = kInvalidId;
    PadId relatedPadId = kInvalidId;
    int pinNumber = -1;
    QString refdes;
    quint64 objectId = 0;
    LayerId layerId = -1;
    LayerGroupId groupId = -1;
};

namespace issue_code {

inline const QString kStructure = QStringLiteral("STRUCTURE");
inline const QString kDrill = QStringLiteral("DRILL");
inline const QString kAnnularRing = QStringLiteral("ANNULAR_RING");
inline const QString kMaskMargin = QStringLiteral("MASK_MARGIN");
inline const QString kClearance = QStringLiteral("CLEARANCE");
inline const QString kPinPadDuplicate = QStringLiteral("PIN_PAD_DUPLICATE");
inline const QString kPadSpacing = QStringLiteral("PAD_SPACING");
inline const QString kPadSpacingUnchecked = QStringLiteral("PAD_SPACING_UNCHECKED");
inline const QString kPadUnreferenced = QStringLiteral("PAD_UNREFERENCED");
inline const QString kPadNumberDuplicate = QStringLiteral("PAD_NUMBER_DUPLICATE");
inline const QString kHoleDrill = QStringLiteral("HOLE_DRILL");
inline const QString kSilkWidth = QStringLiteral("SILK_WIDTH");
inline const QString kCircleNotSquare = QStringLiteral("CIRCLE_NOT_SQUARE");

inline const QString kBoardStructure = QStringLiteral("BOARD_STRUCTURE");
inline const QString kBoardReferences = QStringLiteral("BOARD_REFERENCES");
inline const QString kBoardLayerMissing = QStringLiteral("BOARD_LAYER_MISSING");
inline const QString kBoardGroupOrder = QStringLiteral("BOARD_GROUP_ORDER");
inline const QString kBoardLayerOrder = QStringLiteral("BOARD_LAYER_ORDER");
inline const QString kBoardInstanceOutside = QStringLiteral("BOARD_INSTANCE_OUTSIDE");
inline const QString kBoardFootprint = QStringLiteral("BOARD_FOOTPRINT");
inline const QString kBoardFootprintUnchecked = QStringLiteral("BOARD_FOOTPRINT_UNCHECKED");
inline const QString kTrackWidth = QStringLiteral("TRACK_WIDTH");
inline const QString kTrackOutside = QStringLiteral("TRACK_OUTSIDE");
inline const QString kNetUnassigned = QStringLiteral("NET_UNASSIGNED");
inline const QString kViaDrill = QStringLiteral("VIA_DRILL");
inline const QString kViaAnnularRing = QStringLiteral("VIA_ANNULAR_RING");
inline const QString kViaOutside = QStringLiteral("VIA_OUTSIDE");
inline const QString kPourClearance = QStringLiteral("POUR_CLEARANCE");
inline const QString kPourOutside = QStringLiteral("POUR_OUTSIDE");
inline const QString kPourPolygon = QStringLiteral("POUR_POLYGON");
inline const QString kPourLayer = QStringLiteral("POUR_LAYER");
inline const QString kPourNet = QStringLiteral("POUR_NET");
inline const QString kPourPriority = QStringLiteral("POUR_PRIORITY");
inline const QString kPourId = QStringLiteral("POUR_ID");

} // namespace issue_code

} // namespace eda