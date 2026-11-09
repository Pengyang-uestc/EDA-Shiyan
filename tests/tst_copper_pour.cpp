#include <QtTest>

#include "core/board.h"
#include "feat_pcb/copper_pour.h"

#include <QColor>

using namespace eda;

namespace {

QPolygonF rect(double left, double bottom, double right, double top)
{
    QPolygonF polygon;
    polygon << QPointF(left, bottom) << QPointF(right, bottom)
            << QPointF(right, top) << QPointF(left, top);
    return polygon;
}

Board makeBoard()
{
    Board board;
    board.outline = rect(0, 0, 2000, 1000);

    BoardLayer top;
    top.id = kTopCopper;
    top.groupId = 1;
    top.name = QStringLiteral("Top Copper");
    top.type = BoardLayerType::Copper;
    top.color = QColor(QStringLiteral("#C83232"));
    board.layers.append(top);

    BoardLayer bottom;
    bottom.id = kBottomCopper;
    bottom.groupId = 2;
    bottom.name = QStringLiteral("Bottom Copper");
    bottom.type = BoardLayerType::Copper;
    bottom.color = QColor(QStringLiteral("#3264C8"));
    board.layers.append(bottom);

    BoardLayer silk;
    silk.id = kTopSilk;
    silk.groupId = 1;
    silk.name = QStringLiteral("Top Silk");
    silk.type = BoardLayerType::Silk;
    silk.color = QColor(QStringLiteral("#F0F0F0"));
    board.layers.append(silk);

    Net net;
    net.name = QStringLiteral("N001");
    board.nets.append(net);
    return board;
}

PourCreateOptions options()
{
    PourCreateOptions options;
    options.polygon = rect(100, 100, 500, 500);
    options.layerId = kTopCopper;
    options.net = QStringLiteral("N001");
    options.clearance = 10.0;
    options.priority = 1;
    return options;
}

bool hasIssue(const QVector<Issue>& issues, const QString& code)
{
    for (const Issue& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestCopperPour : public QObject {
    Q_OBJECT

private slots:
    void addValidPour();
    void addInvalidPolygonFails();
    void addNonCopperLayerFails();
    void addTooSmallClearanceFails();
    void addUnknownNetFails();
    void addNegativePriorityFails();
    void addOutsideBoardFails();
    void addReservedNetFails();
    void removeAndMissing();
    void updateFields();
    void updatesAreAtomic();
    void validateFindsInvalidPour();
    void validateFindsInvalidIds();
    void rebuildChecks();
    void rebuildFindsInvalidPour();
};

void TestCopperPour::addValidPour()
{
    Board board = makeBoard();
    const Result<PourId> added = CopperPourService::add(board, options());
    QVERIFY2(added.status.ok, qPrintable(added.status.message));
    QCOMPARE(board.pours.size(), 1);
    QCOMPARE(board.pours[0].id, *added.value);
    QCOMPARE(board.pours[0].layerId, kTopCopper);
    QCOMPARE(board.pours[0].net, QStringLiteral("N001"));
}

void TestCopperPour::addInvalidPolygonFails()
{
    Board board = makeBoard();
    const PourId previousNextId = board.nextPourId;
    PourCreateOptions invalid = options();
    invalid.polygon.clear();
    invalid.polygon << QPointF(0, 0) << QPointF(100, 100)
                    << QPointF(0, 100) << QPointF(100, 0);
    QVERIFY(!CopperPourService::add(board, invalid).status.ok);
    QVERIFY(board.pours.isEmpty());
    QCOMPARE(board.nextPourId, previousNextId);
}

void TestCopperPour::addNonCopperLayerFails()
{
    Board board = makeBoard();
    PourCreateOptions invalid = options();
    invalid.layerId = kTopSilk;
    QVERIFY(!CopperPourService::add(board, invalid).status.ok);
}

void TestCopperPour::addTooSmallClearanceFails()
{
    Board board = makeBoard();
    PourCreateOptions invalid = options();
    invalid.clearance = 1.0;
    QVERIFY(!CopperPourService::add(board, invalid).status.ok);
}

void TestCopperPour::addUnknownNetFails()
{
    Board board = makeBoard();
    PourCreateOptions invalid = options();
    invalid.net = QStringLiteral("NO_SUCH_NET");
    QVERIFY(!CopperPourService::add(board, invalid).status.ok);
}

void TestCopperPour::addNegativePriorityFails()
{
    Board board = makeBoard();
    PourCreateOptions invalid = options();
    invalid.priority = -1;
    QVERIFY(!CopperPourService::add(board, invalid).status.ok);
}

void TestCopperPour::addOutsideBoardFails()
{
    Board board = makeBoard();
    PourCreateOptions invalid = options();
    invalid.polygon = rect(-100, -100, 10, 10);
    QVERIFY(!CopperPourService::add(board, invalid).status.ok);
}

void TestCopperPour::addReservedNetFails()
{
    Board board = makeBoard();
    PourCreateOptions invalid = options();
    invalid.net = QStringLiteral("-");
    QVERIFY(!CopperPourService::add(board, invalid).status.ok);
}

void TestCopperPour::removeAndMissing()
{
    Board board = makeBoard();
    const Result<PourId> added = CopperPourService::add(board, options());
    QVERIFY(added.status.ok);
    QVERIFY(CopperPourService::remove(board, *added.value).ok);
    QVERIFY(board.pours.isEmpty());
    QVERIFY(!CopperPourService::remove(board, *added.value).ok);
}

void TestCopperPour::updateFields()
{
    Board board = makeBoard();
    const Result<PourId> added = CopperPourService::add(board, options());
    QVERIFY(added.status.ok);
    const PourId id = *added.value;

    QVERIFY(CopperPourService::setPolygon(board, id, rect(200, 200, 600, 600)).ok);
    QVERIFY(CopperPourService::setLayer(board, id, kBottomCopper).ok);
    QVERIFY(CopperPourService::setNet(board, id, QString()).ok);
    QVERIFY(CopperPourService::setClearance(board, id, 12.0).ok);
    QVERIFY(CopperPourService::setPriority(board, id, 3).ok);

    QCOMPARE(board.pours[0].points.size(), qsizetype(4));
    QCOMPARE(board.pours[0].layerId, kBottomCopper);
    QVERIFY(board.pours[0].net.isEmpty());
    QCOMPARE(board.pours[0].clearance, 12.0);
    QCOMPARE(board.pours[0].priority, 3);

    QVERIFY(!CopperPourService::setLayer(board, id, kTopSilk).ok);
    QVERIFY(!CopperPourService::setClearance(board, id, 1.0).ok);
}

void TestCopperPour::updatesAreAtomic()
{
    Board board = makeBoard();
    const Result<PourId> added = CopperPourService::add(board, options());
    QVERIFY(added.status.ok);
    const PourId id = *added.value;
    const QPolygonF originalPolygon = board.pours[0].points;
    const double originalClearance = board.pours[0].clearance;

    QVERIFY(!CopperPourService::setPolygon(
        board, id, QPolygonF{QPointF(0, 0), QPointF(100, 100),
                             QPointF(0, 100), QPointF(100, 0)}).ok);
    QCOMPARE(board.pours[0].points, originalPolygon);

    QVERIFY(!CopperPourService::setLayer(board, id, kTopSilk).ok);
    QCOMPARE(board.pours[0].layerId, kTopCopper);

    QVERIFY(!CopperPourService::setNet(board, id, QStringLiteral("NO_SUCH_NET")).ok);
    QCOMPARE(board.pours[0].net, QStringLiteral("N001"));

    QVERIFY(!CopperPourService::setClearance(board, id, 1.0).ok);
    QCOMPARE(board.pours[0].clearance, originalClearance);

    QVERIFY(!CopperPourService::setPriority(board, id, -1).ok);
    QCOMPARE(board.pours[0].priority, 1);
}

void TestCopperPour::validateFindsInvalidPour()
{
    Board board = makeBoard();
    CopperPour invalid;
    invalid.id = 99;
    invalid.layerId = kTopSilk;
    invalid.net = QStringLiteral("NO_SUCH_NET");
    invalid.clearance = 1.0;
    invalid.priority = -1;
    invalid.points = rect(0, 0, 10, 10);
    board.pours.append(invalid);

    const QVector<Issue> issues = CopperPourService::validate(board);
    QVERIFY(hasIssue(issues, issue_code::kPourLayer));
    QVERIFY(hasIssue(issues, issue_code::kPourNet));
    QVERIFY(hasIssue(issues, issue_code::kPourClearance));
    QVERIFY(hasIssue(issues, issue_code::kPourPriority));
}

void TestCopperPour::validateFindsInvalidIds()
{
    Board board = makeBoard();
    CopperPour zero;
    zero.id = kInvalidId;
    zero.layerId = kTopCopper;
    zero.points = rect(0, 0, 10, 10);
    board.pours.append(zero);

    CopperPour duplicate = zero;
    duplicate.id = 7;
    board.pours.append(duplicate);
    board.pours.append(duplicate);

    const QVector<Issue> issues = CopperPourService::validate(board);
    QVERIFY(hasIssue(issues, issue_code::kPourId));
}

void TestCopperPour::rebuildChecks()
{
    Board board = makeBoard();
    const Result<PourId> added = CopperPourService::add(board, options());
    QVERIFY(added.status.ok);
    QVERIFY(CopperPourService::rebuild(board, *added.value).ok);
    QVERIFY(!CopperPourService::rebuild(board, 9999).ok);
}

void TestCopperPour::rebuildFindsInvalidPour()
{
    Board board = makeBoard();
    const Result<PourId> added = CopperPourService::add(board, options());
    QVERIFY(added.status.ok);
    board.pours[0].points.clear();
    QVERIFY(!CopperPourService::rebuild(board, *added.value).ok);
}

QTEST_APPLESS_MAIN(TestCopperPour)

#include "tst_copper_pour.moc"