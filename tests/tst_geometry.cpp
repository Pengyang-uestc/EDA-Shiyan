#include <QtTest>

#include "core/geometry.h"

#include <cmath>

using namespace eda;

namespace {

QPolygonF polygon(std::initializer_list<QPointF> points)
{
    QPolygonF result;
    for (const QPointF& point : points) {
        result << point;
    }
    return result;
}

bool near(double a, double b, double epsilon = 1e-6)
{
    return std::abs(a - b) <= epsilon;
}

} // namespace

class TestGeometry : public QObject {
    Q_OBJECT

private slots:
    void signedAreaCcwPositive();
    void signedAreaCwNegative();
    void simplePolygonAcceptsSquare();
    void simplePolygonRejectsTooFewPoints();
    void simplePolygonRejectsBowTie();
    void simplePolygonRejectsRepeatedAdjacentPoint();
    void simplePolygonRejectsCollinearDoublingBack();
    void simplePolygonAcceptsCollinearStraight();
    void containsPointInsideOutsideBoundary();
    void containsPointConcavePolygon();
    void segmentsIntersectCrossing();
    void segmentsIntersectAtEndpoint();
    void segmentsParallelDoNotIntersect();
    void segmentsCollinearOverlap();
    void distancePointToSegment();
    void distancePointToDegenerateSegment();
    void footprintToBoardTopRotation();
    void footprintToBoardBottomMirrorsX();
    void footprintToBoardBottomRotation();
    void footprintToBoardRoundTrip();
    void mapFootprintLayerToBoard();
};

void TestGeometry::signedAreaCcwPositive()
{
    QVERIFY(near(signedArea(polygon({{0, 0}, {100, 0}, {100, 100}, {0, 100}})),
                 10000.0));
}

void TestGeometry::signedAreaCwNegative()
{
    QVERIFY(near(signedArea(polygon({{0, 0}, {0, 100}, {100, 100}, {100, 0}})),
                 -10000.0));
}

void TestGeometry::simplePolygonAcceptsSquare()
{
    QVERIFY(isSimplePolygon(polygon({{0, 0}, {100, 0}, {100, 100}, {0, 100}})));
}

void TestGeometry::simplePolygonRejectsTooFewPoints()
{
    QVERIFY(!isSimplePolygon(polygon({{0, 0}, {100, 0}})));
    QVERIFY(!isSimplePolygon(QPolygonF()));
}

void TestGeometry::simplePolygonRejectsBowTie()
{
    QVERIFY(!isSimplePolygon(polygon({{0, 0}, {100, 100}, {0, 100}, {100, 0}})));
}

void TestGeometry::simplePolygonRejectsRepeatedAdjacentPoint()
{
    QVERIFY(!isSimplePolygon(polygon({{0, 0}, {100, 0}, {100, 0}, {100, 100}})));
}

void TestGeometry::simplePolygonRejectsCollinearDoublingBack()
{
    QVERIFY(!isSimplePolygon(polygon({{0, 0}, {100, 0}, {50, 0}, {50, 100}})));
}

void TestGeometry::simplePolygonAcceptsCollinearStraight()
{
    QVERIFY(isSimplePolygon(polygon({{0, 0}, {100, 0}, {200, 0}, {200, 100}, {0, 100}})));
}

void TestGeometry::containsPointInsideOutsideBoundary()
{
    const QPolygonF square = polygon({{0, 0}, {100, 0}, {100, 100}, {0, 100}});
    QVERIFY(containsPoint(square, QPointF(50, 50)));
    QVERIFY(!containsPoint(square, QPointF(150, 50)));
    QVERIFY(containsPoint(square, QPointF(0, 50)));
    QVERIFY(containsPoint(square, QPointF(0, 0)));
}

void TestGeometry::containsPointConcavePolygon()
{
    const QPolygonF shape = polygon({{0, 0}, {200, 0}, {200, 100},
                                     {100, 100}, {100, 200}, {0, 200}});
    QVERIFY(containsPoint(shape, QPointF(50, 50)));
    QVERIFY(containsPoint(shape, QPointF(150, 50)));
    QVERIFY(containsPoint(shape, QPointF(50, 150)));
    QVERIFY(!containsPoint(shape, QPointF(150, 150)));
}

void TestGeometry::segmentsIntersectCrossing()
{
    QVERIFY(segmentsIntersect(QPointF(0, 0), QPointF(10, 10),
                              QPointF(0, 10), QPointF(10, 0)));
}

void TestGeometry::segmentsIntersectAtEndpoint()
{
    QVERIFY(segmentsIntersect(QPointF(0, 0), QPointF(10, 0),
                              QPointF(10, 0), QPointF(20, 10)));
}

