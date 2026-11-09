#pragma once

#include "core/board.h"
#include "core/io.h"

#include <QString>
#include <QVector>

namespace eda {

struct NetConnectivity {
    QString name;
    bool connected = false;
    int componentCount = 0;   // 该网络引脚落在几个物理连通分量里
};

struct ConnectivityResult {
    QVector<NetConnectivity> nets;
    QVector<QString> shortedNets;   // 同一物理分量里出现多个网络名
    QVector<QString> errors;        // 封装/焊盘解析失败等

    // 所有网络各自单连通且没有短路才返回 true。
    [[nodiscard]] bool allNetsConnected() const;
    [[nodiscard]] bool hasShorts() const { return !shortedNets.isEmpty(); }
};

// 物理连通检查，单位/坐标沿用 Board 的 mil、board-world。
// 调用前应保证 Board 结构合法（validateBoardStructure 通过）；本函数不重复做结构校验。
// 节点：实例引脚、整条走线、过孔、覆铜。边：同层铜体接触；过孔连接 from/to 层；
// 通孔焊盘（drill > 0）同时连接顶层和底层铜。
// 接触按铜体近似：走线用宽度、过孔用直径、圆形焊盘用半径；
// 矩形焊盘用轴对齐矩形，长圆/圆角矩形按外接矩形，属于课程级近似。
// 不按 net 标签合并；net 只用于最后判断“同一网络是否连通”和“是否短路”。
// 封装通过 resolver 解析；解析失败写入 errors，不抛异常。
[[nodiscard]] ConnectivityResult analyzeConnectivity(
    const Board& board,
    const FootprintResolver& resolveFootprint);

} // namespace eda