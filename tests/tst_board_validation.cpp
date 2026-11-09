#include <QtTest>

#include "core/board.h"
#include "core/footprint.h"
#include "core/manufacturing_rules.h"
#include "core/validation.h"
#include "feat_pcb/board_validation.h"

#include <QColor>

#include <algorithm>

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

Board makeBoard()
{
    Board board;
    board.outline = polygon({{0, 0}, {2000, 0}, {2000, 1000}, {0, 1000}});
    board.groups.append(LayerGroup{1, QStringLiteral("Top"), 0});
    board.groups.append(LayerGroup{2, QStringLiteral("Bottom"), 1});
    board.groups.append(LayerGroup{3, QStringLiteral("Mechanical"), 2});

    addLayer(board, kTopCopper, 1, QStringLiteral("Top Copper"),
             BoardLayerType::Copper, QStringLiteral("#C83232"), 0);
    addLayer(board, kBottomCopper, 2, QStringLiteral("Bottom Copper"),
             BoardLayerType::Copper, QStringLiteral("#3264C8"), 0);
    addLayer(board, kTopSilk, 1, QStringLiteral("Top Silk"),
             BoardLayerType::Silk, QStringLiteral("#F0F0F0"), 1);
    addLayer(board, kBottomSilk, 2, QStringLiteral("Bottom Silk"),
             BoardLayerType::Silk, QStringLiteral("#D0D0D0"), 1);
    addLayer(board, kTopMask, 1, QStringLiteral("Top Mask"),
             BoardLayerType::Mask, QStringLiteral("#C8A000"), 2);
    addLayer(board, kBottomMask, 2, QStringLiteral("Bottom Mask"),
             BoardLayerType::Mask, QStringLiteral("#A08000"), 2);
    addLayer(board, kBoardOutline, 3, QStringLiteral("Board Outline"),
             BoardLayerType::Outline, QStringLiteral("#FFFF00"), 0);

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

class TestBoardValidation : public QObject {
    Q_OBJECT

private slots:
    void validBoardPasses();
    void structureErrorShortCircuits();
    void missingRequiredLayer();
    void duplicateGroupOrder();
    void duplicateLayerOrder();
    void instanceOutsideBoard();
    void trackWidthTooSmall();
    void trackOutsideBoard();
    void viaDrillOutOfRange();
    void viaAnnularRingTooSmall();
    void viaOutsideBoard();
    void pourClearanceTooSmall();
    void pourOutsideBoard();
    void missingFootprint();
    void noFootprintValidationWarning();
    void footprintCallbackIssuesAreIncluded();
    void unassignedNetIsWarning();
    void instanceCenterInsideButPadOutside();
    void boundaryValuesPass();
};

void TestBoardValidation::validBoardPasses()
{
    const FootprintValidation noop = [](const Footprint&) {
        return QVector<Issue>{};
    };
    const QVector<Issue> issues =
        validateBoardManufacturing(makeBoard(), resolver(), noop);
    QVERIFY2(issues.isEmpty(), qPrintable(issues.isEmpty()
                                              ? QString()
                                              : issues.first().message));
}

void TestBoardValidation::structureErrorShortCircuits()
{
    Board board = makeBoard();
    std::reverse(board.outline.begin(), board.outline.end());
    const QVector<Issue> issues = validateBoardManufacturing(board, resolver());
    QCOMPARE(issues.size(), 1);
    QCOMPARE(issues[0].code, issue_code::kBoardStructure);
}

void TestBoardValidation::missingRequiredLayer()
{
    Board board = makeBoard();
    for (int i = 0; i < board.layers.size(); ++i) {
        if (board.layers[i].id == kBottomSilk) {
            board.layers.removeAt(i);
            break;
        }
    }
    const QVector<Issue> issues = validateBoardManufacturing(board, resolver());
    QVERIFY(hasIssue(issues, issue_code::kBoardLayerMissing));
    for (const Issue& issue : issues) {
        if (issue.code == issue_code::kBoardLayerMissing) {
            QVERIFY(issue.severity == IssueSeverity::Warning);
        }
    }
}

void TestBoardValidation::duplicateGroupOrder()
{
    Board board = makeBoard();
    board.groups[1].stackOrder = board.groups[0].stackOrder;
    QVERIFY(hasIssue(validateBoardManufacturing(board, resolver()),
                     issue_code::kBoardGroupOrder));
}

void TestBoardValidation::duplicateLayerOrder()
{
    Board board = makeBoard();
    board.layers[2].stackOrder = board.layers[0].stackOrder;
    QVERIFY(hasIssue(validateBoardManufacturing(board, resolver()),
                     issue_code::kBoardLayerOrder));
}

void TestBoardValidation::instanceOutsideBoard()
{
    Board board = makeBoard();
    board.instances[1].pos = QPointF(2500, 500);
    QVERIFY(hasIssue(validateBoardManufacturing(board, resolver()),
                     issue_code::kBoardInstanceOutside));
}

void TestBoardValidation::trackWidthTooSmall()
{
    Board board = makeBoard();
    board.tracks[0].width = 3.0;
    const QVector<Issue> issues = validateBoardManufacturing(board, resolver());
    QVERIFY(hasIssue(issues, issue_code::kTrackWidth));
    for (const Issue& issue : issues) {
        if (issue.code == issue_code::kTrackWidth) {
            QCOMPARE(issue.objectId, quint64(board.tracks[0].id));
        }
    }
}

void TestBoardValidation::trackOutsideBoard()
{
    Board board = makeBoard();
    board.tracks[0].points[0] = QPointF(-10, 500);
    QVERIFY(hasIssue(validateBoardManufacturing(board, resolver()),
                     issue_code::kTrackOutside));
}

void TestBoardValidation::viaDrillOutOfRange()
{
    Board board = makeBoard();
    board.vias[0].drill = 3.0;
    const QVector<Issue> issues = validateBoardManufacturing(board, resolver());
    QVERIFY(hasIssue(issues, issue_code::kViaDrill));
    for (const Issue& issue : issues) {
        if (issue.code == issue_code::kViaDrill) {
            QCOMPARE(issue.objectId, quint64(board.vias[0].id));
        }
    }
}

void TestBoardValidation::viaAnnularRingTooSmall()
{
    Board board = makeBoard();
    board.vias[0].diameter = 30.0;
    QVERIFY(hasIssue(validateBoardManufacturing(board, resolver()),
                     issue_code::kViaAnnularRing));
}

void TestBoardValidation::viaOutsideBoard()
{
    Board board = makeBoard();
    board.vias[0].pos = QPointF(-10, 500);
    QVERIFY(hasIssue(validateBoardManufacturing(board, resolver()),
                     issue_code::kViaOutside));
}

void TestBoardValidation::pourClearanceTooSmall()
{
    Board board = makeBoard();
    board.pours[0].clearance = 1.0;
    const QVector<Issue> issues = validateBoardManufacturing(board, resolver());
    QVERIFY(hasIssue(issues, issue_code::kPourClearance));
    for (const Issue& issue : issues) {
        if (issue.code == issue_code::kPourClearance) {
            QCOMPARE(issue.objectId, quint64(board.pours[0].id));
        }
    }
}

void TestBoardValidation::pourOutsideBoard()
{
    Board board = makeBoard();
    board.pours[0].points = polygon({{-10, 0}, {1000, 0}, {1000, 500}, {-10, 500}});
    QVERIFY(hasIssue(validateBoardManufacturing(board, resolver()),
                     issue_code::kPourOutside));
}

void TestBoardValidation::missingFootprint()
{
    const FootprintResolver missing = [](const QString&) -> Result<Footprint> {
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
    const QVector<Issue> issues = validateBoardManufacturing(makeBoard(), missing);
    QVERIFY(hasIssue(issues, issue_code::kBoardReferences));
    QVERIFY(!hasIssue(issues, issue_code::kBoardFootprint));
}

void TestBoardValidation::noFootprintValidationWarning()
{
    const QVector<Issue> issues = validateBoardManufacturing(makeBoard(), resolver());
    QVERIFY(hasIssue(issues, issue_code::kBoardFootprintUnchecked));
}

void TestBoardValidation::footprintCallbackIssuesAreIncluded()
{
    const FootprintValidation callback = [](const Footprint&) {
        Issue issue;
        issue.severity = IssueSeverity::Error;
        issue.code = issue_code::kDrill;
        issue.message = QStringLiteral("封装钻孔非法");
        return QVector<Issue>{issue};
    };
    const QVector<Issue> issues =
        validateBoardManufacturing(makeBoard(), resolver(), callback);
    QVERIFY(!issues.isEmpty());
    bool found = false;
    for (const Issue& issue : issues) {
        if (issue.code == issue_code::kDrill && issue.refdes == QStringLiteral("U1")) {
            QVERIFY(issue.message.startsWith(QStringLiteral("U1: ")));
            found = true;
        }
    }
    QVERIFY(found);
}

void TestBoardValidation::instanceCenterInsideButPadOutside()
{
    Board board = makeBoard();
    board.instances[1].pos = QPointF(1950, 500);
    QVERIFY(hasIssue(validateBoardManufacturing(board, resolver()),
                     issue_code::kBoardInstanceOutside));
}

void TestBoardValidation::boundaryValuesPass()
{
    Board board = makeBoard();
    board.tracks[0].width = validation::kMinTrackWidthMil;
    board.vias[0].diameter = board.vias[0].drill + 2.0 * validation::kMinAnnularRingMil;
    board.pours[0].clearance = validation::kMinClearanceMil;

    const QVector<Issue> issues = validateBoardManufacturing(board, resolver());
    QVERIFY(!hasIssue(issues, issue_code::kTrackWidth));
    QVERIFY(!hasIssue(issues, issue_code::kViaAnnularRing));
    QVERIFY(!hasIssue(issues, issue_code::kPourClearance));
}

void TestBoardValidation::unassignedNetIsWarning()
{
    Board board = makeBoard();
    board.tracks[0].net.clear();
    const QVector<Issue> issues = validateBoardManufacturing(board, resolver());
    QVERIFY(hasIssue(issues, issue_code::kNetUnassigned));
    for (const Issue& issue : issues) {
        if (issue.code == issue_code::kNetUnassigned) {
            QVERIFY(issue.severity == IssueSeverity::Warning);
        }
    }
}

QTEST_APPLESS_MAIN(TestBoardValidation)

#include "tst_board_validation.moc"