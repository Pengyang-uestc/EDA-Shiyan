#include <QtTest>

#include "core/board.h"
#include "feat_pcb/layer_model.h"

#include <QColor>

using namespace eda;

namespace {

Board makeBoard()
{
    Board board;
    board.groups.append(LayerGroup{1, QStringLiteral("Top"), 0});
    board.groups.append(LayerGroup{2, QStringLiteral("Bottom"), 1});

    BoardLayer top;
    top.id = kTopCopper;
    top.groupId = 1;
    top.name = QStringLiteral("Top Copper");
    top.type = BoardLayerType::Copper;
    top.color = QColor(QStringLiteral("#C83232"));
    top.stackOrder = 0;
    board.layers.append(top);

    BoardLayer silk;
    silk.id = kTopSilk;
    silk.groupId = 1;
    silk.name = QStringLiteral("Top Silk");
    silk.type = BoardLayerType::Silk;
    silk.color = QColor(QStringLiteral("#F0F0F0"));
    silk.stackOrder = 1;
    board.layers.append(silk);

    BoardLayer bottom;
    bottom.id = kBottomCopper;
    bottom.groupId = 2;
    bottom.name = QStringLiteral("Bottom Copper");
    bottom.type = BoardLayerType::Copper;
    bottom.color = QColor(QStringLiteral("#3264C8"));
    bottom.stackOrder = 0;
    board.layers.append(bottom);
    return board;
}

} // namespace

class TestLayerModel : public QObject {
    Q_OBJECT

private slots:
    void buildsTree();
    void rolesAndDisplay();
    void indexForLayer();
    void refreshAfterChange();
    void nullBoardClears();
};

void TestLayerModel::buildsTree()
{
    Board board = makeBoard();
    LayerModel model;
    model.setBoard(&board);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowCount(model.index(0, 0)), 2);
    QCOMPARE(model.rowCount(model.index(1, 0)), 1);
    QCOMPARE(model.columnCount(), 4);
    QCOMPARE(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(),
             QStringLiteral("层"));
}

void TestLayerModel::rolesAndDisplay()
{
    Board board = makeBoard();
    LayerModel model;
    model.setBoard(&board);

    const QModelIndex group = model.index(0, 0);
    QVERIFY(group.data(LayerModel::IsGroupRole).toBool());
    QCOMPARE(group.data(LayerModel::GroupIdRole).toInt(), 1);

    const QModelIndex layer = model.index(0, 0, group);
    QVERIFY(!layer.data(LayerModel::IsGroupRole).toBool());
    QCOMPARE(layer.data(LayerModel::LayerIdRole).toInt(), int(kTopCopper));
    QCOMPARE(layer.data(Qt::DisplayRole).toString(), QStringLiteral("Top Copper"));
    QCOMPARE(layer.data(LayerModel::ColorRole).value<QColor>(),
             QColor(QStringLiteral("#C83232")));
    QCOMPARE(layer.data(LayerModel::LayerTypeRole).toInt(),
             int(BoardLayerType::Copper));
}

void TestLayerModel::indexForLayer()
{
    Board board = makeBoard();
    LayerModel model;
    model.setBoard(&board);

    const QModelIndex index = model.indexForLayer(kTopSilk);
    QVERIFY(index.isValid());
    QCOMPARE(model.layerId(index), kTopSilk);
    QCOMPARE(index.data(Qt::DisplayRole).toString(), QStringLiteral("Top Silk"));
    QVERIFY(!model.indexForLayer(12345).isValid());
}

void TestLayerModel::refreshAfterChange()
{
    Board board = makeBoard();
    LayerModel model;
    model.setBoard(&board);
    QCOMPARE(model.rowCount(model.index(0, 0)), 2);

    BoardLayer extra;
    extra.id = 100;
    extra.groupId = 1;
    extra.name = QStringLiteral("Inner 1");
    extra.type = BoardLayerType::Copper;
    extra.color = QColor(QStringLiteral("#A0A0A0"));
    extra.stackOrder = 2;
    board.layers.append(extra);
    model.refresh();

    QCOMPARE(model.rowCount(model.index(0, 0)), 3);
    QVERIFY(model.indexForLayer(100).isValid());
}

void TestLayerModel::nullBoardClears()
{
    Board board = makeBoard();
    LayerModel model;
    model.setBoard(&board);
    model.setBoard(nullptr);
    QCOMPARE(model.rowCount(), 0);
}

QTEST_APPLESS_MAIN(TestLayerModel)

#include "tst_layer_model.moc"