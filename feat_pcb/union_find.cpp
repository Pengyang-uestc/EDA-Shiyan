#include "feat_pcb/union_find.h"

#include <algorithm>

namespace eda {

UnionFind::UnionFind(int count)
    : m_parent(std::max(count, 0))
    , m_rank(std::max(count, 0), 0)
{
    for (qsizetype i = 0; i < m_parent.size(); ++i) {
        m_parent[i] = static_cast<int>(i);
    }
}

int UnionFind::find(int x)
{
    if (x < 0 || static_cast<qsizetype>(x) >= m_parent.size()) {
        return -1;
    }

    int rootIndex = x;
    while (m_parent[rootIndex] != rootIndex) {
        rootIndex = m_parent[rootIndex];
    }

    while (m_parent[x] != x) {
        const int parent = m_parent[x];
        m_parent[x] = rootIndex;
        x = parent;
    }

    return rootIndex;
}

int UnionFind::root(int x) const
{
    if (x < 0 || static_cast<qsizetype>(x) >= m_parent.size()) {
        return -1;
    }

    while (m_parent[x] != x) {
        x = m_parent[x];
    }
    return x;
}

bool UnionFind::unite(int a, int b)
{
    const int rootA = find(a);
    const int rootB = find(b);
    if (rootA < 0 || rootB < 0) {
        return false;
    }
    if (rootA == rootB) {
        return false;
    }

    if (m_rank[rootA] < m_rank[rootB]) {
        m_parent[rootA] = rootB;
    } else if (m_rank[rootA] > m_rank[rootB]) {
        m_parent[rootB] = rootA;
    } else {
        m_parent[rootB] = rootA;
        ++m_rank[rootA];
    }
    return true;
}

bool UnionFind::connected(int a, int b) const
{
    const int rootA = root(a);
    const int rootB = root(b);
    return rootA >= 0 && rootA == rootB;
}

} // namespace eda