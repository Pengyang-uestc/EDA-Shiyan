#pragma once

#include "core/board.h"

#include <QStandardItemModel>

namespace eda {

// 只读的层树模型：QTreeView 可直接使用。
// 第 0 列名称，第 1 列类型，第 2 列颜色，第 3 列 stackOrder。
// active/visible 不在模型里，仍归 DocumentContext。
class LayerModel : public QStandardItemModel {
  public:
    enum Role {
        LayerIdRole = Qt::UserRole + 1,
        GroupIdRole,
        LayerTypeRole,
        ColorRole,
        StackOrderRole,
        IsGroupRole
    };

    explicit LayerModel(QObject* parent = nullptr);

    void setBoard(const Board* board);   // 非拥有
    const Board* board() const { return m_board; }
    void refresh();

    [[nodiscard]] QModelIndex indexForLayer(LayerId id) const;
    [[nodiscard]] LayerId layerId(const QModelIndex& index) const;

  private:
    const Board* m_board = nullptr;
};

} // namespace eda