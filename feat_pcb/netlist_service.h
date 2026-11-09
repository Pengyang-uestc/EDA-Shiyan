#pragma once

#include "core/board.h"
#include "core/io.h"

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QVector>

namespace eda {

// 网表初始布局：以 origin 为左上角、按全局最大 cell 排列，用于导入后立刻有可用板面。
// 它与 grid_layout.h 的 gridLayout()（整组居中、每列/每行取最大尺寸）是两套语义；
// B 的“自动布局”按钮应调用 gridLayout()/后续 layoutInstances()，不要复用本服务的初始坐标。
struct NetlistImportOptions {
    QPointF origin{200, 200};
    double gapMil = 100.0;
    int columns = 0;   // <=0: ceil(sqrt(componentCount))
};

struct NetlistImportResult {
    Board board;
    QVector<QString> warnings;
};

// 网表 → Board 的导入流程：解析 COMP/NET、解析封装、建实例、建网络、
// 按规则网格给出初始坐标，最后跑 validateBoard。
// 失败返回 Status，原 Board 不变（调用方用返回值整体替换）。
class NetlistService {
  public:
    [[nodiscard]] Result<NetlistImportResult> importData(
        const QByteArray& data,
        const FootprintResolver& resolveFootprint,
        const NetlistImportOptions& options = {}) const;

    [[nodiscard]] Result<NetlistImportResult> importFile(
        const QString& path,
        const FootprintResolver& resolveFootprint,
        const NetlistImportOptions& options = {}) const;
};

} // namespace eda