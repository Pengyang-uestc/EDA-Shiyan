#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "core/io.h"
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
        invalidUtf8.append(char(0xff));
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
};
QTEST_APPLESS_MAIN(CoreTest)
#include "tst_core.moc"
