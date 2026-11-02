#include "app/document.h"
#include "app/demo_documents.h"
#include "app/local_services.h"
#include "app/main_window.h"
#include "app/panels.h"
#include "core/io.h"
#include "feat_fp/basic_commands.h"
#include "feat_pcb/board_commands.h"
#include "render/canvas.h"

#include <QAction>
#include <QImage>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>

class GuiTest : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void commandUndoRedo() {
        Document document;
        document.installFootprint(eda::exampleFootprint());
        const auto before = document.footprintData();
        auto after = before;
        after.pads[0].pos += QPointF(100, 50);
        const auto pushed =
            document.pushCommand(new ReplaceFootprintCommand(&document, before, after, QStringLiteral("移动")));
        QVERIFY(pushed.ok);
        QCOMPARE(document.footprintData().pads[0].pos, QPointF(100, 50));
        QVERIFY(document.canUndo());
        document.undo();
        QCOMPARE(document.footprintData().pads[0].pos, before.pads[0].pos);
        document.redo();
        QCOMPARE(document.footprintData().pads[0].pos, QPointF(100, 50));
    }

    void snappingAndSessions() {
        Document document;
        document.setGridMil(50);
        document.setSnapEnabled(true);
        QCOMPARE(document.snap(QPointF(74, -26)), QPointF(50, -50));
        document.setSelection({ObjectRef{ObjectType::Pad, 99, {}, 0}});
        document.notifyModelChanged();
        QVERIFY(document.selection().isEmpty());
    }

    void canvasRoundTripAndHitTest() {
        Document document;
        document.installFootprint(eda::exampleFootprint());
        CanvasWidget canvas;
        canvas.resize(900, 620);
        canvas.setDocumentContext(&document);
        canvas.fitToView();

        const QPointF world(125.0, -75.0);
        const QPointF screen = canvas.worldToScreen(world);
        const QPointF restored = canvas.screenToWorld(screen);
        QVERIFY(QLineF(restored, world).length() < 0.001);

        const QPointF padCenter = canvas.worldToScreen(document.footprintData().pads.first().pos);
        QCOMPARE(canvas.padAtScreen(padCenter), document.footprintData().pads.first().id);
        QCOMPARE(canvas.padAtScreen(QPointF(2, 2)), eda::kInvalidId);
    }

    void mainWindowHasBWorkflow() {
        QTemporaryDir directory(QDir::currentPath() + "/b-services-XXXXXX");
        QVERIFY(directory.isValid());
        LocalLibraryService library(directory.path());
        LocalLayerService layers;
        StructuralValidationService validation;
        MainWindow window(&library, &layers, &validation);
        QVERIFY(window.canvas());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("newAction")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("openAction")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("saveAction")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("addPadAction")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("undoAction")));
        QVERIFY(window.findChild<QWidget*>(QStringLiteral("libraryTree")));
        QVERIFY(window.findChild<QWidget*>(QStringLiteral("layerList")));
        QVERIFY(window.findChild<QWidget*>(QStringLiteral("applyPadProperties")));
    }

    void screenshotsAreNonBlank() {
        QTemporaryDir temporaryDirectory(QDir::currentPath() + "/b-shot-XXXXXX");
        QVERIFY(temporaryDirectory.isValid());
        LocalLibraryService library(temporaryDirectory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        LocalLayerService layers;
        StructuralValidationService validation;
        MainWindow window(&library, &layers, &validation);
        window.resize(1280, 800);
        window.show();
        QTest::qWait(120);
        const QImage image = window.grab().toImage();
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), QSize(1280, 800));
        QSet<QRgb> sampledColors;
        for (int y = 4; y < image.height(); y += 32) {
            for (int x = 4; x < image.width(); x += 32)
                sampledColors.insert(image.pixel(x, y));
        }
        QVERIFY(sampledColors.size() > 8);
        const QString path = temporaryDirectory.filePath(QStringLiteral("b-window.png"));
        QVERIFY(image.save(path));

        const QImage loaded(path);
        QVERIFY(!loaded.isNull());
        QVERIFY(loaded.size().width() >= 1000);
    }

    void servicesDrivePanels() {
        QTemporaryDir temporaryDirectory(QDir::currentPath() + "/b-panel-services-XXXXXX");
        QVERIFY(temporaryDirectory.isValid());
        LocalLibraryService library(temporaryDirectory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        LocalLayerService layers;
        StructuralValidationService validation;

        MainWindow window(&library, &layers, &validation);
        auto* libraryPanel = window.findChild<LibraryPanel*>(QStringLiteral("libraryPanel"));
        auto* layerPanel = window.findChild<LayerPanel*>(QStringLiteral("layerPanel"));
        QVERIFY(libraryPanel);
        QVERIFY(layerPanel);
        QCOMPARE(libraryPanel->service(), static_cast<ILibraryService*>(&library));
        QCOMPARE(layerPanel->service(), static_cast<ILayerService*>(&layers));

        auto* libraryView = window.findChild<QTreeView*>(QStringLiteral("libraryTree"));
        auto* layerView = window.findChild<QTreeView*>(QStringLiteral("layerList"));
        QVERIFY(libraryView);
        QVERIFY(layerView);
        QVERIFY(libraryView->model()->rowCount() >= 1);
        QCOMPARE(layerView->model()->rowCount(), 6);
    }

    void validationServiceReportsIssues() {
        StructuralValidationService validation;
        QVERIFY(validation.validateFootprint(eda::exampleFootprint()).isEmpty());
        auto invalid = eda::exampleFootprint();
        invalid.id.clear();
        const auto issues = validation.validateFootprint(invalid);
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().severity, ValidationSeverity::Error);
        QVERIFY(!issues.first().message.isEmpty());

        auto board = exampleBoard();
        QVERIFY(validation.validateBoard(board).isEmpty());
        board.outline.clear();
        QVERIFY(!validation.validateBoard(board).isEmpty());
    }

    void boardSnapshotUndo() {
        Document document;
        document.installBoard(exampleBoard());
        const auto before = document.boardData();
        auto after = before;
        after.instances[0].pos += QPointF(120, 60);
        const auto pushed =
            document.pushCommand(new ReplaceBoardCommand(&document, before, after, QStringLiteral("移动实例")));
        QVERIFY(pushed.ok);
        QCOMPARE(document.boardData().instances[0].pos, before.instances[0].pos + QPointF(120, 60));
        document.undo();
        QCOMPARE(document.boardData().instances[0].pos, before.instances[0].pos);
        document.redo();
        QCOMPARE(document.boardData().instances[0].pos, before.instances[0].pos + QPointF(120, 60));
    }

    void boardLayerServiceAndHitTest() {
        QTemporaryDir temporaryDirectory(QDir::currentPath() + "/b-board-XXXXXX");
        QVERIFY(temporaryDirectory.isValid());
        LocalLibraryService library(temporaryDirectory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        LocalLayerService layers;
        StructuralValidationService validation;
        MainWindow window(&library, &layers, &validation);
        QVERIFY(window.loadExampleBoard());
        QCOMPARE(window.document()->mode(), Document::Mode::Board);
        QCOMPARE(window.document()->boardData().instances.size(), 2);

        auto* layerView = window.findChild<QTreeView*>(QStringLiteral("layerList"));
        QVERIFY(layerView);
        QCOMPARE(layerView->model()->rowCount(), 7);

        CanvasWidget* canvas = window.canvas();
        canvas->resize(1000, 700);
        canvas->fitToView();
        const QPointF center = canvas->worldToScreen(window.document()->boardData().instances.first().pos);
        const ObjectRef hit = canvas->boardObjectAtScreen(center);
        QCOMPARE(hit.type, ObjectType::Instance);
        QCOMPARE(hit.refdes, QStringLiteral("U1"));

        const ObjectRef trackHit =
            canvas->boardObjectAtScreen(canvas->worldToScreen(window.document()->boardData().tracks[0].points[2]));
        QCOMPARE(trackHit.type, ObjectType::Track);
        QCOMPARE(trackHit.objectId, quint64(1));

        const ObjectRef pourHit = canvas->boardObjectAtScreen(canvas->worldToScreen(QPointF(300, 300)));
        QCOMPARE(pourHit.type, ObjectType::Pour);
        QCOMPARE(pourHit.objectId, quint64(1));
    }

    void boardFileRoundTrip() {
        QTemporaryDir temporaryDirectory(QDir::currentPath() + "/b-board-file-XXXXXX");
        QVERIFY(temporaryDirectory.isValid());
        LocalLibraryService library(temporaryDirectory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        const auto resolver = [&](const QString& id) { return library.load(id); };
        const eda::Board board = exampleBoard();

        const auto encoded = eda::encodeBoard(board, resolver);
        QVERIFY2(encoded.status.ok, qPrintable(encoded.status.message));
        const auto decoded = eda::decodeBoard(*encoded.value, resolver);
        QVERIFY2(decoded.status.ok, qPrintable(decoded.status.message));
        QCOMPARE(*eda::encodeBoard(*decoded.value, resolver).value, *encoded.value);

        const QString path = temporaryDirectory.filePath(QStringLiteral("example.brd"));
        QVERIFY(eda::saveBoard(path, board, resolver).ok);
        const auto loaded = eda::loadBoard(path, resolver);
        QVERIFY2(loaded.status.ok, qPrintable(loaded.status.message));
        QCOMPARE(*eda::encodeBoard(*loaded.value, resolver).value, *encoded.value);
    }
};

QTEST_MAIN(GuiTest)
#include "tst_b.moc"
