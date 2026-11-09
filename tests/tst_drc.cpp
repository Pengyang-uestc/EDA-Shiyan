#include <QtTest>

#include "core/board.h"
#include "core/footprint.h"
#include "feat_pcb/drc.h"

#include <QColor>

using namespace eda;

namespace {

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

    Pad pad;
    pad.number = 1;
    pad.shape = PadShape::Circle;
    pad.pos = QPointF(0, 0);
    pad.size = QSizeF(60, 60);
    pad.drill = drill;
    pad.layer = kTopCopper;
    pad.clearance = 10.0;
    const Result<PadId> added = footprint.addPad(pad);
    requireOk(added.status);
    footprint.pins.append(Pin{1, QStringLiteral("1"),
                              ElectricalType::Passive, *added.value});
    return footprint;
}

Footprint makeMultiFootprint()
{
    Footprint footprint;
    footprint.id = QStringLiteral("test8");
    footprint.name = footprint.id;
    footprint.units = QStringLiteral("mil");

    for (int i = 1; i <= 8; ++i) {
        Pad pad;
        pad.number = i;
        pad.shape = PadShape::Circle;
        pad.pos = QPointF((i - 1) * 100, 0);
        pad.size = QSizeF(60, 60);
        pad.drill = 30;
        pad.layer = kTopCopper;
        pad.clearance = 10.0;
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
        if (id == QStringLiteral("single")) {
            return {Status::success(), makeFootprint(id, 30.0)};
        }
        if (id == QStringLiteral("smd")) {
            return {Status::success(), makeFootprint(id, 0.0)};
        }
        if (id == QStringLiteral("test8")) {
            return {Status::success(), makeMultiFootprint()};
        }
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
}

void addLayer(Board& board, LayerId id, LayerGroupId groupId, const QString& name,
              const QString& color)
{
    BoardLayer layer;
    layer.id = id;
    layer.groupId = groupId;
    layer.name = name;
    layer.type = BoardLayerType::Copper;
    layer.color = QColor(color);
    layer.stackOrder = id;
    board.layers.append(layer);
}

Board makeBoard(const QString& footprintId = QStringLiteral("single"))
{
    Board board;
    board.groups.append(LayerGroup{1, QStringLiteral("Top"), 0});
    board.groups.append(LayerGroup{2, QStringLiteral("Bottom"), 1});
    addLayer(board, kTopCopper, 1, QStringLiteral("Top Copper"),
             QStringLiteral("#C83232"));
    addLayer(board, kBottomCopper, 2, QStringLiteral("Bottom Copper"),
             QStringLiteral("#3264C8"));

    Instance instance;
    instance.refdes = QStringLiteral("U1");
    instance.footprintId = footprintId;
    instance.pos = QPointF(500, 500);
    board.instances.append(instance);

    Net net;
    net.name = QStringLiteral("N001");
    net.pins = {PinRef{QStringLiteral("U1"), 1}};
    board.nets.append(net);

    return board;
}

TrackId addTrack(Board& board, QVector<QPointF> points, LayerId layer,
                 const QString& net, double width = 12.0)
{
    Track track;
    track.id = board.allocateTrackId();
    track.layerId = layer;
    track.width = width;
    track.net = net;
    track.points = std::move(points);
    requireOk(board.insertTrack(track));
    return track.id;
}

QPolygonF polygon(std::initializer_list<QPointF> points)
{
    QPolygonF result;
    for (const QPointF& point : points) {
        result << point;
    }
    return result;
}

void addSecondSingle(Board& board, QPointF pos, const QString& netName)
{
    Instance instance;
    instance.refdes = QStringLiteral("U2");
    instance.footprintId = QStringLiteral("single");
    instance.pos = pos;
    board.instances.append(instance);

    Net net;
    net.name = netName;
    net.pins = {PinRef{QStringLiteral("U2"), 1}};
    board.nets.append(net);
}

PourId addPour(Board& board, const QPolygonF& points, LayerId layer,
               const QString& net)
{
    CopperPour pour;
    pour.id = board.allocatePourId();
    pour.layerId = layer;
    pour.net = net;
    pour.clearance = 10.0;
    pour.points = points;
    requireOk(board.insertCopperPour(pour));
    return pour.id;
}

ViaId addVia(Board& board, QPointF pos, const QString& net)
{
    Via via;
    via.id = board.allocateViaId();
    via.pos = pos;
    via.drill = 24.0;
    via.diameter = 40.0;
    via.fromLayer = kTopCopper;
    via.toLayer = kBottomCopper;
    via.net = net;
    requireOk(board.insertVia(via));
    return via.id;
}

bool hasViolation(const DrcResult& result, const QString& code)
{
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestDrc : public QObject {
    Q_OBJECT

private slots:
    void cleanBoard();
    void padTrackTooClose();
    void sameNetPadTrackIgnored();
    void padTrackAtBoundaryPasses();
    void trackTrackTooClose();
    void sameNetTrackTrackIgnored();
    void differentLayerIgnored();
    void viaTrackTooClose();
    void sameNetViaIgnored();
    void bottomSideSmdUsesBottomLayer();
    void multiPadUsesTruePadCenter();
    void rotatedBottomMultiPadUsesTruePadCenter();
    void ruleSwitchesDisableChecks();
    void newRuleSwitchesDisableChecks();
    void sameNetNewChecksIgnored();
    void padPadTooClose();
    void sameNetPadPadIgnored();
    void padViaTooClose();
    void padPourTooClose();
    void trackPourTooClose();
    void viaPourTooClose();
    void pourPourTooClose();
    void missingFootprintReportsError();
};

void TestDrc::cleanBoard()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(500, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    const DrcResult result = runDrc(board, resolver());
    QVERIFY2(result.clean(), qPrintable(result.violations.isEmpty()
                                            ? QString()
                                            : result.violations.first().message));
}

void TestDrc::padTrackTooClose()
{
    Board board = makeBoard();
    const TrackId trackId = addTrack(
        board, {QPointF(400, 535), QPointF(600, 535)}, kTopCopper,
        QStringLiteral("OTHER"));
    const DrcResult result = runDrc(board, resolver());
    QVERIFY(hasViolation(result, drc_code::kPadTrackClearance));
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kPadTrackClearance) {
            QCOMPARE(violation.refdes, QStringLiteral("U1"));
            QCOMPARE(violation.padId, PadId(1));
            QCOMPARE(violation.trackIdA, trackId);
        }
    }
}

