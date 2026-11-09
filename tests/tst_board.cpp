#include <QtTest>

#include "core/board.h"
#include "core/footprint.h"
#include "core/geometry.h"
#include "core/io.h"

#include <QColor>
#include <QTemporaryDir>

#include <algorithm>
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

void requireOk(const Status& status)
{
    if (!status.ok) {
        qFatal("fixture failed: %s", qPrintable(status.message));
    }
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

// 独立的最小 8 脚封装：不依赖 C 的模板和制造校验，避免 board 测试互相掩盖。
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
        const Result<PadId> added = footprint.addPad(pad);
        requireOk(added.status);
        footprint.pins.append(Pin{i, QString::number(i),
                                  ElectricalType::Passive, *added.value});
    }
    return footprint;
}

FootprintResolver resolver()
{
    return [](const QString& footprintId) -> Result<Footprint> {
        if (footprintId == QStringLiteral("test8")) {
            return {Status::success(), makeTestFootprint()};
        }
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
}

Board makeBoard()
{
    Board board;
    board.outline = polygon({{0, 0}, {2000, 0}, {2000, 1000}, {0, 1000}});

    board.groups.append(LayerGroup{1, QStringLiteral("Top"), 0});
    board.groups.append(LayerGroup{2, QStringLiteral("Bottom"), 1});

    addLayer(board, kTopCopper, 1, QStringLiteral("Top Copper"),
             BoardLayerType::Copper, QStringLiteral("#C83232"), 0);
    addLayer(board, kBottomCopper, 2, QStringLiteral("Bottom Copper"),
             BoardLayerType::Copper, QStringLiteral("#3264C8"), 0);
    addLayer(board, kTopSilk, 1, QStringLiteral("Top Silk"),
             BoardLayerType::Silk, QStringLiteral("#F0F0F0"), 1);
    addLayer(board, kBoardOutline, 1, QStringLiteral("Board Outline"),
             BoardLayerType::Outline, QStringLiteral("#FFFF00"), 2);

    Instance u1;
    u1.refdes = QStringLiteral("U1");
    u1.footprintId = QStringLiteral("test8");
    u1.pos = QPointF(500, 500);
    board.instances.append(u1);

    Instance u2;
    u2.refdes = QStringLiteral("U2");
    u2.footprintId = QStringLiteral("test8");
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

    Track track;
    track.id = board.allocateTrackId();
    track.layerId = kTopCopper;
    track.width = 12.0;
    track.net = QStringLiteral("N001");
    track.points = {QPointF(500, 500), QPointF(700, 500)};
    requireOk(board.insertTrack(track));

    Via via;
    via.id = board.allocateViaId();
    via.pos = QPointF(700, 500);
    via.drill = 24.0;
    via.diameter = 40.0;
    via.fromLayer = kTopCopper;
    via.toLayer = kBottomCopper;
    via.net = QStringLiteral("N001");
    requireOk(board.insertVia(via));

    CopperPour pour;
    pour.id = board.allocatePourId();
    pour.layerId = kBottomCopper;
    pour.net = QStringLiteral("GND");
    pour.clearance = 10.0;
    pour.points = polygon({{0, 0}, {1000, 0}, {1000, 500}, {0, 500}});
    requireOk(board.insertCopperPour(pour));

    return board;
}

QByteArray encodeFixture(const Board& board)
{
    const Result<QByteArray> encoded = encodeBoard(board, resolver());
    requireOk(encoded.status);
    return *encoded.value;
}

bool near(double a, double b, double epsilon = 1e-6)
{
    return std::abs(a - b) <= epsilon;
}

void requireSamePoint(const QPointF& a, const QPointF& b)
{
    QVERIFY2(near(a.x(), b.x()) && near(a.y(), b.y()),
             qPrintable(QStringLiteral("point mismatch: (%1,%2) vs (%3,%4)")
                            .arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y())));
}

