#pragma once

#include "core/board.h"
#include "core/io.h"

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QVector>

namespace eda {

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