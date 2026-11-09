#include "feat_pcb/grid_layout.h"

#include "core/geometry.h"

#include <QPainterPath>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <cmath>

namespace eda {
namespace {

constexpr double kLayoutEpsilonMil = 1e-6;

bool isPositiveFinite(double value)
{
    return std::isfinite(value) && value > 0.0;
}

LayoutResult failure(const QString& message)
{
    LayoutResult result;
    result.status = Status{false, message};
    return result;
}

} // namespace

LayoutResult gridLayout(const QVector<QSizeF>& sizesMil,
                        const QPolygonF& outlineMil,
                        double gapMil,
                        int columns)
{
    LayoutResult result;

    if (!std::isfinite(gapMil) || gapMil < 0.0) {
        return failure(QStringLiteral("间距必须是非负有限数"));
    }

    if (outlineMil.size() < 3) {
        return failure(QStringLiteral("板框至少需要 3 个点"));
    }
    if (!isSimplePolygon(outlineMil)) {
        return failure(QStringLiteral("板框必须是简单多边形"));
    }

    for (int i = 0; i < sizesMil.size(); ++i) {
        if (!isPositiveFinite(sizesMil[i].width())
            || !isPositiveFinite(sizesMil[i].height())) {
            return failure(QStringLiteral("第 %1 个元件的尺寸非法").arg(i + 1));
        }
    }

    if (sizesMil.isEmpty()) {
        result.status = Status{true, QString()};
        return result;
    }

    const int count = sizesMil.size();
    int columnCount = columns > 0
                          ? columns
                          : static_cast<int>(std::ceil(std::sqrt(count)));
    columnCount = std::clamp(columnCount, 1, count);
    const int rowCount = (count + columnCount - 1) / columnCount;

    QVector<double> columnWidths(columnCount, 0.0);
    QVector<double> rowHeights(rowCount, 0.0);

    for (int i = 0; i < count; ++i) {
        const int column = i % columnCount;
        const int row = i / columnCount;
        columnWidths[column] = std::max(columnWidths[column], sizesMil[i].width());
        rowHeights[row] = std::max(rowHeights[row], sizesMil[i].height());
    }

    double totalWidth = gapMil * (columnCount - 1);
    for (double width : columnWidths) {
        totalWidth += width;
    }

    double totalHeight = gapMil * (rowCount - 1);
    for (double height : rowHeights) {
        totalHeight += height;
    }

    const QRectF bounds = outlineMil.boundingRect();
    if (totalWidth > bounds.width() + kLayoutEpsilonMil
        || totalHeight > bounds.height() + kLayoutEpsilonMil) {
        return failure(QStringLiteral("板框内放不下这些元件"));
    }

    QVector<double> columnLefts(columnCount, 0.0);
    QVector<double> rowTops(rowCount, 0.0);

    const double originX = bounds.center().x() - totalWidth / 2.0;
    const double originY = bounds.center().y() - totalHeight / 2.0;

    for (int column = 1; column < columnCount; ++column) {
        columnLefts[column] = columnLefts[column - 1]
                              + columnWidths[column - 1] + gapMil;
    }
    for (int row = 1; row < rowCount; ++row) {
        rowTops[row] = rowTops[row - 1] + rowHeights[row - 1] + gapMil;
    }

    QPainterPath outlinePath;
    outlinePath.addPolygon(outlineMil);
    outlinePath.closeSubpath();

    result.positions.reserve(count);

    for (int i = 0; i < count; ++i) {
        const int column = i % columnCount;
        const int row = i / columnCount;

        const double width = sizesMil[i].width();
        const double height = sizesMil[i].height();
        const double left = originX + columnLefts[column]
                            + (columnWidths[column] - width) / 2.0;
        const double top = originY + rowTops[row]
                           + (rowHeights[row] - height) / 2.0;

        const QRectF rect(left, top, width, height);

        // 向内缩一个 epsilon，允许矩形贴边；直接 contains(rect) 会把贴边判成越界。
        const double shrink = std::min({kLayoutEpsilonMil, width / 4.0, height / 4.0});
        const QRectF probe = rect.adjusted(shrink, shrink, -shrink, -shrink);
        if (probe.width() <= 0.0 || probe.height() <= 0.0
            || !outlinePath.contains(probe)) {
            return failure(QStringLiteral("第 %1 个元件超出板框").arg(i + 1));
        }

        result.positions.append(rect.center());
    }

    result.status = Status{true, QString()};
    return result;
}

} // namespace eda