void requireSameBoard(const Board& a, const Board& b)
{
    QCOMPARE(a.outline.size(), b.outline.size());
    for (int i = 0; i < a.outline.size(); ++i) {
        requireSamePoint(a.outline[i], b.outline[i]);
    }

    QCOMPARE(a.groups.size(), b.groups.size());
    for (int i = 0; i < a.groups.size(); ++i) {
        QCOMPARE(a.groups[i].id, b.groups[i].id);
        QCOMPARE(a.groups[i].name, b.groups[i].name);
        QCOMPARE(a.groups[i].stackOrder, b.groups[i].stackOrder);
    }

    QCOMPARE(a.layers.size(), b.layers.size());
    for (int i = 0; i < a.layers.size(); ++i) {
        QCOMPARE(a.layers[i].id, b.layers[i].id);
        QCOMPARE(a.layers[i].groupId, b.layers[i].groupId);
        QCOMPARE(a.layers[i].name, b.layers[i].name);
        QVERIFY(a.layers[i].type == b.layers[i].type);
        QCOMPARE(a.layers[i].color, b.layers[i].color);
        QCOMPARE(a.layers[i].stackOrder, b.layers[i].stackOrder);
    }

    QCOMPARE(a.instances.size(), b.instances.size());
    for (int i = 0; i < a.instances.size(); ++i) {
        QCOMPARE(a.instances[i].refdes, b.instances[i].refdes);
        QCOMPARE(a.instances[i].footprintId, b.instances[i].footprintId);
        requireSamePoint(a.instances[i].pos, b.instances[i].pos);
        QVERIFY(near(a.instances[i].rotationDeg, b.instances[i].rotationDeg));
        QVERIFY(a.instances[i].side == b.instances[i].side);
    }

    QCOMPARE(a.tracks.size(), b.tracks.size());
    for (int i = 0; i < a.tracks.size(); ++i) {
        QCOMPARE(a.tracks[i].id, b.tracks[i].id);
        QCOMPARE(a.tracks[i].layerId, b.tracks[i].layerId);
        QVERIFY(near(a.tracks[i].width, b.tracks[i].width));
        QCOMPARE(a.tracks[i].net, b.tracks[i].net);
        QCOMPARE(a.tracks[i].points.size(), b.tracks[i].points.size());
        for (int j = 0; j < a.tracks[i].points.size(); ++j) {
            requireSamePoint(a.tracks[i].points[j], b.tracks[i].points[j]);
        }
    }

    QCOMPARE(a.vias.size(), b.vias.size());
    for (int i = 0; i < a.vias.size(); ++i) {
        QCOMPARE(a.vias[i].id, b.vias[i].id);
        requireSamePoint(a.vias[i].pos, b.vias[i].pos);
        QVERIFY(near(a.vias[i].drill, b.vias[i].drill));
        QVERIFY(near(a.vias[i].diameter, b.vias[i].diameter));
        QCOMPARE(a.vias[i].fromLayer, b.vias[i].fromLayer);
        QCOMPARE(a.vias[i].toLayer, b.vias[i].toLayer);
        QCOMPARE(a.vias[i].net, b.vias[i].net);
    }

    QCOMPARE(a.pours.size(), b.pours.size());
    for (int i = 0; i < a.pours.size(); ++i) {
        QCOMPARE(a.pours[i].id, b.pours[i].id);
        QCOMPARE(a.pours[i].layerId, b.pours[i].layerId);
        QCOMPARE(a.pours[i].net, b.pours[i].net);
        QVERIFY(near(a.pours[i].clearance, b.pours[i].clearance));
        QCOMPARE(a.pours[i].priority, b.pours[i].priority);
        QCOMPARE(a.pours[i].points.size(), b.pours[i].points.size());
        for (int j = 0; j < a.pours[i].points.size(); ++j) {
            requireSamePoint(a.pours[i].points[j], b.pours[i].points[j]);
        }
    }

    QCOMPARE(a.nets.size(), b.nets.size());
    for (int i = 0; i < a.nets.size(); ++i) {
        QCOMPARE(a.nets[i].name, b.nets[i].name);
        QCOMPARE(a.nets[i].pins.size(), b.nets[i].pins.size());
        for (int j = 0; j < a.nets[i].pins.size(); ++j) {
            QCOMPARE(a.nets[i].pins[j].refdes, b.nets[i].pins[j].refdes);
            QCOMPARE(a.nets[i].pins[j].pinNumber, b.nets[i].pins[j].pinNumber);
        }
    }

    QCOMPARE(a.nextTrackId, b.nextTrackId);
    QCOMPARE(a.nextViaId, b.nextViaId);
    QCOMPARE(a.nextPourId, b.nextPourId);
}

} // namespace