void TestGeometry::segmentsParallelDoNotIntersect()
{
    QVERIFY(!segmentsIntersect(QPointF(0, 0), QPointF(10, 0),
                               QPointF(0, 10), QPointF(10, 10)));
}

void TestGeometry::segmentsCollinearOverlap()
{
    QVERIFY(segmentsIntersect(QPointF(0, 0), QPointF(10, 0),
                              QPointF(5, 0), QPointF(15, 0)));
    QVERIFY(!segmentsIntersect(QPointF(0, 0), QPointF(10, 0),
                               QPointF(11, 0), QPointF(15, 0)));
}

void TestGeometry::distancePointToSegment()
{
    QVERIFY(near(eda::distancePointToSegment(QPointF(5, 5), QPointF(0, 0), QPointF(10, 0)), 5.0));
    QVERIFY(near(eda::distancePointToSegment(QPointF(5, -5), QPointF(0, 0), QPointF(10, 0)), 5.0));
    QVERIFY(near(eda::distancePointToSegment(QPointF(20, 0), QPointF(0, 0), QPointF(10, 0)), 10.0));
    QVERIFY(near(eda::distancePointToSegment(QPointF(5, 0), QPointF(0, 0), QPointF(10, 0)), 0.0));
}

void TestGeometry::distancePointToDegenerateSegment()
{
    QVERIFY(near(eda::distancePointToSegment(QPointF(5, 5), QPointF(0, 0), QPointF(0, 0)),
                 std::hypot(5.0, 5.0)));
}

void TestGeometry::footprintToBoardTopRotation()
{
    const QPointF result = footprintToBoard(QPointF(100, 0), QPointF(1000, 500),
                                            90.0, BoardSide::Top);
    QVERIFY(near(result.x(), 1000.0));
    QVERIFY(near(result.y(), 600.0));
}

void TestGeometry::footprintToBoardBottomMirrorsX()
{
    const QPointF result = footprintToBoard(QPointF(100, 0), QPointF(1000, 500),
                                            0.0, BoardSide::Bottom);
    QVERIFY(near(result.x(), 900.0));
    QVERIFY(near(result.y(), 500.0));
}

void TestGeometry::footprintToBoardBottomRotation()
{
    const QPointF result = footprintToBoard(QPointF(100, 0), QPointF(1000, 500),
                                            90.0, BoardSide::Bottom);
    QVERIFY(near(result.x(), 1000.0));
    QVERIFY(near(result.y(), 400.0));
}

void TestGeometry::footprintToBoardRoundTrip()
{
    const QVector<QPointF> localPoints{{0, 0}, {100, 0}, {0, 50}, {-30, 20}};
    const QVector<double> rotations{0.0, 90.0, 37.0, -120.0};

    for (BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
        for (double rotation : rotations) {
            const QPointF position(1000, 500);
            for (const QPointF& local : localPoints) {
                const QPointF board = footprintToBoard(local, position, rotation, side);
                const QPointF back = boardToFootprint(board, position, rotation, side);
                QVERIFY2(near(back.x(), local.x(), 1e-6) && near(back.y(), local.y(), 1e-6),
                         qPrintable(QStringLiteral("round trip failed: side=%1 rot=%2")
                                        .arg(side == BoardSide::Top ? "top" : "bottom")
                                        .arg(rotation)));
            }
        }
    }
}

void TestGeometry::mapFootprintLayerToBoard()
{
    QCOMPARE(eda::mapFootprintLayerToBoard(kTopCopper, BoardSide::Top), kTopCopper);
    QCOMPARE(eda::mapFootprintLayerToBoard(kTopCopper, BoardSide::Bottom), kBottomCopper);
    QCOMPARE(eda::mapFootprintLayerToBoard(kBottomCopper, BoardSide::Bottom), kTopCopper);
    QCOMPARE(eda::mapFootprintLayerToBoard(kTopSilk, BoardSide::Bottom), kBottomSilk);
    QCOMPARE(eda::mapFootprintLayerToBoard(kTopMask, BoardSide::Bottom), kBottomMask);
    QCOMPARE(eda::mapFootprintLayerToBoard(kBoardOutline, BoardSide::Bottom), kBoardOutline);
    QCOMPARE(eda::mapFootprintLayerToBoard(kTopSilk, BoardSide::Top), kTopSilk);
    QCOMPARE(eda::mapFootprintLayerToBoard(kBottomSilk, BoardSide::Bottom), kTopSilk);
    QCOMPARE(eda::mapFootprintLayerToBoard(kBottomMask, BoardSide::Bottom), kTopMask);
}

QTEST_APPLESS_MAIN(TestGeometry)

#include "tst_geometry.moc"