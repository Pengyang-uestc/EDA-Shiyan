#pragma once

#include <QVector>
#include <QtGlobal>

namespace eda {

// 连通检查的地基；节点编号为 0 .. count-1。
// 无效索引返回 -1，unite/connected 返回 false。
class UnionFind {
public:
    explicit UnionFind(int count = 0);

    [[nodiscard]] int find(int x);          // 带路径压缩
    [[nodiscard]] int root(int x) const;    // 只读，不压缩
    [[nodiscard]] bool unite(int a, int b);
    [[nodiscard]] bool connected(int a, int b) const;
    [[nodiscard]] qsizetype count() const { return m_parent.size(); }

private:
    QVector<int> m_parent;
    QVector<int> m_rank;
};

} // namespace eda