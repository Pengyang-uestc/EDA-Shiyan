#pragma once

#include "feat_pcb/drc.h"

#include <QString>
#include <QVector>

namespace eda {

// DRC 结果的 UI 视图。runDrc 把“间距违规”和“封装/焊盘解析失败”分成两组，
// 这里统一成可定位的记录，B 的检查面板直接消费，不用自己解析 DrcResult。
enum class DrcIssueKind {
    Clearance,   // 来自 DrcResult::violations
    Resolution,  // 来自 DrcResult::errors
};

struct DrcIssue {
    DrcIssueKind kind = DrcIssueKind::Clearance;
    QString code;
    QString message;
    QString refdes;
    QString refdesB;
    double actualMil = 0.0;
    double requiredMil = 0.0;
    PadId padId = kInvalidId;
    PadId relatedPadId = kInvalidId;
    TrackId trackId = kInvalidId;
    TrackId relatedTrackId = kInvalidId;
    ViaId viaId = kInvalidId;
    PourId pourId = kInvalidId;
    PourId relatedPourId = kInvalidId;
};

inline const QString kDrcResolveError = QStringLiteral("DRC_RESOLVE_ERROR");

// 纯映射，不改 DrcResult；顺序是先 violations 后 errors。
[[nodiscard]] QVector<DrcIssue> collectDrcIssues(const DrcResult& result);

// 便捷入口：跑一次 runDrc 再映射。
[[nodiscard]] QVector<DrcIssue> collectDrcIssues(const Board& board,
                                                 const FootprintResolver& resolveFootprint,
                                                 const DrcRules& rules = {});

} // namespace eda