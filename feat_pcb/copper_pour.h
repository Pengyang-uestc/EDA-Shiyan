#pragma once

#include "core/board.h"
#include "core/validation.h"

#include <QPolygonF>
#include <QString>
#include <QVector>

namespace eda {

struct PourCreateOptions {
    QPolygonF polygon;
    LayerId layerId = kTopCopper;
    QString net;              // empty = unassigned
    double clearance = 10.0;  // mil
    int priority = 0;
};

// 覆铜管理：创建/删除/改多边形/网络/层/间距/优先级/重建校验。
// 注意：这些函数直接修改 Board；B 的 PourTool 必须在 QUndoCommand 中调用
// （前后 Board 快照），不能绕过命令栈。
// 不直接画填充几何；rebuild 只做校验，不重新计算填充/避让。
class CopperPourService {
  public:
    [[nodiscard]] static Result<PourId> add(Board& board,
                                            const PourCreateOptions& options);
    [[nodiscard]] static Status remove(Board& board, PourId id);

    [[nodiscard]] static Status setPolygon(Board& board, PourId id,
                                           const QPolygonF& polygon);
    [[nodiscard]] static Status setLayer(Board& board, PourId id,
                                         LayerId layerId);
    [[nodiscard]] static Status setNet(Board& board, PourId id,
                                       const QString& net);
    [[nodiscard]] static Status setClearance(Board& board, PourId id,
                                             double clearanceMil);
    [[nodiscard]] static Status setPriority(Board& board, PourId id,
                                            int priority);

    // 本版本不做几何填充，rebuild 只做一次完整校验。
    [[nodiscard]] static Status rebuild(const Board& board, PourId id);
    [[nodiscard]] static QVector<Issue> validate(const Board& board);
};

} // namespace eda