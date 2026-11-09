#include <QtTest>

#include "core/board.h"
#include "core/footprint.h"
#include "feat_pcb/connectivity.h"

#include <QColor>

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

void requireOk(const Status& status)
{
    if (!status.ok) {
        qFatal("fixture failed: %s", qPrintable(status.message));
    }
}

Footprint makeFootprint(const QString& id, double drill)
{
    Footprint footprint;
    footprint.id = id;
    footprint.name = id;
    footprint.units = QStringLiteral("mil");

    for (int i = 1; i <= 8; ++i) {
        Pad pad;
        pad.number = i;
        pad.shape = PadShape::Circle;
        pad.pos = QPointF((i - 1) * 100, 0);
        pad.size = QSizeF(60, 60);
        pad.drill = drill;
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
            return {Status::success(), makeFootprint(id, 30.0)};
        }
        if (id == QStringLiteral("smd8")) {
            return {Status::success(), makeFootprint(id, 0.0)};
        }
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
}

void addLayer(Board& board, LayerId id, LayerGroupId groupId, const QString& name,
              BoardLayerType type, const QString& color, int stackOrder)
{
    BoardLayer layer;
    layer.id = id;
    layer.groupId = groupId;
    layer.name = name;
    layer.type = type;
    layer.color = QColor(color);
    layer.stackOrder = stackOrder;
    board.layers.append(layer);
}

Board makeBoard(const QString& footprintId = QStringLiteral("test8"))
{
    Board board;
    board.outline = polygon({{0, 0}, {2000, 0}, {2000, 1000}, {0, 1000}});
    board.groups.append(LayerGroup{1, QStringLiteral("Top"), 0});
    board.groups.append(LayerGroup{2, QStringLiteral("Bottom"), 1});
    addLayer(board, kTopCopper, 1, QStringLiteral("Top Copper"),
             BoardLayerType::Copper, QStringLiteral("#C83232"), 0);
    addLayer(board, kBottomCopper, 2, QStringLiteral("Bottom Copper"),
             BoardLayerType::Copper, QStringLiteral("#3264C8"), 0);

    Instance u1;
    u1.refdes = QStringLiteral("U1");
    u1.footprintId = footprintId;
    u1.pos = QPointF(500, 500);
    board.instances.append(u1);

    Instance u2;
    u2.refdes = QStringLiteral("U2");
    u2.footprintId = footprintId;
    u2.pos = QPointF(1000, 500);
    board.instances.append(u2);

    Net n001;
    n001.name = QStringLiteral("N001");
    n001.pins = {PinRef{QStringLiteral("U1"), 1}, PinRef{QStringLiteral("U2"), 1}};
    board.nets.append(n001);

    Net gnd;
    gnd.name = QStringLiteral("GND");
    gnd.pins = {PinRef{QStringLiteral("U1"), 8}, PinRef{QStringLiteral("U2"), 8}};
    board.nets.append(gnd);

    return board;
}

void addTrack(Board& board, const QVector<QPointF>& points, LayerId layer,
              const QString& net)
{
    Track track;
    track.id = board.allocateTrackId();
    track.layerId = layer;
    track.width = 12.0;
    track.net = net;
    track.points = points;
    requireOk(board.insertTrack(track));
}

void addVia(Board& board, QPointF pos, LayerId from, LayerId to, const QString& net)
{
    Via via;
    via.id = board.allocateViaId();
    via.pos = pos;
    via.drill = 24.0;
    via.diameter = 40.0;
    via.fromLayer = from;
    via.toLayer = to;
    via.net = net;
    requireOk(board.insertVia(via));
}

void addPour(Board& board, const QPolygonF& points, LayerId layer, const QString& net)
{
    CopperPour pour;
    pour.id = board.allocatePourId();
    pour.layerId = layer;
    pour.net = net;
    pour.clearance = 10.0;
    pour.points = points;
    requireOk(board.insertCopperPour(pour));
}

const NetConnectivity* netStatus(const ConnectivityResult& result, const QString& name)
{
    for (const NetConnectivity& net : result.nets) {
        if (net.name == name) {
            return &net;
        }
    }
    return nullptr;
}

bool hasShort(const ConnectivityResult& result, const QString& name)
{
    return result.shortedNets.contains(name);
}

} // namespace

class TestConnectivity : public QObject {
    Q_OBJECT

private slots:
    void pinToPinViaTrack();
    void gapDisconnects();
    void viaBridgesTopAndBottom();
    void throughHolePadConnectsBottomTrack();
    void smdPadDoesNotConnectBottomTrack();
    void differentNetTracksAreShorted();
    void pourConnectsPins();
    void unassignedTrackStillConnects();
    void parallelTrackBodiesOverlap();
    void trackTouchesPadEdge();
    void viaEdgeTouchesTrack();
    void overlappingPadsConnect();
    void singlePinNetIsConnected();
    void netWithMissingPinIsNotConnected();
    void missingFootprintReportsError();
};

void TestConnectivity::pinToPinViaTrack()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(500, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(n001->connected);
}

void TestConnectivity::gapDisconnects()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(500, 500), QPointF(800, 500)}, kTopCopper,
             QStringLiteral("N001"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(!n001->connected);
    QVERIFY(n001->componentCount >= 2);
}