class TestBoard : public QObject {
    Q_OBJECT

private slots:
    void validBoardPassesStructure();
    void validBoardPassesReferences();
    void clockwiseOutlineRejected();
    void selfIntersectingOutlineRejected();
    void missingGroupRejected();
    void duplicateGroupRejected();
    void emptyGroupNameRejected();
    void duplicateLayerRejected();
    void emptyLayerNameRejected();
    void layerTypeMismatchRejected();
    void duplicateRefdesRejected();
    void emptyFootprintIdRejected();
    void duplicateTrackIdRejected();
    void trackOnSilkRejected();
    void trackWithSinglePointRejected();
    void trackWithZeroWidthRejected();
    void viaSameLayerRejected();
    void viaWithZeroDrillRejected();
    void pourSelfIntersectionRejected();
    void pourWithNegativeClearanceRejected();
    void duplicateNetPinRejected();
    void reservedNetNameRejected();
    void unknownTrackNetRejected();
    void allocateIdsAndPreserveInsertedId();
    void removeMissingTrackFails();
    void boardEncodingRoundTrip();
    void bottomSideInstanceRoundTrip();
    void coordinateRoundingInFile();
    void downwardOutlineNormalizedOnLoad();
    void negativeCoordinatesRoundTrip();
    void missingFootprintRejected();
    void resolverWrongFootprintIdRejected();
    void unknownFootprintPinRejected();
    void badHeaderRejected();
    void missingEndRejected();
    void trailingDataRejected();
    void unknownRecordRejected();
    void saveLoadRoundTrip();
    void invalidSaveDoesNotOverwriteExistingFile();
};

void TestBoard::validBoardPassesStructure()
{
    const Status status = validateBoardStructure(makeBoard());
    QVERIFY2(status.ok, qPrintable(status.message));
}

void TestBoard::validBoardPassesReferences()
{
    const Status status = validateBoard(makeBoard(), resolver());
    QVERIFY2(status.ok, qPrintable(status.message));
}

