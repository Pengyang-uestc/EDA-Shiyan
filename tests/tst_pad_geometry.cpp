#include <QtTest>

#include "core/geometry.h"
#include "core/footprint.h"
#include "feat_pcb/pad_geometry.h"

#include <cmath>

using namespace eda;

namespace {

bool near(double a, double b, double epsilon = 1e-6)
{
    return std::abs(a - b) <= epsilon;
}

Pad makePad(PadShape shape, QSizeF size, QPointF pos = QPointF(0, 0))
{
    Pad pad;
    pad.shape = shape;
    pad.size = size;
    pad.pos = pos;
    return pad;
}

} // namespace

class TestPadGeometry : public QObject {
    Q_OBJECT

private slots:
    void circleOutline();
    void rectangleOutline();
    void roundRectAndOblongOutline();
    void boardTransformTopAndBottom();
    void pointDistanceCircle();
    void segmentDistanceCircle();
    void rectangleDistance();
    void copperLayers();
    void padToPadCircle();
    void padToPolygonCircle();
    void polygonToPolygonDistances();
};

void TestPadGeometry::circleOutline()
{
    const Pad pad = makePad(PadShape::Circle, QSizeF(60, 60));
    const QPolygonF outline = padOutline(pad);
    QCOMPARE(outline.size(), qsizetype(32));
    for (qsizetype i = 0; i < outline.size(); ++i) {
        const QPointF& a = outline[i];
        const QPointF& b = outline[(i + 1) % outline.size()];
        QVERIFY(std::hypot(a.x() - b.x(), a.y() - b.y()) > 1e-6);
    }
    for (const QPointF& point : outline) {
        QVERIFY(near(std::hypot(point.x(), point.y()), 30.0, 1e-6));
    }
    QVERIFY(outline.boundingRect().contains(QRectF(-30, -30, 60, 60)));

    QCOMPARE(padOutline(pad, 8).size(), qsizetype(8));
    QCOMPARE(padOutline(pad, 64).size(), qsizetype(64));
}

void TestPadGeometry::rectangleOutline()
{
    const Pad pad = makePad(PadShape::Rectangle, QSizeF(100, 50));
    const QPolygonF outline = padOutline(pad);
    QCOMPARE(outline.size(), qsizetype(4));
    QVERIFY(outline.boundingRect().contains(QRectF(-50, -25, 100, 50)));
}

void TestPadGeometry::roundRectAndOblongOutline()
{
    Pad roundRect = makePad(PadShape::RoundRect, QSizeF(100, 50));
    roundRect.cornerRadius = 10;
    QVERIFY(padOutline(roundRect).size() > 4);

    const Pad oblong = makePad(PadShape::Oblong, QSizeF(100, 50));
    QVERIFY(padOutline(oblong).size() > 4);
}

void TestPadGeometry::boardTransformTopAndBottom()
{
    const Pad pad = makePad(PadShape::Rectangle, QSizeF(60, 40));

    const QPolygonF top = padOutlineOnBoard(
        pad, QPointF(1000, 500), 90.0, BoardSide::Top);
    QVERIFY(containsPoint(top, QPointF(1000, 530)));

    const QPolygonF bottom = padOutlineOnBoard(
        pad, QPointF(1000, 500), 0.0, BoardSide::Bottom);
    QVERIFY(containsPoint(bottom, QPointF(970, 500)));
}

void TestPadGeometry::pointDistanceCircle()
{
    const Pad pad = makePad(PadShape::Circle, QSizeF(60, 60));
    QVERIFY(near(distancePointToPad(QPointF(0, 40), pad, QPointF(0, 0), 0, BoardSide::Top),
                 10.0));
    QVERIFY(near(distancePointToPad(QPointF(0, 10), pad, QPointF(0, 0), 0, BoardSide::Top),
                 0.0));
}

void TestPadGeometry::segmentDistanceCircle()
{
    const Pad pad = makePad(PadShape::Circle, QSizeF(60, 60));
    QVERIFY(near(distanceSegmentToPad(QPointF(100, -10), QPointF(100, 10),
                                      pad, QPointF(0, 0), 0, BoardSide::Top),
                 70.0));
    QVERIFY(near(distanceSegmentToPad(QPointF(-100, 0), QPointF(100, 0),
                                      pad, QPointF(0, 0), 0, BoardSide::Top),
                 0.0));
}

void TestPadGeometry::rectangleDistance()
{
    Pad pad = makePad(PadShape::Rectangle, QSizeF(100, 50));
    QVERIFY(near(distancePointToPad(QPointF(60, 0), pad, QPointF(0, 0), 0, BoardSide::Top),
                 10.0));
    QVERIFY(near(distancePointToPad(QPointF(0, 0), pad, QPointF(0, 0), 0, BoardSide::Top),
                 0.0));
}

void TestPadGeometry::copperLayers()
{
    Pad throughHole = makePad(PadShape::Circle, QSizeF(60, 60));
    throughHole.drill = 30;
    throughHole.layer = kTopCopper;
    const QVector<LayerId> top = padCopperLayers(throughHole, BoardSide::Top);
    QVERIFY(top.contains(kTopCopper));
    QVERIFY(top.contains(kBottomCopper));

    Pad smd = makePad(PadShape::Circle, QSizeF(60, 60));
    smd.drill = 0;
    smd.layer = kTopCopper;
    const QVector<LayerId> smdLayers = padCopperLayers(smd, BoardSide::Bottom);
    QVERIFY(smdLayers.contains(kBottomCopper));
    QVERIFY(!smdLayers.contains(kTopCopper));
}

void TestPadGeometry::padToPadCircle()
{
    const Pad a = makePad(PadShape::Circle, QSizeF(60, 60), QPointF(0, 0));
    const Pad b = makePad(PadShape::Circle, QSizeF(60, 60), QPointF(100, 0));
    QVERIFY(near(distancePadToPad(a, QPointF(0, 0), 0, BoardSide::Top,
                                 b, QPointF(0, 0), 0, BoardSide::Top),
                 40.0));
}

void TestPadGeometry::padToPolygonCircle()
{
    const Pad pad = makePad(PadShape::Circle, QSizeF(60, 60), QPointF(0, 0));
    QPolygonF rect;
    rect << QPointF(100, -50) << QPointF(200, -50)
         << QPointF(200, 50) << QPointF(100, 50);
    QVERIFY(near(distancePadToPolygon(pad, QPointF(0, 0), 0, BoardSide::Top, rect),
                 70.0));
}

void TestPadGeometry::polygonToPolygonDistances()
{
    QPolygonF a;
    a << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10) << QPointF(0, 10);
    QPolygonF b;
    b << QPointF(20, 0) << QPointF(30, 0) << QPointF(30, 10) << QPointF(20, 10);
    QVERIFY(near(distancePolygonToPolygon(a, b), 10.0));

    QPolygonF inner;
    inner << QPointF(2, 2) << QPointF(8, 2) << QPointF(8, 8) << QPointF(2, 8);
    QVERIFY(near(distancePolygonToPolygon(a, inner), 0.0));
}

QTEST_APPLESS_MAIN(TestPadGeometry)

#include "tst_pad_geometry.moc"