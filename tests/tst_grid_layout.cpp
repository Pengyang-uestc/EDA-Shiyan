#include <QtTest>

#include "feat_pcb/grid_layout.h"

#include <cmath>
#include <limits>

using namespace eda;

namespace {

QPolygonF rectangle(double left, double top, double right, double bottom)
{
    QPolygonF polygon;
    polygon << QPointF(left, top)
            << QPointF(right, top)
            << QPointF(right, bottom)
            << QPointF(left, bottom);
    return polygon;
}

QPolygonF lShape()
{
    QPolygonF polygon;
    polygon << QPointF(0, 0) << QPointF(200, 0) << QPointF(200, 100)
            << QPointF(100, 100) << QPointF(100, 200) << QPointF(0, 200);
    return polygon;
}

bool rectanglesOverlap(const QRectF& a, const QRectF& b)
{
    return a.left() < b.right() && b.left() < a.right()
           && a.top() < b.bottom() && b.top() < a.bottom();
}

QRectF rectFor(const QPointF& center, const QSizeF& size)
{
    return QRectF(center.x() - size.width() / 2.0,
                  center.y() - size.height() / 2.0,
                  size.width(),
                  size.height());
}

} // namespace

class TestGridLayout : public QObject {
    Q_OBJECT

private slots:
    void emptyInput();
    void emptyInputStillValidatesArguments();
    void singleItemCentered();
    void exactFitAllowed();
    void fourItemsInTwoColumns();
    void varyingSizesUseColumnAndRowMaximums();
    void secondItemOutsideFailsWithoutPartialResult();
    void negativeGapFails();
    void nonFiniteGapFails();
    void zeroSizeFails();
    void nonFiniteSizeFails();
    void tooSmallOutlineFails();
    void twoPointOutlineFails();
    void concaveOutlineCenteredFails();
    void negativeCoordinates();
    void automaticColumns();
    void negativeColumnsMeanAuto();
};