void TestBoard::clockwiseOutlineRejected()
{
    Board board = makeBoard();
    std::reverse(board.outline.begin(), board.outline.end());
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::selfIntersectingOutlineRejected()
{
    Board board = makeBoard();
    board.outline = polygon({{0, 0}, {2000, 1000}, {0, 1000}, {2000, 0}});
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::missingGroupRejected()
{
    Board board = makeBoard();
    board.layers[0].groupId = 99;
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::duplicateGroupRejected()
{
    Board board = makeBoard();
    board.groups.append(LayerGroup{1, QStringLiteral("Duplicate"), 2});
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::emptyGroupNameRejected()
{
    Board board = makeBoard();
    board.groups[0].name.clear();
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::duplicateLayerRejected()
{
    Board board = makeBoard();
    board.layers.append(board.layers[0]);
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::emptyLayerNameRejected()
{
    Board board = makeBoard();
    board.layers[0].name.clear();
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::layerTypeMismatchRejected()
{
    Board board = makeBoard();
    board.layers[0].type = BoardLayerType::Silk;
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::duplicateRefdesRejected()
{
    Board board = makeBoard();
    board.instances.append(board.instances[0]);
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::emptyFootprintIdRejected()
{
    Board board = makeBoard();
    board.instances[0].footprintId.clear();
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::duplicateTrackIdRejected()
{
    Board board = makeBoard();
    board.tracks.append(board.tracks[0]);
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::trackOnSilkRejected()
{
    Board board = makeBoard();
    board.tracks[0].layerId = kTopSilk;
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::trackWithSinglePointRejected()
{
    Board board = makeBoard();
    board.tracks[0].points = {QPointF(10, 10)};
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::trackWithZeroWidthRejected()
{
    Board board = makeBoard();
    board.tracks[0].width = 0.0;
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::viaSameLayerRejected()
{
    Board board = makeBoard();
    board.vias[0].toLayer = board.vias[0].fromLayer;
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::viaWithZeroDrillRejected()
{
    Board board = makeBoard();
    board.vias[0].drill = 0.0;
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::pourSelfIntersectionRejected()
{
    Board board = makeBoard();
    board.pours[0].points = polygon({{0, 0}, {100, 100}, {0, 100}, {100, 0}});
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::pourWithNegativeClearanceRejected()
{
    Board board = makeBoard();
    board.pours[0].clearance = -1.0;
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::duplicateNetPinRejected()
{
    Board board = makeBoard();
    Net extra;
    extra.name = QStringLiteral("N002");
    extra.pins = {PinRef{QStringLiteral("U1"), 1}};
    board.nets.append(extra);
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::reservedNetNameRejected()
{
    Board board = makeBoard();
    board.nets[0].name = QStringLiteral("-");
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::unknownTrackNetRejected()
{
    Board board = makeBoard();
    board.tracks[0].net = QStringLiteral("NO_SUCH_NET");
    QVERIFY(!validateBoardStructure(board).ok);
}

void TestBoard::allocateIdsAndPreserveInsertedId()
{
    Board board = makeBoard();
    const TrackId next = board.allocateTrackId();
    QVERIFY(next > 0);

    Track track;
    track.id = next;
    track.layerId = kTopCopper;
    track.points = {QPointF(0, 0), QPointF(10, 0)};
    QVERIFY(board.insertTrack(track).ok);

    Track explicitId;
    explicitId.id = 100;
    explicitId.layerId = kTopCopper;
    explicitId.points = {QPointF(0, 0), QPointF(10, 0)};
    QVERIFY(board.insertTrack(explicitId).ok);
    QVERIFY(board.nextTrackId > 100);

    Track duplicate = explicitId;
    QVERIFY(!board.insertTrack(duplicate).ok);
    QVERIFY(board.removeTrack(100).ok);
}

void TestBoard::removeMissingTrackFails()
{
    Board board = makeBoard();
    QVERIFY(!board.removeTrack(9999).ok);
}

void TestBoard::boardEncodingRoundTrip()
{
    const Board original = makeBoard();
    const Result<QByteArray> encoded = encodeBoard(original, resolver());
    QVERIFY2(encoded.status.ok, qPrintable(encoded.status.message));

    const Result<Board> decoded = decodeBoard(*encoded.value, resolver());
    QVERIFY2(decoded.status.ok, qPrintable(decoded.status.message));
    requireSameBoard(original, *decoded.value);
}

void TestBoard::bottomSideInstanceRoundTrip()
{
    Board board = makeBoard();
    board.instances[1].side = BoardSide::Bottom;
    board.instances[1].rotationDeg = 37.0;

    const QByteArray data = encodeFixture(board);
    const Result<Board> decoded = decodeBoard(data, resolver());
    QVERIFY2(decoded.status.ok, qPrintable(decoded.status.message));
    requireSameBoard(board, *decoded.value);
}

void TestBoard::coordinateRoundingInFile()
{
    Board board = makeBoard();
    board.tracks[0].points[1] = QPointF(123.4567, 500);

    const QByteArray data = encodeFixture(board);
    QVERIFY(QString::fromUtf8(data).contains(QStringLiteral("123.457")));

    const Result<Board> decoded = decodeBoard(data, resolver());
    QVERIFY2(decoded.status.ok, qPrintable(decoded.status.message));
    QVERIFY(near(decoded.value->tracks[0].points[1].x(), 123.457));
}

void TestBoard::downwardOutlineNormalizedOnLoad()
{
    QString text = QString::fromUtf8(encodeFixture(makeBoard()));
    QStringList lines = text.split(QChar('\n'));
    for (QString& line : lines) {
        if (line.startsWith(QStringLiteral("outline "))) {
            line = QStringLiteral("outline 4 0 0 0 1000 2000 1000 2000 0");
        }
    }
    text = lines.join(QChar('\n'));

    const Result<Board> decoded = decodeBoard(text.toUtf8(), resolver());
    QVERIFY2(decoded.status.ok, qPrintable(decoded.status.message));
    QVERIFY(signedArea(decoded.value->outline) > 0.0);
}

void TestBoard::negativeCoordinatesRoundTrip()
{
    Board board = makeBoard();
    board.outline = polygon({{-1000, -500}, {1000, -500}, {1000, 500}, {-1000, 500}});
    board.tracks[0].points = {QPointF(-100, -100), QPointF(0, 0)};
    board.vias[0].pos = QPointF(0, 0);
    board.pours[0].points = polygon({{-500, -200}, {0, -200}, {0, 0}, {-500, 0}});

    const QByteArray data = encodeFixture(board);
    const Result<Board> decoded = decodeBoard(data, resolver());
    QVERIFY2(decoded.status.ok, qPrintable(decoded.status.message));
    requireSameBoard(board, *decoded.value);
}

void TestBoard::missingFootprintRejected()
{
    const FootprintResolver missing = [](const QString&) -> Result<Footprint> {
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
    const Status status = validateBoard(makeBoard(), missing);
    QVERIFY(!status.ok);
    QVERIFY(status.message.contains(QStringLiteral("找不到")));
}

void TestBoard::resolverWrongFootprintIdRejected()
{
    const FootprintResolver wrong = [](const QString&) -> Result<Footprint> {
        Footprint footprint = makeTestFootprint();
        footprint.id = QStringLiteral("other");
        return {Status::success(), std::move(footprint)};
    };
    const Status status = validateBoard(makeBoard(), wrong);
    QVERIFY(!status.ok);
    QVERIFY(status.message.contains(QStringLiteral("错误的封装 ID")));
}

void TestBoard::unknownFootprintPinRejected()
{
    Board board = makeBoard();
    board.nets[0].pins[0].pinNumber = 99;
    const Status status = validateBoard(board, resolver());
    QVERIFY(!status.ok);
    QVERIFY(status.message.contains(QStringLiteral("引脚")));
}

void TestBoard::badHeaderRejected()
{
    const Result<Board> result = decodeBoard("EDA-BRD 99\nend\n", resolver());
    QVERIFY(!result.status.ok);
    QVERIFY(!result.value.has_value());
}

void TestBoard::missingEndRejected()
{
    QByteArray data = encodeFixture(makeBoard());
    data.chop(4); // "end\n"
    QVERIFY(!decodeBoard(data, resolver()).status.ok);
}

void TestBoard::trailingDataRejected()
{
    QByteArray data = encodeFixture(makeBoard());
    data.append("GARBAGE\n");
    QVERIFY(!decodeBoard(data, resolver()).status.ok);
}

void TestBoard::unknownRecordRejected()
{
    QByteArray data = encodeFixture(makeBoard());
    data.replace("\nend\n", "\nbogus 1\nend\n");
    QVERIFY(!decodeBoard(data, resolver()).status.ok);
}

void TestBoard::saveLoadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.brd"));

    const Board original = makeBoard();
    const Status saved = saveBoard(path, original, resolver());
    QVERIFY2(saved.ok, qPrintable(saved.message));

    const Result<Board> loaded = loadBoard(path, resolver());
    QVERIFY2(loaded.status.ok, qPrintable(loaded.status.message));
    requireSameBoard(original, *loaded.value);
}

void TestBoard::invalidSaveDoesNotOverwriteExistingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.brd"));

    const Board valid = makeBoard();
    QVERIFY(saveBoard(path, valid, resolver()).ok);

    Board invalid = makeBoard();
    invalid.tracks[0].layerId = kTopSilk;
    QVERIFY(!saveBoard(path, invalid, resolver()).ok);

    const Result<Board> loaded = loadBoard(path, resolver());
    QVERIFY2(loaded.status.ok, qPrintable(loaded.status.message));
    QCOMPARE(loaded.value->tracks.size(), valid.tracks.size());
    QCOMPARE(loaded.value->tracks[0].layerId, kTopCopper);
}

QTEST_APPLESS_MAIN(TestBoard)

#include "tst_board.moc"