void TestDrc::sameNetPadTrackIgnored()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(400, 535), QPointF(600, 535)}, kTopCopper,
             QStringLiteral("N001"));
    QVERIFY(runDrc(board, resolver()).clean());
}

void TestDrc::padTrackAtBoundaryPasses()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(400, 546), QPointF(600, 546)}, kTopCopper,
             QStringLiteral("OTHER"));
    QVERIFY(runDrc(board, resolver()).clean());
}

void TestDrc::trackTrackTooClose()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(0, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    addTrack(board, {QPointF(0, 510), QPointF(1000, 510)}, kTopCopper,
             QStringLiteral("OTHER"));
    QVERIFY(hasViolation(runDrc(board, resolver()),
                         drc_code::kTrackTrackClearance));
}

void TestDrc::sameNetTrackTrackIgnored()
{
    Board board = makeBoard();
    addTrack(board, {QPointF(0, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    addTrack(board, {QPointF(0, 510), QPointF(1000, 510)}, kTopCopper,
             QStringLiteral("N001"));
    QVERIFY(!hasViolation(runDrc(board, resolver()),
                          drc_code::kTrackTrackClearance));
}

void TestDrc::differentLayerIgnored()
{
    Board board = makeBoard(QStringLiteral("smd"));
    addTrack(board, {QPointF(0, 500), QPointF(1000, 500)},
             kTopCopper, QStringLiteral("N001"));
    addTrack(board, {QPointF(0, 510), QPointF(1000, 510)},
             kBottomCopper, QStringLiteral("OTHER"));
    QVERIFY(runDrc(board, resolver()).clean());
}

void TestDrc::viaTrackTooClose()
{
    Board board = makeBoard();
    const ViaId viaId = addVia(board, QPointF(500, 530), QStringLiteral("OTHER"));
    addTrack(board, {QPointF(0, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    const DrcResult result = runDrc(board, resolver());
    QVERIFY(hasViolation(result, drc_code::kViaTrackClearance));
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kViaTrackClearance) {
            QCOMPARE(violation.viaId, viaId);
            QVERIFY(violation.trackIdA != kInvalidId);
        }
    }
}

void TestDrc::sameNetViaIgnored()
{
    Board board = makeBoard();
    addVia(board, QPointF(500, 530), QStringLiteral("N001"));
    addTrack(board, {QPointF(0, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    QVERIFY(!hasViolation(runDrc(board, resolver()),
                          drc_code::kViaTrackClearance));
}

void TestDrc::bottomSideSmdUsesBottomLayer()
{
    Board board = makeBoard(QStringLiteral("smd"));
    board.instances[0].side = BoardSide::Bottom;

    addTrack(board, {QPointF(400, 535), QPointF(600, 535)}, kTopCopper,
             QStringLiteral("OTHER"));
    QVERIFY(!hasViolation(runDrc(board, resolver()),
                          drc_code::kPadTrackClearance));

    addTrack(board, {QPointF(400, 535), QPointF(600, 535)}, kBottomCopper,
             QStringLiteral("OTHER"));
    QVERIFY(hasViolation(runDrc(board, resolver()),
                         drc_code::kPadTrackClearance));
}

void TestDrc::rotatedBottomMultiPadUsesTruePadCenter()
{
    Board board = makeBoard(QStringLiteral("test8"));
    board.instances[0].side = BoardSide::Bottom;
    board.instances[0].rotationDeg = 90.0;

    addTrack(board, {QPointF(500, 400), QPointF(600, 400)}, kTopCopper,
             QStringLiteral("OTHER"));

    const DrcResult result = runDrc(board, resolver());
    bool foundPad2 = false;
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kPadTrackClearance
            && violation.padId == PadId(2)) {
            foundPad2 = true;
        }
    }
    QVERIFY(foundPad2);
}

void TestDrc::ruleSwitchesDisableChecks()
{
    Board padBoard = makeBoard();
    addTrack(padBoard, {QPointF(400, 535), QPointF(600, 535)}, kTopCopper,
             QStringLiteral("OTHER"));
    DrcRules rules;
    rules.checkPadTrack = false;
    QVERIFY(!hasViolation(runDrc(padBoard, resolver(), rules),
                          drc_code::kPadTrackClearance));

    Board trackBoard = makeBoard();
    addTrack(trackBoard, {QPointF(0, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    addTrack(trackBoard, {QPointF(0, 510), QPointF(1000, 510)}, kTopCopper,
             QStringLiteral("OTHER"));
    rules = DrcRules{};
    rules.checkTrackTrack = false;
    QVERIFY(!hasViolation(runDrc(trackBoard, resolver(), rules),
                          drc_code::kTrackTrackClearance));

    Board viaBoard = makeBoard();
    addVia(viaBoard, QPointF(500, 530), QStringLiteral("OTHER"));
    addTrack(viaBoard, {QPointF(0, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    rules = DrcRules{};
    rules.checkViaTrack = false;
    QVERIFY(!hasViolation(runDrc(viaBoard, resolver(), rules),
                          drc_code::kViaTrackClearance));
}

void TestDrc::multiPadUsesTruePadCenter()
{
    Board board = makeBoard(QStringLiteral("test8"));
    addTrack(board, {QPointF(550, 535), QPointF(650, 535)}, kTopCopper,
             QStringLiteral("OTHER"));

    const DrcResult result = runDrc(board, resolver());
    bool foundPad2 = false;
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kPadTrackClearance
            && violation.padId == PadId(2)) {
            foundPad2 = true;
        }
    }
    QVERIFY(foundPad2);
}

void TestDrc::newRuleSwitchesDisableChecks()
{
    Board board = makeBoard();
    addSecondSingle(board, QPointF(530, 500), QStringLiteral("N002"));
    addVia(board, QPointF(500, 540), QStringLiteral("OTHER"));
    addPour(board, polygon({{535, 470}, {700, 470}, {700, 530}, {535, 530}}),
            kTopCopper, QStringLiteral("OTHER"));

    DrcRules rules;
    rules.checkPadPad = false;
    rules.checkPadVia = false;
    rules.checkPadPour = false;
    rules.checkTrackPour = false;
    rules.checkViaPour = false;
    rules.checkPourPour = false;
    const DrcResult result = runDrc(board, resolver(), rules);
    QVERIFY(!hasViolation(result, drc_code::kPadPadClearance));
    QVERIFY(!hasViolation(result, drc_code::kPadViaClearance));
    QVERIFY(!hasViolation(result, drc_code::kPadPourClearance));
}

void TestDrc::sameNetNewChecksIgnored()
{
    Board padVia = makeBoard();
    addVia(padVia, QPointF(500, 540), QStringLiteral("N001"));
    QVERIFY(!hasViolation(runDrc(padVia, resolver()), drc_code::kPadViaClearance));

    Board padPour = makeBoard();
    addPour(padPour, polygon({{535, 470}, {700, 470}, {700, 530}, {535, 530}}),
            kTopCopper, QStringLiteral("N001"));
    QVERIFY(!hasViolation(runDrc(padPour, resolver()), drc_code::kPadPourClearance));

    Board trackPour = makeBoard();
    addTrack(trackPour, {QPointF(500, 500), QPointF(1000, 500)}, kTopCopper,
             QStringLiteral("N001"));
    addPour(trackPour, polygon({{900, 490}, {1100, 490}, {1100, 510}, {900, 510}}),
            kTopCopper, QStringLiteral("N001"));
    QVERIFY(!hasViolation(runDrc(trackPour, resolver()), drc_code::kTrackPourClearance));

    Board viaPour = makeBoard();
    addVia(viaPour, QPointF(540, 540), QStringLiteral("P"));
    addPour(viaPour, polygon({{530, 520}, {700, 520}, {700, 560}, {530, 560}}),
            kTopCopper, QStringLiteral("P"));
    QVERIFY(!hasViolation(runDrc(viaPour, resolver()), drc_code::kViaPourClearance));

    Board pourPour = makeBoard();
    addPour(pourPour, polygon({{0, 0}, {100, 0}, {100, 100}, {0, 100}}),
            kTopCopper, QStringLiteral("A"));
    addPour(pourPour, polygon({{95, 0}, {200, 0}, {200, 100}, {95, 100}}),
            kTopCopper, QStringLiteral("A"));
    QVERIFY(!hasViolation(runDrc(pourPour, resolver()), drc_code::kPourPourClearance));
}

void TestDrc::padPadTooClose()
{
    Board board = makeBoard();
    addSecondSingle(board, QPointF(530, 500), QStringLiteral("N002"));

    const DrcResult result = runDrc(board, resolver());
    QVERIFY(hasViolation(result, drc_code::kPadPadClearance));
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kPadPadClearance) {
            QCOMPARE(violation.refdes, QStringLiteral("U1"));
            QCOMPARE(violation.refdesB, QStringLiteral("U2"));
            QCOMPARE(violation.padId, PadId(1));
            QCOMPARE(violation.relatedPadId, PadId(1));
        }
    }
}

void TestDrc::sameNetPadPadIgnored()
{
    Board board = makeBoard();
    addSecondSingle(board, QPointF(530, 500), QStringLiteral("N001"));
    QVERIFY(!hasViolation(runDrc(board, resolver()),
                          drc_code::kPadPadClearance));
}

void TestDrc::padViaTooClose()
{
    Board board = makeBoard();
    const ViaId viaId = addVia(board, QPointF(500, 540), QStringLiteral("OTHER"));
    const DrcResult result = runDrc(board, resolver());
    QVERIFY(hasViolation(result, drc_code::kPadViaClearance));
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kPadViaClearance) {
            QCOMPARE(violation.padId, PadId(1));
            QCOMPARE(violation.viaId, viaId);
        }
    }
}

void TestDrc::padPourTooClose()
{
    Board board = makeBoard();
    const PourId pourId = addPour(
        board, polygon({{535, 470}, {700, 470}, {700, 530}, {535, 530}}),
        kTopCopper, QStringLiteral("OTHER"));
    const DrcResult result = runDrc(board, resolver());
    QVERIFY(hasViolation(result, drc_code::kPadPourClearance));
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kPadPourClearance) {
            QCOMPARE(violation.padId, PadId(1));
            QCOMPARE(violation.pourId, pourId);
        }
    }
}

void TestDrc::trackPourTooClose()
{
    Board board = makeBoard();
    const TrackId trackId = addTrack(board, {QPointF(500, 500), QPointF(1000, 500)},
                                     kTopCopper, QStringLiteral("N001"));
    const PourId pourId = addPour(
        board, polygon({{900, 490}, {1100, 490}, {1100, 510}, {900, 510}}),
        kTopCopper, QStringLiteral("OTHER"));
    const DrcResult result = runDrc(board, resolver());
    QVERIFY(hasViolation(result, drc_code::kTrackPourClearance));
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kTrackPourClearance) {
            QCOMPARE(violation.trackIdA, trackId);
            QCOMPARE(violation.pourId, pourId);
        }
    }
}

void TestDrc::viaPourTooClose()
{
    Board board = makeBoard();
    const ViaId viaId = addVia(board, QPointF(540, 540), QStringLiteral("V"));
    const PourId pourId = addPour(
        board, polygon({{530, 520}, {700, 520}, {700, 560}, {530, 560}}),
        kTopCopper, QStringLiteral("P"));
    const DrcResult result = runDrc(board, resolver());
    QVERIFY(hasViolation(result, drc_code::kViaPourClearance));
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kViaPourClearance) {
            QCOMPARE(violation.viaId, viaId);
            QCOMPARE(violation.pourId, pourId);
        }
    }
}

void TestDrc::pourPourTooClose()
{
    Board board = makeBoard();
    const PourId first = addPour(
        board, polygon({{0, 0}, {100, 0}, {100, 100}, {0, 100}}),
        kTopCopper, QStringLiteral("A"));
    const PourId second = addPour(
        board, polygon({{95, 0}, {200, 0}, {200, 100}, {95, 100}}),
        kTopCopper, QStringLiteral("B"));
    const DrcResult result = runDrc(board, resolver());
    QVERIFY(hasViolation(result, drc_code::kPourPourClearance));
    for (const DrcViolation& violation : result.violations) {
        if (violation.code == drc_code::kPourPourClearance) {
            const bool matched = (violation.pourId == first
                                  && violation.relatedPourId == second)
                                 || (violation.pourId == second
                                     && violation.relatedPourId == first);
            QVERIFY(matched);
        }
    }
}

void TestDrc::missingFootprintReportsError()
{
    const FootprintResolver missing = [](const QString&) -> Result<Footprint> {
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
    const DrcResult result = runDrc(makeBoard(), missing);
    QVERIFY(!result.errors.isEmpty());
    QVERIFY(!result.clean());
}

QTEST_APPLESS_MAIN(TestDrc)

#include "tst_drc.moc"