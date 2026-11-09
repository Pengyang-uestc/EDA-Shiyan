#include <QtTest>

#include "core/footprint.h"
#include "feat_pcb/netlist_service.h"

#include <QFile>
#include <QTemporaryDir>

using namespace eda;

namespace {

const char* kNetlist =
    "COMP U1 test8\n"
    "COMP U2 test8\n"
    "NET N001 U1.1 U2.1\n"
    "NET GND U1.8 U2.8\n";

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

bool hasWarning(const NetlistImportResult& result, const QString& text)
{
    for (const QString& warning : result.warnings) {
        if (warning.contains(text)) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestNetlistService : public QObject {
    Q_OBJECT

private slots:
    void importDataBuildsBoard();
    void importFileMatchesImportData();
    void autoLayoutGivesDistinctPositions();
    void missingFootprintFails();
    void unknownPinFails();
    void warningsForUnconnectedComponentAndSmallNet();
    void emptyNetlistRejected();
    void invalidLayoutOptionsRejected();
    void wrongFootprintIdRejected();
    void columnsOneStacksVertically();
};

void TestNetlistService::importDataBuildsBoard()
{
    const NetlistService service;
    const Result<NetlistImportResult> result =
        service.importData(kNetlist, resolver());
    QVERIFY2(result.status.ok, qPrintable(result.status.message));
    QVERIFY(result.value.has_value());

    const Board& board = result.value->board;
    QCOMPARE(board.instances.size(), 2);
    QCOMPARE(board.nets.size(), 2);
    QCOMPARE(board.layers.size(), 7);
    QVERIFY(board.outline.size() >= 4);
    QCOMPARE(board.instances[0].refdes, QStringLiteral("U1"));
    QCOMPARE(board.instances[0].footprintId, QStringLiteral("test8"));
    QCOMPARE(board.nets[0].name, QStringLiteral("N001"));
    QCOMPARE(board.nets[0].pins.size(), 2);
    QCOMPARE(board.nets[0].pins[0].refdes, QStringLiteral("U1"));
    QCOMPARE(board.nets[0].pins[0].pinNumber, 1);
    QCOMPARE(board.nets[0].pins[1].refdes, QStringLiteral("U2"));
    QCOMPARE(board.nets[0].pins[1].pinNumber, 1);
    QVERIFY(board.outline.boundingRect().width() >= 2000.0);
    QVERIFY(validateBoard(board, resolver()).ok);
}

void TestNetlistService::importFileMatchesImportData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("test.net"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(kNetlist);
    file.close();

    const NetlistService service;
    const Result<NetlistImportResult> result =
        service.importFile(path, resolver());
    QVERIFY2(result.status.ok, qPrintable(result.status.message));
    QCOMPARE(result.value->board.instances.size(), 2);
    QCOMPARE(result.value->board.nets.size(), 2);

    const Result<NetlistImportResult> fromData =
        service.importData(kNetlist, resolver());
    QVERIFY2(fromData.status.ok, qPrintable(fromData.status.message));
    const Result<QByteArray> a = encodeBoard(result.value->board, resolver());
    const Result<QByteArray> b = encodeBoard(fromData.value->board, resolver());
    QVERIFY(a.status.ok && b.status.ok);
    QCOMPARE(*a.value, *b.value);
}

void TestNetlistService::autoLayoutGivesDistinctPositions()
{
    const NetlistService service;
    const Result<NetlistImportResult> result =
        service.importData(kNetlist, resolver());
    QVERIFY2(result.status.ok, qPrintable(result.status.message));

    const Board& board = result.value->board;
    QCOMPARE(board.instances.size(), 2);
    QVERIFY(board.instances[0].pos != board.instances[1].pos);
    QVERIFY(board.instances[0].pos.x() >= 0.0);
    QVERIFY(board.instances[0].pos.y() >= 0.0);
    QVERIFY(std::abs(board.instances[0].pos.x() - board.instances[1].pos.x())
            >= 760.0);
    QVERIFY(validateBoard(board, resolver()).ok);
}

void TestNetlistService::missingFootprintFails()
{
    const FootprintResolver missing = [](const QString&) -> Result<Footprint> {
        return {Status::failure(QStringLiteral("找不到封装")), std::nullopt};
    };
    const NetlistService service;
    const Result<NetlistImportResult> result =
        service.importData(kNetlist, missing);
    QVERIFY(!result.status.ok);
    QVERIFY(result.status.message.contains(QStringLiteral("无法解析")));
}

void TestNetlistService::unknownPinFails()
{
    const QByteArray data =
        "COMP U1 test8\n"
        "COMP U2 test8\n"
        "NET BAD U1.99 U2.1\n";
    const NetlistService service;
    const Result<NetlistImportResult> result =
        service.importData(data, resolver());
    QVERIFY(!result.status.ok);
    QVERIFY(result.status.message.contains(QStringLiteral("引脚"))
            || result.status.message.contains(QStringLiteral("U1")));
}

void TestNetlistService::warningsForUnconnectedComponentAndSmallNet()
{
    const QByteArray data =
        "COMP U1 test8\n"
        "COMP U2 test8\n"
        "NET ONE U1.1\n";
    const NetlistService service;
    const Result<NetlistImportResult> result =
        service.importData(data, resolver());
    QVERIFY2(result.status.ok, qPrintable(result.status.message));
    QVERIFY(hasWarning(*result.value, QStringLiteral("少于两个引脚")));
    QVERIFY(hasWarning(*result.value, QStringLiteral("没有被任何网络引用")));
}

void TestNetlistService::emptyNetlistRejected()
{
    const NetlistService service;
    const Result<NetlistImportResult> result =
        service.importData(QByteArray(), resolver());
    QVERIFY(!result.status.ok);
}

void TestNetlistService::invalidLayoutOptionsRejected()
{
    const NetlistService service;
    NetlistImportOptions options;
    options.gapMil = -10.0;
    QVERIFY(!service.importData(kNetlist, resolver(), options).status.ok);

    options = NetlistImportOptions{};
    options.origin = QPointF(-1, 0);
    QVERIFY(!service.importData(kNetlist, resolver(), options).status.ok);
}

void TestNetlistService::wrongFootprintIdRejected()
{
    const FootprintResolver wrong = [](const QString&) -> Result<Footprint> {
        Footprint footprint = makeTestFootprint();
        footprint.id = QStringLiteral("other");
        return {Status::success(), std::move(footprint)};
    };
    const NetlistService service;
    const Result<NetlistImportResult> result =
        service.importData(kNetlist, wrong);
    QVERIFY(!result.status.ok);
    QVERIFY(result.status.message.contains(QStringLiteral("错误的 ID")));
}

void TestNetlistService::columnsOneStacksVertically()
{
    const NetlistService service;
    NetlistImportOptions options;
    options.columns = 1;
    const Result<NetlistImportResult> result =
        service.importData(kNetlist, resolver(), options);
    QVERIFY2(result.status.ok, qPrintable(result.status.message));
    const Board& board = result.value->board;
    QCOMPARE(board.instances.size(), 2);
    QVERIFY(std::abs(board.instances[0].pos.x() - board.instances[1].pos.x()) < 1e-6);
    QVERIFY(std::abs(board.instances[0].pos.y() - board.instances[1].pos.y()) >= 120.0);
}

QTEST_APPLESS_MAIN(TestNetlistService)

#include "tst_netlist_service.moc"