void TestConnectivity::viaBridgesTopAndBottom()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(500, 500), QPointF(700, 500)}, kTopCopper,
             QStringLiteral("N001"));
    addTrack(board, {QPointF(700, 500), QPointF(1000, 500)}, kBottomCopper,
             QStringLiteral("N001"));
    addVia(board, QPointF(700, 500), kTopCopper, kBottomCopper,
           QStringLiteral("N001"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(n001->connected);
}

void TestConnectivity::throughHolePadConnectsBottomTrack()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(500, 500), QPointF(1000, 500)}, kBottomCopper,
             QStringLiteral("N001"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(n001->connected);
}

void TestConnectivity::smdPadDoesNotConnectBottomTrack()
{
    Board board = makeBoard(QStringLiteral("smd8"));
    addTrack(board, {QPointF(500, 500), QPointF(1000, 500)}, kBottomCopper,
             QStringLiteral("N001"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(!n001->connected);
}

void TestConnectivity::differentNetTracksAreShorted()
{
    Board board = makeBoard();

    Net n002;
    n002.name = QStringLiteral("N002");
    n002.pins = {PinRef{QStringLiteral("U1"), 2}, PinRef{QStringLiteral("U2"), 2}};
    board.nets.append(n002);

    addTrack(board, {QPointF(500, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    addTrack(board, {QPointF(600, 500), QPointF(1100, 500)}, kTopCopper,
             QStringLiteral("N002"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    QVERIFY(hasShort(result, QStringLiteral("N001")));
    QVERIFY(hasShort(result, QStringLiteral("N002")));
    QVERIFY(netStatus(result, QStringLiteral("N001"))->connected);
    QVERIFY(netStatus(result, QStringLiteral("N002"))->connected);
}

void TestConnectivity::pourConnectsPins()
{
    Board board = makeBoard();
    addPour(board, polygon({{1100, 400}, {1800, 400}, {1800, 600}, {1100, 600}}),
            kBottomCopper, QStringLiteral("GND"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* gnd = netStatus(result, QStringLiteral("GND"));
    QVERIFY(gnd);
    QVERIFY(gnd->connected);
}

void TestConnectivity::unassignedTrackStillConnects()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(500, 500), QPointF(1000, 500)}, kTopCopper,
             QString());

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(n001->connected);
    QVERIFY(result.shortedNets.isEmpty());
}

void TestConnectivity::parallelTrackBodiesOverlap()
{
    Board board = makeBoard();

    Net n002;
    n002.name = QStringLiteral("N002");
    n002.pins = {PinRef{QStringLiteral("U1"), 2}, PinRef{QStringLiteral("U2"), 2}};
    board.nets.append(n002);

    addTrack(board, {QPointF(500, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    addTrack(board, {QPointF(600, 505), QPointF(1100, 505)}, kTopCopper,
             QStringLiteral("N002"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    QVERIFY(hasShort(result, QStringLiteral("N001")));
    QVERIFY(hasShort(result, QStringLiteral("N002")));
}

void TestConnectivity::trackTouchesPadEdge()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(500, 535), QPointF(1000, 535)}, kTopCopper,
             QStringLiteral("N001"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(n001->connected);
}

void TestConnectivity::viaEdgeTouchesTrack()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(500, 500), QPointF(700, 500)}, kTopCopper,
             QStringLiteral("N001"));
    addTrack(board, {QPointF(700, 500), QPointF(1000, 500)}, kBottomCopper,
             QStringLiteral("N001"));
    addVia(board, QPointF(700, 525), kTopCopper, kBottomCopper,
           QStringLiteral("N001"));

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(n001->connected);
}

void TestConnectivity::overlappingPadsConnect()
{
    Board board = makeBoard();
    board.instances[1].pos = QPointF(530, 500);

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(n001->connected);
}

void TestConnectivity::singlePinNetIsConnected()
{
    Board board = makeBoard();
    Net single;
    single.name = QStringLiteral("SINGLE");
    single.pins = {PinRef{QStringLiteral("U1"), 3}};
    board.nets.append(single);

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* status = netStatus(result, QStringLiteral("SINGLE"));
    QVERIFY(status);
    QVERIFY(status->connected);
}

void TestConnectivity::netWithMissingPinIsNotConnected()
{
    Board board = makeBoard();
    Net missing;
    missing.name = QStringLiteral("BROKEN");
    missing.pins = {PinRef{QStringLiteral("U1"), 99}};
    board.nets.append(missing);

    const ConnectivityResult result = analyzeConnectivity(board, resolver());
    const NetConnectivity* status = netStatus(result, QStringLiteral("BROKEN"));
    QVERIFY(status);
    QVERIFY(!status->connected);
}

void TestConnectivity::missingFootprintReportsError()
{
    const FootprintResolver missing = [](const QString&) -> Result<Footprint> {
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
    const ConnectivityResult result = analyzeConnectivity(makeBoard(), missing);
    QVERIFY(!result.errors.isEmpty());
    const NetConnectivity* n001 = netStatus(result, QStringLiteral("N001"));
    QVERIFY(n001);
    QVERIFY(!n001->connected);
}

QTEST_APPLESS_MAIN(TestConnectivity)

#include "tst_connectivity.moc"