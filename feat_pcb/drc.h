#pragma once

#include "core/board.h"
#include "core/io.h"
#include "core/footprint.h"
#include "core/manufacturing_rules.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace eda {

namespace drc_code {
inline const QString kPadTrackClearance = QStringLiteral("PAD_TRACK_CLEARANCE");
inline const QString kTrackTrackClearance = QStringLiteral("TRACK_TRACK_CLEARANCE");
inline const QString kViaTrackClearance = QStringLiteral("VIA_TRACK_CLEARANCE");
inline const QString kPadPadClearance = QStringLiteral("PAD_PAD_CLEARANCE");
inline const QString kPadViaClearance = QStringLiteral("PAD_VIA_CLEARANCE");
inline const QString kPadPourClearance = QStringLiteral("PAD_POUR_CLEARANCE");
inline const QString kTrackPourClearance = QStringLiteral("TRACK_POUR_CLEARANCE");
inline const QString kViaPourClearance = QStringLiteral("VIA_POUR_CLEARANCE");
inline const QString kPourPourClearance = QStringLiteral("POUR_POUR_CLEARANCE");
inline const QString kPadEdgeClearance = QStringLiteral("PAD_EDGE_CLEARANCE");
inline const QString kTrackEdgeClearance = QStringLiteral("TRACK_EDGE_CLEARANCE");
inline const QString kViaEdgeClearance = QStringLiteral("VIA_EDGE_CLEARANCE");
inline const QString kPourEdgeClearance = QStringLiteral("POUR_EDGE_CLEARANCE");
} // namespace drc_code

struct DrcRules {
    double minClearanceMil = 7.874;        // 0.2 mm
    double boardEdgeClearanceMil = validation::kMinBoardEdgeClearanceMil;
    bool checkBoardEdge = true;
    bool checkPadTrack = true;
    bool checkTrackTrack = true;
    bool checkViaTrack = true;
    bool checkPadPad = true;
    bool checkPadVia = true;
    bool checkPadPour = true;
    bool checkTrackPour = true;
    bool checkViaPour = true;
    bool checkPourPour = true;
};

struct DrcViolation {
    QString code;
    QString message;
    double actualMil = 0.0;
    double requiredMil = 0.0;

    QString refdes;
    QString refdesB;
    QString netA;
    QString netB;
    PadId padId = kInvalidId;
    PadId relatedPadId = kInvalidId;
    TrackId trackIdA = kInvalidId;
    TrackId trackIdB = kInvalidId;
    ViaId viaId = kInvalidId;
    PourId pourId = kInvalidId;
    PourId relatedPourId = kInvalidId;
};

struct DrcResult {
    QVector<DrcViolation> violations;
    QStringList errors;   // 封装/焊盘解析失败等

    // 只表示本文件列出的间距检查（含板边）通过；丝印/阻焊等规则不在本函数内。
    [[nodiscard]] bool clean() const {
        return violations.isEmpty() && errors.isEmpty();
    }
};

// DRC：pad-track、track-track、via-track、pad-pad、pad-via、
// pad-pour、track-pour、via-pour、pour-pour。
// 单位/坐标沿用 Board 的 mil、board-world；调用前应保证 Board 结构合法。
// 同网络（net 非空且相等）不检查间距；未分配网络仍检查。
// pad-track/pad-via 的 required 取 max(pad.clearance, minClearance)；
// pad-pour 取 max(pad.clearance, pour.clearance, minClearance)；
// track-pour/via-pour 取 max(pour.clearance, minClearance)。
// 板边检查：焊盘/走线/过孔/覆铜到板框边的距离 >= boardEdgeClearanceMil。
// 焊盘制造规则由 board_validation 负责，本函数只做间距。
[[nodiscard]] DrcResult runDrc(const Board& board,
                               const FootprintResolver& resolveFootprint,
                               const DrcRules& rules = {});

} // namespace eda