void TestGridLayout::emptyInput()
{
    const LayoutResult result =
        gridLayout({}, rectangle(0, 0, 1000, 1000), 10.0, 2);

    QVERIFY(result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

void TestGridLayout::emptyInputStillValidatesArguments()
{
    QVERIFY(!gridLayout({}, rectangle(0, 0, 1000, 1000), -1.0, 2).status.ok);
    QVERIFY(!gridLayout({}, QPolygonF(), 10.0, 2).status.ok);
}

void TestGridLayout::singleItemCentered()
{
    const LayoutResult result =
        gridLayout({QSizeF(100, 50)}, rectangle(0, 0, 1000, 1000), 10.0, 1);

    QVERIFY(result.status.ok);
    QCOMPARE(result.positions.size(), 1);
    QVERIFY(std::abs(result.positions[0].x() - 500.0) < 1e-6);
    QVERIFY(std::abs(result.positions[0].y() - 500.0) < 1e-6);
}

void TestGridLayout::exactFitAllowed()
{
    const LayoutResult result =
        gridLayout({QSizeF(1000, 1000)}, rectangle(0, 0, 1000, 1000), 0.0, 1);

    QVERIFY(result.status.ok);
    QCOMPARE(result.positions.size(), 1);
    QVERIFY(std::abs(result.positions[0].x() - 500.0) < 1e-6);
    QVERIFY(std::abs(result.positions[0].y() - 500.0) < 1e-6);
}

void TestGridLayout::fourItemsInTwoColumns()
{
    const QVector<QSizeF> sizes{QSizeF(100, 50), QSizeF(100, 50),
                                QSizeF(100, 50), QSizeF(100, 50)};
    const LayoutResult result =
        gridLayout(sizes, rectangle(0, 0, 1000, 1000), 10.0, 2);

    QVERIFY(result.status.ok);
    QCOMPARE(result.positions.size(), 4);

    const QVector<QPointF> expected{
        QPointF(445, 470), QPointF(555, 470),
        QPointF(445, 530), QPointF(555, 530)};
    for (int i = 0; i < expected.size(); ++i) {
        QVERIFY(std::abs(result.positions[i].x() - expected[i].x()) < 1e-6);
        QVERIFY(std::abs(result.positions[i].y() - expected[i].y()) < 1e-6);
    }

    QVector<QRectF> rects;
    for (int i = 0; i < result.positions.size(); ++i) {
        rects.append(rectFor(result.positions[i], sizes[i]));
        QVERIFY(QRectF(0, 0, 1000, 1000).contains(rects[i]));
    }

    for (int i = 0; i < rects.size(); ++i) {
        for (int j = i + 1; j < rects.size(); ++j) {
            QVERIFY(!rectanglesOverlap(rects[i], rects[j]));
        }
    }
}

void TestGridLayout::varyingSizesUseColumnAndRowMaximums()
{
    const QVector<QSizeF> sizes{QSizeF(100, 50), QSizeF(50, 100)};
    const LayoutResult result =
        gridLayout(sizes, rectangle(0, 0, 1000, 1000), 10.0, 2);

    QVERIFY(result.status.ok);
    QCOMPARE(result.positions.size(), 2);
    QVERIFY(std::abs(result.positions[0].x() - 470.0) < 1e-6);
    QVERIFY(std::abs(result.positions[1].x() - 555.0) < 1e-6);
    QVERIFY(std::abs(result.positions[0].y() - 500.0) < 1e-6);
    QVERIFY(std::abs(result.positions[1].y() - 500.0) < 1e-6);

    const QRectF first = rectFor(result.positions[0], sizes[0]);
    const QRectF second = rectFor(result.positions[1], sizes[1]);
    QVERIFY(!rectanglesOverlap(first, second));
}

void TestGridLayout::secondItemOutsideFailsWithoutPartialResult()
{
    const QVector<QSizeF> sizes{QSizeF(100, 100), QSizeF(100, 100)};
    const LayoutResult result = gridLayout(sizes, lShape(), 0.0, 2);

    QVERIFY(!result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

void TestGridLayout::negativeGapFails()
{
    const LayoutResult result =
        gridLayout({QSizeF(100, 50)}, rectangle(0, 0, 1000, 1000), -1.0, 1);

    QVERIFY(!result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

void TestGridLayout::nonFiniteGapFails()
{
    const LayoutResult result =
        gridLayout({QSizeF(100, 50)}, rectangle(0, 0, 1000, 1000),
                   std::numeric_limits<double>::quiet_NaN(), 1);

    QVERIFY(!result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

void TestGridLayout::zeroSizeFails()
{
    const LayoutResult result =
        gridLayout({QSizeF(0, 50)}, rectangle(0, 0, 1000, 1000), 10.0, 1);

    QVERIFY(!result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

void TestGridLayout::nonFiniteSizeFails()
{
    const LayoutResult result =
        gridLayout({QSizeF(std::numeric_limits<double>::infinity(), 50)},
                   rectangle(0, 0, 1000, 1000), 10.0, 1);

    QVERIFY(!result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

void TestGridLayout::tooSmallOutlineFails()
{
    const QVector<QSizeF> sizes{QSizeF(60, 60), QSizeF(60, 60),
                                QSizeF(60, 60), QSizeF(60, 60)};
    const LayoutResult result =
        gridLayout(sizes, rectangle(0, 0, 100, 100), 10.0, 2);

    QVERIFY(!result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

void TestGridLayout::twoPointOutlineFails()
{
    QPolygonF outline;
    outline << QPointF(0, 0) << QPointF(100, 100);
    const LayoutResult result = gridLayout({QSizeF(10, 10)}, outline, 0.0, 1);

    QVERIFY(!result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

void TestGridLayout::concaveOutlineCenteredFails()
{
    // 只做整组居中；L 形板框的居中点落在空洞里，按约定返回失败。
    const LayoutResult result =
        gridLayout({QSizeF(100, 100)}, lShape(), 10.0, 1);

    QVERIFY(!result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

void TestGridLayout::negativeCoordinates()
{
    const LayoutResult result =
        gridLayout({QSizeF(100, 50)}, rectangle(-500, -500, 500, 500), 0.0, 1);

    QVERIFY(result.status.ok);
    QCOMPARE(result.positions.size(), 1);
    QVERIFY(std::abs(result.positions[0].x()) < 1e-6);
    QVERIFY(std::abs(result.positions[0].y()) < 1e-6);
}

void TestGridLayout::automaticColumns()
{
    const QVector<QSizeF> sizes{QSizeF(50, 50), QSizeF(50, 50),
                                QSizeF(50, 50), QSizeF(50, 50),
                                QSizeF(50, 50)};
    const LayoutResult result =
        gridLayout(sizes, rectangle(0, 0, 1000, 1000), 10.0, 0);

    QVERIFY(result.status.ok);
    QCOMPARE(result.positions.size(), 5);
}

void TestGridLayout::negativeColumnsMeanAuto()
{
    const QVector<QSizeF> sizes{QSizeF(50, 50), QSizeF(50, 50),
                                QSizeF(50, 50), QSizeF(50, 50),
                                QSizeF(50, 50)};
    const LayoutResult result =
        gridLayout(sizes, rectangle(0, 0, 1000, 1000), 10.0, -3);

    QVERIFY(result.status.ok);
    QCOMPARE(result.positions.size(), 5);
}

QTEST_APPLESS_MAIN(TestGridLayout)

#include "tst_grid_layout.moc"