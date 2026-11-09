#include <QtTest>

#include "core/board.h"
#include "feat_pcb/layer_service.h"

using namespace eda;

namespace {

bool hasIssue(const QVector<Issue>& issues, const QString& code)
{
    for (const Issue& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

bool hasLayer(const Board& board, LayerId id)
{
    for (const BoardLayer& layer : board.layers) {
        if (layer.id == id) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestLayerService : public QObject {
    Q_OBJECT

private slots:
    void ensureDefaultsIsIdempotent();
    void addGroupAndDuplicates();
    void addLayerAndDuplicates();
    void removeLayerInUseFails();
    void removeGroupWithLayersFails();
    void updatesAreAtomic();
    void validateEmptyBoardWarns();
    void validateDetectsDuplicateIdsAndOrders();
    void validateDetectsBadLayer();
    void ensureDefaultsFillsPartialBoard();
};

void TestLayerService::ensureDefaultsIsIdempotent()
{
    Board board;
    QVERIFY(LayerService::ensureDefaultLayers(board).ok);
    QCOMPARE(board.groups.size(), 3);
    QCOMPARE(board.layers.size(), 7);
    QVERIFY(hasLayer(board, kTopCopper));
    QVERIFY(hasLayer(board, kBoardOutline));

    QVERIFY(LayerService::ensureDefaultLayers(board).ok);
    QCOMPARE(board.groups.size(), 3);
    QCOMPARE(board.layers.size(), 7);
}

void TestLayerService::addGroupAndDuplicates()
{
    Board board;
    QVERIFY(LayerService::ensureDefaultLayers(board).ok);

    const Result<LayerGroupId> added =
        LayerService::addGroup(board, QStringLiteral("Inner"), 10);
    QVERIFY(added.status.ok);
    QCOMPARE(board.groups.size(), 4);

    QVERIFY(!LayerService::addGroup(board, QStringLiteral("Inner"), 11).status.ok);
    QVERIFY(!LayerService::addGroup(board, QStringLiteral("Top"), 12).status.ok);
    QVERIFY(!LayerService::addGroup(board, QStringLiteral("Other"), 10).status.ok);
}

void TestLayerService::addLayerAndDuplicates()
{
    Board board;
    QVERIFY(LayerService::ensureDefaultLayers(board).ok);
    const LayerGroupId group = 3;

    const Result<LayerId> added = LayerService::addLayer(
        board, group, QStringLiteral("Inner 1"), BoardLayerType::Copper,
        QColor(QStringLiteral("#A0A0A0")), 3);
    QVERIFY(added.status.ok);
    QVERIFY(*added.value >= 100);
    QVERIFY(hasLayer(board, *added.value));

    QVERIFY(!LayerService::addLayer(board, group, QStringLiteral("Inner 1"),
                                    BoardLayerType::Copper,
                                    QColor(QStringLiteral("#A0A0A0")), 4)
                 .status.ok);
    QVERIFY(!LayerService::addLayer(board, group, QStringLiteral("Inner 2"),
                                    BoardLayerType::Copper,
                                    QColor(QStringLiteral("#A0A0A0")), 3)
                 .status.ok);
    QVERIFY(!LayerService::addLayer(board, 99, QStringLiteral("Bad"),
                                    BoardLayerType::Copper,
                                    QColor(QStringLiteral("#A0A0A0")), 0)
                 .status.ok);
}

void TestLayerService::removeLayerInUseFails()
{
    Board board;
    QVERIFY(LayerService::ensureDefaultLayers(board).ok);
    QVERIFY(!LayerService::removeLayer(board, kTopCopper).ok);

    const Result<LayerId> custom = LayerService::addLayer(
        board, 3, QStringLiteral("Inner 1"), BoardLayerType::Copper,
        QColor(QStringLiteral("#A0A0A0")), 5);
    QVERIFY(custom.status.ok);

    Track track;
    track.id = board.allocateTrackId();
    track.layerId = *custom.value;
    track.points = {QPointF(0, 0), QPointF(10, 0)};
    QVERIFY(board.insertTrack(track).ok);

    QVERIFY(!LayerService::removeLayer(board, *custom.value).ok);
    QVERIFY(board.removeTrack(track.id).ok);
    QVERIFY(LayerService::removeLayer(board, *custom.value).ok);
}

void TestLayerService::removeGroupWithLayersFails()
{
    Board board;
    QVERIFY(LayerService::ensureDefaultLayers(board).ok);
    QVERIFY(!LayerService::removeGroup(board, 1).ok);

    const Result<LayerGroupId> group =
        LayerService::addGroup(board, QStringLiteral("Custom"), 10);
    QVERIFY(group.status.ok);
    const Result<LayerId> layer = LayerService::addLayer(
        board, *group.value, QStringLiteral("Inner 1"),
        BoardLayerType::Copper, QColor(QStringLiteral("#A0A0A0")), 0);
    QVERIFY(layer.status.ok);

    QVERIFY(!LayerService::removeGroup(board, *group.value).ok);
    QVERIFY(LayerService::removeLayer(board, *layer.value).ok);
    QVERIFY(LayerService::removeGroup(board, *group.value).ok);
}

void TestLayerService::updatesAreAtomic()
{
    Board board;
    QVERIFY(LayerService::ensureDefaultLayers(board).ok);
    const QColor originalColor = board.layers[0].color;
    const int originalOrder = board.layers[0].stackOrder;

    QVERIFY(!LayerService::renameLayer(board, kTopCopper, QString()).ok);
    QVERIFY(!LayerService::renameLayer(board, kTopCopper,
                                       QStringLiteral("Top Silk")).ok);
    QVERIFY(!LayerService::renameLayer(board, kTopCopper,
                                       QStringLiteral("Renamed")).ok);
    QVERIFY(!LayerService::setLayerColor(board, kTopCopper, QColor()).ok);
    QVERIFY(!LayerService::setLayerStackOrder(board, kTopCopper,
                                              board.layers[2].stackOrder).ok);

    QCOMPARE(board.layers[0].color, originalColor);
    QCOMPARE(board.layers[0].stackOrder, originalOrder);
    QVERIFY(LayerService::setGroupStackOrder(board, 1, 10).ok);
    QVERIFY(!LayerService::setGroupStackOrder(board, 1, 1).ok);
}

void TestLayerService::validateEmptyBoardWarns()
{
    Board board;
    const QVector<Issue> issues = LayerService::validate(board);
    QVERIFY(hasIssue(issues, issue_code::kLayerStructure));
    bool hasError = false;
    for (const Issue& issue : issues) {
        if (issue.code == issue_code::kLayerStructure
            && issue.severity == IssueSeverity::Error) {
            hasError = true;
        }
    }
    QVERIFY(hasError);
}

void TestLayerService::validateDetectsDuplicateIdsAndOrders()
{
    Board board;
    QVERIFY(LayerService::ensureDefaultLayers(board).ok);
    board.groups.append(board.groups[0]);
    board.layers.append(board.layers[0]);

    const QVector<Issue> issues = LayerService::validate(board);
    QVERIFY(hasIssue(issues, issue_code::kLayerOrder));
}

void TestLayerService::validateDetectsBadLayer()
{
    Board board;
    QVERIFY(LayerService::ensureDefaultLayers(board).ok);
    board.layers[0].type = BoardLayerType::Silk;
    board.layers[1].color = QColor();

    const QVector<Issue> issues = LayerService::validate(board);
    QVERIFY(hasIssue(issues, issue_code::kLayerStructure));
}

void TestLayerService::ensureDefaultsFillsPartialBoard()
{
    Board board;
    board.groups.append(LayerGroup{1, QStringLiteral("Top"), 0});
    board.layers.append(BoardLayer{kTopCopper, 1, QStringLiteral("Top Copper"),
                                   BoardLayerType::Copper,
                                   QColor(QStringLiteral("#C83232")), 0});
    QVERIFY(LayerService::ensureDefaultLayers(board).ok);
    QCOMPARE(board.groups.size(), 3);
    QCOMPARE(board.layers.size(), 7);
}

QTEST_APPLESS_MAIN(TestLayerService)

#include "tst_layer_service.moc"