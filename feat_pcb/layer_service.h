#pragma once

#include "core/board.h"
#include "core/validation.h"

#include <QColor>
#include <QString>
#include <QVector>

namespace eda {

// 层组/层定义管理：默认 7 层、组/层增删、颜色、stackOrder、命名、校验。
// active/visible 是会话状态，归 app 的 DocumentContext；本服务只管持久化层定义。
// 这些函数直接修改 Board；B 的层面板操作必须包在 QUndoCommand 里。
class LayerService {
  public:
    [[nodiscard]] static Status ensureDefaultLayers(Board& board);

    [[nodiscard]] static Result<LayerGroupId> addGroup(
        Board& board, const QString& name, int stackOrder);
    [[nodiscard]] static Status removeGroup(Board& board, LayerGroupId id);

    [[nodiscard]] static Result<LayerId> addLayer(
        Board& board, LayerGroupId groupId, const QString& name,
        BoardLayerType type, const QColor& color, int stackOrder);
    [[nodiscard]] static Status removeLayer(Board& board, LayerId id);

    [[nodiscard]] static Status renameLayer(Board& board, LayerId id,
                                             const QString& name);
    [[nodiscard]] static Status setLayerColor(Board& board, LayerId id,
                                               const QColor& color);
    [[nodiscard]] static Status setLayerStackOrder(Board& board, LayerId id,
                                                    int stackOrder);
    [[nodiscard]] static Status setGroupStackOrder(Board& board,
                                                    LayerGroupId id,
                                                    int stackOrder);

    [[nodiscard]] static QVector<Issue> validate(const Board& board);
};

} // namespace eda