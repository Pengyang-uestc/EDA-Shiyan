#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "core/io.h"
#include "core/geometry.h"
#include "core/library.h"
using namespace eda;
class CoreTest : public QObject {
    Q_OBJECT
  private slots:
    void stableIds() {
        auto fp = exampleFootprint();
        QVERIFY(!fp.removePad(fp.pads[0].id).ok);
        fp.pins.clear();
        const auto deleted = fp.pads.last().id;
        QVERIFY(fp.removePad(deleted).ok);
        auto added = fp.addPad(Pad{});
        QVERIFY(added.status.ok);
        QVERIFY(*added.value > deleted);
        auto decoded = decodeFootprint(*encodeFootprint(fp).value);
        QVERIFY(decoded.status.ok);
        QCOMPARE(decoded.value->nextPadId, fp.nextPadId);
    }
    void malformedInput() {
        const auto data = *encodeFootprint(exampleFootprint()).value;
        auto unknown = data;
        unknown.replace("pad 1 1 circle", "unknown 1 1 circle");
        auto result = decodeFootprint(unknown);
        QVERIFY(!result.status.ok);
        QVERIFY(!result.value.has_value());
        QVERIFY(result.status.message.contains(QStringLiteral("第 5 行")));
        auto invalidUtf8 = data;
        invalidUtf8.append('\xFF');
        QVERIFY(!decodeFootprint(invalidUtf8).status.ok);
        auto invalid = exampleFootprint();
        invalid.silk.push_back(nullptr);
        QVERIFY(!encodeFootprint(invalid).status.ok);
    }
    void libraryCorruption() {
        QTemporaryDir dir(QDir::currentPath() + "/case-XXXXXX");
        QVERIFY(dir.isValid());
        FootprintLibrary lib(dir.path());
        auto fp = exampleFootprint();
        QVERIFY(lib.save(fp).ok);
        QVERIFY(!lib.load("con").status.ok);
        QFile file(dir.filePath("footprints/broken.fp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("EDA-FP 1\n");
        file.close();
        auto listed = lib.list();
        QVERIFY(!listed.status.ok);
        QVERIFY(!listed.value.has_value());
        QVERIFY(listed.status.message.contains("broken.fp"));
        QVERIFY(!lib.rebuildIndex().ok);
    }
    void roundTrip() {
        auto a = exampleFootprint();
        a.name = QString::fromUtf8("中文 名称\"\\\n");
        a.pads[0].id = Q_UINT64_C(9007199254740993);
        a.pins[0].padId = a.pads[0].id;
        a.nextPadId = a.pads[0].id + 1;
        auto encoded = encodeFootprint(a);
        QVERIFY(encoded.status.ok);
        auto b = decodeFootprint(*encoded.value);
        QVERIFY2(b.status.ok, qPrintable(b.status.message));
        QCOMPARE(*encoded.value, *encodeFootprint(*b.value).value);
        Footprint copy = a;
        copy.silk[0]->width = 88;
        QVERIFY(a.silk[0]->width != copy.silk[0]->width);
    }
    void failureIsolation() {
        auto fp = exampleFootprint();
        auto before = *encodeFootprint(fp).value;

        QVERIFY(!decodeFootprint("EDA-FP 99\nEND\n").status.ok);
        QCOMPARE(*encodeFootprint(fp).value, before);
        auto truncated = before.left(before.lastIndexOf("end\n"));
        QVERIFY(!decodeFootprint(truncated).status.ok);
        QVERIFY(!decodeFootprint(before + "GARBAGE\n").status.ok);
        QCOMPARE(*encodeFootprint(fp).value, before);
        auto malformed = before;
        malformed.replace("pad 1 1 circle", "pad 1 1 invalid");
        QVERIFY(malformed != before);
        QVERIFY(!decodeFootprint(malformed).status.ok);
    }
    void structuralErrors() {
        auto fp = exampleFootprint();
        QString e;
        fp.pins[0].padId = 999;
        QVERIFY(!validateStructure(fp).ok);
        fp = exampleFootprint();
        fp.pads.append(fp.pads[0]);
        QVERIFY(!validateStructure(fp).ok);
        fp = exampleFootprint();
        fp.nextPadId = fp.pads[0].id;
        QVERIFY(!validateStructure(fp).ok);
    }
    void atomicSave() {
        QTemporaryDir dir(QDir::currentPath() + "/case-XXXXXX");
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QStringLiteral("中文.fp"));
        auto fp = exampleFootprint();
        QString e;
        auto saved = saveFootprint(path, fp);
        QVERIFY2(saved.ok, qPrintable(saved.message));
        fp.pins[0].padId = 999;
        QVERIFY(!saveFootprint(path, fp).ok);
        auto loaded = loadFootprint(path);
        QVERIFY(loaded.status.ok);
        QCOMPARE(*encodeFootprint(*loaded.value).value, *encodeFootprint(exampleFootprint()).value);
    }
    void libraryLifecycle() {
        QTemporaryDir dir(QDir::currentPath() + "/case-XXXXXX");
        FootprintLibrary lib(dir.path());
        QString e;
        auto fp = exampleFootprint();
        auto saved = lib.save(fp);
        QVERIFY2(saved.ok, qPrintable(saved.message));
        QVector<LibraryEntry> rows;
        auto refresh = [&]() {
            auto r = lib.list();
            if (r.status.ok)
                rows = *r.value;
            return r.status.ok;
        };
        QVERIFY(refresh());
        QCOMPARE(rows.size(), 1);
        QVERIFY(lib.rebuildIndex().ok);
        QFile index(dir.filePath("index.idx"));
        QVERIFY(index.open(QIODevice::ReadOnly));
        QVERIFY(index.readAll().startsWith("EDA-LIB 1\n"));
        index.close();
        QFile broken(dir.filePath("index.idx"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("broken");
        broken.close();
        QVERIFY(refresh());
        QCOMPARE(rows.size(), 1);
        QVERIFY(lib.rebuildIndex().ok);
        auto loaded = lib.load(fp.id);
        QVERIFY(loaded.status.ok);
        QCOMPARE(*encodeFootprint(*loaded.value).value, *encodeFootprint(fp).value);
        QVERIFY(!lib.load("../escape").status.ok);
        QVERIFY(!lib.remove("../escape").ok);
        fp.id = "../escape";
        QVERIFY(!lib.save(fp).ok);
        QVERIFY(lib.remove("example").ok);
        QVERIFY(refresh());
        QVERIFY(rows.isEmpty());
    }
    void boardNetlist() {
        const auto netlist = decodeNetlist("# sample\r\nNET N1 U1.1 U2.2\r\nCOMP U1 example\r\nCOMP U2 example\r\n");
        QVERIFY2(netlist.status.ok, qPrintable(netlist.status.message));
        QCOMPARE(netlist.value->components.size(), 2);
        int lookups = 0;
        const auto resolved = resolveNetlistPins(*netlist.value, [&](const QString&) {
            ++lookups;
            auto footprint = exampleFootprint();
            footprint.pins[1].number = 2;
            footprint.pins[1].padId = footprint.pads[2].id;
            return Result<Footprint>{Status::success(), std::move(footprint)};
        });
        QVERIFY2(resolved.status.ok, qPrintable(resolved.status.message));
        QCOMPARE(lookups, 1);
        QCOMPARE(resolved.value->at(1).padId, PadId{3});

        const auto unicode = decodeNetlist(QStringLiteral("COMP 模块.A example\nNET 电源 模块.A.1\n").toUtf8());
        QVERIFY2(unicode.status.ok, qPrintable(unicode.status.message));
        QCOMPARE(unicode.value->nets[0].pins[0].refdes, QStringLiteral("模块.A"));
        const auto dangling = decodeNetlist("COMP U1 example\nNET N1 U2.1\n");
        QVERIFY(!dangling.status.ok);
        QVERIFY(dangling.status.message.contains(QStringLiteral("第 2 行")));
        QVERIFY(!decodeNetlist("COMP U1 example\nNET N1 U1.01\nNET N2 U1.1\n").status.ok);
        QVERIFY(!decodeNetlist(QByteArray("COMP U1 example\n#") + '\xE4').status.ok);
        const auto missingPin = decodeNetlist("COMP U1 example\nNET N1 U1.99\n");
        QVERIFY(missingPin.status.ok);
        QVERIFY(!resolveNetlistPins(*missingPin.value, [](const QString&) {
            return Result<Footprint>{Status::success(), exampleFootprint()};
        }).status.ok);
    }

    void boardGeometry() {
        const QPolygonF square{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
        QCOMPARE(distanceSegmentToSegment({0, 0}, {4, 0}, {2, -2}, {2, 2}), 0.0);
        QCOMPARE(distanceSegmentToSegment({0, 0}, {4, 0}, {2, 3}, {5, 3}), 3.0);
        QCOMPARE(distancePointToSegment({5e154, 1}, {0, 0}, {1e155, 0}), 1.0);
        QCOMPARE(distancePointToPolygon({2, 2}, square), 0.0);
        QCOMPARE(distancePointToPolygon({7, 2}, square), 3.0);
        QCOMPARE(distanceSegmentToPolygon({-1, 2}, {5, 2}, square), 0.0);
        QCOMPARE(distanceSegmentToPolygon({1, 7}, {3, 7}, square), 3.0);
        QVERIFY(boundsMayBeWithin(QRectF{0, 0, 0, 0}, QRectF{3, 4, 0, 0}, 5));
        QVERIFY(!boundsMayBeWithin(QRectF{0, 0, 0, 0}, QRectF{3, 4, 0, 0}, 4.9));

        const QPointF local{3, 5};
        const QPointF origin{100, 200};
        const auto world = footprintToBoard(local, origin, 90, BoardSide::Bottom);
        QVERIFY(distancePointToSegment(world, {95, 197}, {95, 197}) < 1e-9);
        const auto restored = boardToFootprint(world, origin, 90, BoardSide::Bottom);
        QVERIFY(distancePointToSegment(restored, local, local) < 1e-9);
        QCOMPARE(mapFootprintLayerToBoard(kTopCopper, BoardSide::Bottom), kBottomCopper);
    }

    void boardStorage() {
        const eda::FootprintResolver resolve = [](const QString&) {
            return Result<Footprint>{Status::success(), exampleFootprint()};
        };
        const QString sample = QFINDTESTDATA("../examples/board-demo.brd");
        QVERIFY(!sample.isEmpty());
        auto board = loadBoard(sample, resolve);
        QVERIFY2(board.status.ok, qPrintable(board.status.message));
        QCOMPARE(board.value->instances.size(), 2);
        QCOMPARE(board.value->tracks.size(), 2);
        QCOMPARE(board.value->vias.size(), 1);
        QCOMPARE(board.value->pours.size(), 1);
        QCOMPARE(board.value->nets.size(), 1);
        board.value->groups[0].name = QStringLiteral("Top\n");
        board.value->instances[0].refdes = QStringLiteral("模块.A");
        board.value->nets[0].pins[0].refdes = QStringLiteral("模块.A");
        QTemporaryDir temporary(QDir::currentPath() + "/board-case-XXXXXX");
        QVERIFY(temporary.isValid());
        const auto path = temporary.filePath("round-trip.brd");
        const auto written = saveBoard(path, *board.value, resolve);
        QVERIFY2(written.ok, qPrintable(written.message));
        auto invalid = *board.value;
        invalid.tracks[0].net = "missing";
        QVERIFY(!saveBoard(path, invalid, resolve).ok);
        const auto restored = loadBoard(path, resolve);
        QVERIFY2(restored.status.ok, qPrintable(restored.status.message));
        QCOMPARE(restored.value->groups[0].name, QStringLiteral("Top\n"));
        QCOMPARE(restored.value->nets[0].pins[0].refdes, QStringLiteral("模块.A"));
        const auto before = encodeBoard(*board.value, resolve);
        const auto after = encodeBoard(*restored.value, resolve);
        QVERIFY(before.status.ok);
        QVERIFY(after.status.ok);
        QCOMPARE(*before.value, *after.value);
        QVERIFY(!decodeBoard(*before.value + "# truncated " + '\xE4', resolve).status.ok);
    }
};
QTEST_APPLESS_MAIN(CoreTest)
#include "tst_core.moc"
