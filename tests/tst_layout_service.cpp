#include <QtTest>

#include "core/board.h"
#include "core/footprint.h"
#include "core/geometry.h"
#include "feat_pcb/layout_service.h"
#include "feat_pcb/pad_geometry.h"

#include <array>
#include <cmath>

using namespace eda;

namespace {

void requireOk(const Status& status)
{
    if (!status.ok) {
        qFatal("fixture failed: %s", qPrintable(status.message));
    }
}

Footprint makeTestFootprint()
{
    Footprint footprint;
    footprint.id = QStringLiteral("test8");
    footprint.name = QStringLiteral("Test 8-pin");
    footprint.units = QStringLiteral("mil");

    for (int i = 1; i <= 8; ++i) {
        Pad pad;
        pad.number = i;
        pad.shape = PadShape::Circle;
        pad.pos = QPointF((i - 1) * 100, 0);
        pad.size = QSizeF(60, 60);
        pad.drill = 30;
        pad.layer = kTopCopper;
        const Result<PadId> added = footprint.addPad(pad);
        requireOk(added.status);
        footprint.pins.append(Pin{i, QString::number(i),
                                  ElectricalType::Passive, *added.value});
    }
    return footprint;
}

FootprintResolver resolver()
{
    return [](const QString& id) -> Result<Footprint> {
        if (id == QStringLiteral("test8")) {
            return {Status::success(), makeTestFootprint()};
        }
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
}

Board makeBoard(int count = 2)
{
    Board board;
    board.outline << QPointF(0, 0) << QPointF(2000, 0)
                  << QPointF(2000, 1000) << QPointF(0, 1000);
    for (int i = 0; i < count; ++i) {
        Instance instance;
        instance.refdes = QStringLiteral("U%1").arg(i + 1);
        instance.footprintId = QStringLiteral("test8");
        instance.pos = QPointF(0, 0);
        board.instances.append(instance);
    }
    return board;
}

bool inside(const QRectF& rect, const QPolygonF& outline)
{
    const std::array<QPointF, 4> corners{
        rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
    for (const QPointF& corner : corners) {
        if (!containsPoint(outline, corner)) {
            return false;
        }
    }
    return true;
}

} // namespace

class TestLayoutService : public QObject {
    Q_OBJECT

private slots:
    void twoInstancesInsideBoard();
    void columnsOneStacks();
    void missingFootprintFails();
    void smallBoardFails();
    void bottomRotatedInstanceUsesRotatedBounds();
    void concaveOutlineFails();
    void emptyBoardSucceeds();
};

void TestLayoutService::twoInstancesInsideBoard()
{
    const Board board = makeBoard(2);
    const InstanceLayoutResult result = layoutInstances(board, resolver(), {});
    QVERIFY2(result.status.ok, qPrintable(result.status.message));
    QCOMPARE(result.positions.size(), 2);
    QVERIFY(result.positions[0] != result.positions[1]);
    QCOMPARE(result.bounds.size(), 2);
    QVERIFY(std::abs(result.bounds[0].left() - 100.0) < 1e-6);
    QVERIFY(std::abs(result.bounds[0].top() - 100.0) < 1e-6);

    const Footprint footprint = makeTestFootprint();
    QRectF first;
    QRectF second;
    for (int i = 0; i < 2; ++i) {
        Instance placed = board.instances[i];
        placed.pos = result.positions[i];
        const QRectF bounds = instanceBounds(placed, footprint);
        QVERIFY(inside(bounds, board.outline));
        if (i == 0) {
            first = bounds;
        } else {
            second = bounds;
        }
    }
    QVERIFY(!first.intersects(second));
}

void TestLayoutService::columnsOneStacks()
{
    const Board board = makeBoard(2);
    InstanceLayoutOptions options;
    options.columns = 1;
    const InstanceLayoutResult result = layoutInstances(board, resolver(), options);
    QVERIFY2(result.status.ok, qPrintable(result.status.message));
    QCOMPARE(result.positions.size(), 2);
    QVERIFY(std::abs(result.positions[0].x() - result.positions[1].x()) < 1e-6);
    QVERIFY(std::abs(result.positions[0].y() - result.positions[1].y()) >= 120.0);
}

void TestLayoutService::missingFootprintFails()
{
    const FootprintResolver missing = [](const QString&) -> Result<Footprint> {
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
    const InstanceLayoutResult result = layoutInstances(makeBoard(1), missing);
    QVERIFY(!result.status.ok);
}

void TestLayoutService::smallBoardFails()
{
    Board board = makeBoard(2);
    board.outline.clear();
    board.outline << QPointF(0, 0) << QPointF(500, 0)
                  << QPointF(500, 500) << QPointF(0, 500);
    const InstanceLayoutResult result = layoutInstances(board, resolver(), {});
    QVERIFY(!result.status.ok);
    QVERIFY(result.status.message.contains(QStringLiteral("板框")));
}

void TestLayoutService::bottomRotatedInstanceUsesRotatedBounds()
{
    Board board = makeBoard(1);
    board.instances[0].side = BoardSide::Bottom;
    board.instances[0].rotationDeg = 90.0;

    const InstanceLayoutResult result = layoutInstances(board, resolver(), {});
    QVERIFY2(result.status.ok, qPrintable(result.status.message));
    QCOMPARE(result.positions.size(), 1);

    Instance placed = board.instances[0];
    placed.pos = result.positions[0];
    QVERIFY(inside(instanceBounds(placed, makeTestFootprint()), board.outline));
}

void TestLayoutService::concaveOutlineFails()
{
    Board board = makeBoard(2);
    board.outline.clear();
    board.outline << QPointF(0, 0) << QPointF(800, 0)
                  << QPointF(800, 400) << QPointF(400, 400)
                  << QPointF(400, 1000) << QPointF(0, 1000);
    const InstanceLayoutResult result = layoutInstances(board, resolver(), {});
    QVERIFY(!result.status.ok);
    QVERIFY(result.status.message.contains(QStringLiteral("板框")));
}

void TestLayoutService::emptyBoardSucceeds()
{
    const Board board = makeBoard(0);
    const InstanceLayoutResult result = layoutInstances(board, resolver(), {});
    QVERIFY(result.status.ok);
    QVERIFY(result.positions.isEmpty());
}

QTEST_APPLESS_MAIN(TestLayoutService)

#include "tst_layout_service.moc"
