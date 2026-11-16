#include "app/c_services.h"
#include "app/document.h"
#include "app/demo_documents.h"
#include "app/local_services.h"
#include "app/main_window.h"
#include "app/panels.h"
#include "core/io.h"
#include "feat_fp/basic_commands.h"
#include "feat_pcb/board_commands.h"
#include "render/board_tools.h"
#include "render/canvas.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QMessageBox>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeView>
#include <QtTest>

#include <algorithm>

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

    void editorModeTracksActions() {
        QTemporaryDir directory(QDir::currentPath() + "/b-mode-XXXXXX");
        QVERIFY(directory.isValid());
        LocalLibraryService library(directory.path());
        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
        MainWindow window(&library, &layers, &validation);

        auto* lineAction = window.findChild<QAction*>(QStringLiteral("lineModeAction"));
        auto* selectAction = window.findChild<QAction*>(QStringLiteral("selectModeAction"));
        QVERIFY(lineAction);
        QVERIFY(selectAction);
        lineAction->trigger();
        QCOMPARE(window.canvas()->editorMode(), CanvasWidget::EditorMode::AddSilkLine);
        QVERIFY(lineAction->isChecked());

        QTest::keyClick(window.canvas(), Qt::Key_Escape);
        QCOMPARE(window.canvas()->editorMode(), CanvasWidget::EditorMode::Select);
        QVERIFY(selectAction->isChecked());
    }

    void silkDrawingUsesSilkLayer() {
        Document document;
        document.installFootprint(eda::exampleFootprint());
        document.setActiveLayer(eda::kTopCopper);
        CanvasWidget canvas;
        canvas.resize(900, 620);
        canvas.setDocumentContext(&document);
        canvas.fitToView();
        canvas.setEditorMode(CanvasWidget::EditorMode::AddSilkLine);

        bool requested = false;
        eda::Footprint edited;
        connect(&canvas, &CanvasWidget::footprintEditRequested,
                [&](const eda::Footprint&, const eda::Footprint& after, const QString&) {
                    requested = true;
                    edited = after;
                });
        const QPoint start = (canvas.canvasViewport().center() - QPointF(80, 0)).toPoint();
        const QPoint end = (canvas.canvasViewport().center() + QPointF(80, 0)).toPoint();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&canvas, end, 10);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end);

        QVERIFY(requested);
        QVERIFY(!edited.silk.empty());
        QCOMPARE(edited.silk.back()->layer, eda::kTopSilk);
    }

    void rubberBandRespectsVisibilityAndGeometry() {
        eda::Board board = emptyBoardTemplate();
        eda::Track track;
        track.id = 1;
        track.layerId = eda::kTopCopper;
        track.width = 20.0;
        track.points = {QPointF(100, 100), QPointF(900, 100), QPointF(900, 900)};
        board.tracks.append(track);

        Document document;
        document.installBoard(board);
        CanvasWidget canvas;
        canvas.resize(900, 620);
        canvas.setDocumentContext(&document);
        canvas.fitToView();

        const auto hasTrack = [&]() {
            const auto selection = document.selection();
            return std::any_of(selection.begin(), selection.end(), [](const ObjectRef& ref) {
                return ref.type == ObjectType::Track && ref.objectId == 1;
            });
        };
        const auto rubberBand = [&](QPointF from, QPointF to) {
            const QPoint start = canvas.worldToScreen(from).toPoint();
            const QPoint end = canvas.worldToScreen(to).toPoint();
            QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(&canvas, end, 10);
            QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end);
        };

        document.setLayerVisible(eda::kTopCopper, false);
        rubberBand(QPointF(50, 150), QPointF(950, 50));
        QVERIFY(!hasTrack());

        document.setLayerVisible(eda::kTopCopper, true);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier,
                          canvas.worldToScreen(QPointF(450, 500)).toPoint());
        QVERIFY(document.selection().isEmpty());

        rubberBand(QPointF(50, 150), QPointF(950, 50));
        QVERIFY(hasTrack());
    }

    void boardToolsCommitAndUndo() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalLibraryService library(directory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
        MainWindow window(&library, &layers, &validation);
        auto* canvas = window.canvas();
        auto* document = qobject_cast<Document*>(canvas->documentContext());
        QVERIFY(document);
        document->installBoard(exampleBoard());
        document->setActiveLayer(eda::kTopCopper);

        auto* place = window.findChild<QAction*>(QStringLiteral("placeToolAction"));
        auto* route = window.findChild<QAction*>(QStringLiteral("routeToolAction"));
        auto* pour = window.findChild<QAction*>(QStringLiteral("pourToolAction"));
        QVERIFY(place && place->isEnabled() && route && route->isEnabled() && pour && pour->isEnabled());
        const auto initial = document->boardData();
        place->trigger();
        QVERIFY(dynamic_cast<PlaceTool*>(canvas->tool()));
        canvas->tool()->mousePress({QPointF(150, 150), Qt::LeftButton});
        QVERIFY(document->boardData().instances.front().pos != initial.instances.front().pos);
        document->undo();
        QCOMPARE(document->boardData().instances.front().pos, initial.instances.front().pos);

        route->trigger();
        auto* routeTool = dynamic_cast<RouteTool*>(canvas->tool());
        QVERIFY(routeTool);
        QCOMPARE(routeTool->orthoMode(), eda::OrthoMode::HV);
        canvas->tool()->mousePress({QPointF(300, 300), Qt::LeftButton});
        canvas->tool()->mousePress({QPointF(700, 450), Qt::LeftButton});
        QCOMPARE(document->boardData().tracks.size(), initial.tracks.size());
        routeTool->placeVia();
        QCOMPARE(document->boardData().vias.size(), initial.vias.size());
        canvas->tool()->mousePress({QPointF(900, 600), Qt::LeftButton});
        routeTool->finishRoute();
        QCOMPARE(document->boardData().tracks.size(), initial.tracks.size() + 2);
        QCOMPARE(document->boardData().vias.size(), initial.vias.size() + 1);
        QCOMPARE(document->boardData().tracks[document->boardData().tracks.size() - 2].points,
                 (QVector<QPointF>{{300, 300}, {700, 300}, {700, 450}}));
        QCOMPARE(document->boardData().tracks.back().layerId, eda::kBottomCopper);
        QCOMPARE(document->boardData().vias.back().fromLayer, eda::kTopCopper);
        QCOMPARE(document->boardData().vias.back().toLayer, eda::kBottomCopper);
        document->undo();
        QCOMPARE(document->boardData().tracks.size(), initial.tracks.size());
        QCOMPARE(document->boardData().vias.size(), initial.vias.size());

        pour->trigger();
        QVERIFY(dynamic_cast<PourTool*>(canvas->tool()));
        canvas->tool()->mousePress({QPointF(300, 300), Qt::LeftButton});
        canvas->tool()->mousePress({QPointF(500, 300), Qt::LeftButton});
        canvas->tool()->mousePress({QPointF(500, 500), Qt::LeftButton});
        canvas->tool()->mousePress({QPointF(500, 500), Qt::RightButton});
        QCOMPARE(document->boardData().pours.size(), initial.pours.size() + 1);
        document->undo();
        QCOMPARE(document->boardData().pours.size(), initial.pours.size());
        document->installFootprint(eda::exampleFootprint());
        QVERIFY(!canvas->tool());
        QVERIFY(!place->isEnabled() && !route->isEnabled() && !pour->isEnabled());
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
        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
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
        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
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
        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
        QCOMPARE(layers.model()->rowCount(), 6);

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
        QTemporaryDir directory(QDir::currentPath() + "/b-validation-XXXXXX");
        QVERIFY(directory.isValid());
        LocalLibraryService library(directory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
        const auto exampleIssues = validation.validateFootprint(eda::exampleFootprint());
        QVERIFY(std::none_of(exampleIssues.begin(), exampleIssues.end(), [](const ValidationIssue& issue) {
            return issue.severity == ValidationSeverity::Error;
        }));
        auto invalid = eda::exampleFootprint();
        invalid.id.clear();
        const auto issues = validation.validateFootprint(invalid);
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().severity, ValidationSeverity::Error);
        QVERIFY(!issues.first().message.isEmpty());

        auto manufacturing = eda::exampleFootprint();
        manufacturing.pads[0].drill = 3.0;
        const auto manufacturingIssues = validation.validateFootprint(manufacturing);
        QVERIFY(std::any_of(manufacturingIssues.begin(), manufacturingIssues.end(),
                            [&](const ValidationIssue& issue) {
                                return issue.severity == ValidationSeverity::Error &&
                                       issue.padId == manufacturing.pads[0].id;
                            }));

        auto board = exampleBoard();
        const auto boardIssues = validation.validateBoard(board);
        QVERIFY(std::none_of(boardIssues.begin(), boardIssues.end(), [](const ValidationIssue& issue) {
            return issue.severity == ValidationSeverity::Error;
        }));
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
        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
        MainWindow window(&library, &layers, &validation);
        QVERIFY(window.loadExampleBoard());
        QCOMPARE(window.document()->mode(), Document::Mode::Board);
        QCOMPARE(window.document()->boardData().instances.size(), 2);

        auto* layerView = window.findChild<QTreeView*>(QStringLiteral("layerList"));
        QVERIFY(layerView);
        QCOMPARE(layerView->model()->rowCount(), 3);
        QCOMPARE(layerView->model()->rowCount(layerView->model()->index(0, 0)), 3);
        QCOMPARE(layerView->model()->rowCount(layerView->model()->index(1, 0)), 3);
        QCOMPARE(layerView->model()->rowCount(layerView->model()->index(2, 0)), 1);

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

    void netlistImportBuildsBoard() {
        QTemporaryDir temporaryDirectory(QDir::currentPath() + "/b-netlist-XXXXXX");
        QVERIFY(temporaryDirectory.isValid());
        LocalLibraryService library(temporaryDirectory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
        MainWindow window(&library, &layers, &validation);

        const QString netlistPath = temporaryDirectory.filePath(QStringLiteral("example.net"));
        QFile netlist(netlistPath);
        QVERIFY(netlist.open(QIODevice::WriteOnly | QIODevice::Text));
        netlist.write("COMP U1 example\nCOMP U2 example\nNET N001 U1.1 U2.2\n");
        netlist.close();

        QVERIFY(window.importNetlistFile(netlistPath));
        QCOMPARE(window.document()->mode(), Document::Mode::Board);
        QCOMPARE(window.document()->boardData().instances.size(), 2);
        QCOMPARE(window.document()->boardData().nets.size(), 1);
        QCOMPARE(window.document()->boardData().nets.first().pins.size(), 2);

        QVERIFY(window.importNetlistFile(netlistPath));
        QCOMPARE(window.document()->boardData().instances.size(), 2);
        QCOMPARE(window.document()->boardData().nets.first().pins.size(), 2);
    }

    void routeModesProduceStrictOrthogonalPaths() {
        const QPointF start(100, 100);
        const QPointF end(700, 460);
        for (eda::OrthoMode mode : {eda::OrthoMode::HV, eda::OrthoMode::VH, eda::OrthoMode::Z_H,
                                    eda::OrthoMode::Z_V}) {
            Document document;
            document.installBoard(emptyBoardTemplate());
            document.setActiveLayer(eda::kTopCopper);
            RouteTool tool(mode);
            tool.activate(&document);
            tool.mousePress({start, Qt::LeftButton});
            tool.mousePress({end, Qt::LeftButton});
            tool.finishRoute();

            QCOMPARE(document.boardData().tracks.size(), 1);
            const auto actual = document.boardData().tracks.first().points;
            const auto expected = eda::orthogonalPath(start, end, mode);
            QCOMPARE(actual, expected);
            for (qsizetype i = 1; i < actual.size(); ++i) {
                const QPointF delta = actual[i] - actual[i - 1];
                QVERIFY(qFuzzyIsNull(delta.x()) || qFuzzyIsNull(delta.y()));
            }
        }
    }

    void layerTreeSupportsGroupAndLayerEditing() {
        QTemporaryDir directory(QDir::currentPath() + "/b-layer-XXXXXX");
        QVERIFY(directory.isValid());
        LocalLibraryService library(directory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
        MainWindow window(&library, &layers, &validation);
        QVERIFY(window.loadExampleBoard());
        QTimer closer;
        connect(&closer, &QTimer::timeout, [] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (auto* box = qobject_cast<QMessageBox*>(widget))
                    box->reject();
            }
        });
        closer.start(10);

        auto* panel = window.findChild<LayerPanel*>(QStringLiteral("layerPanel"));
        auto* view = window.findChild<QTreeView*>(QStringLiteral("layerList"));
        QVERIFY(panel);
        QVERIFY(view);
        QCOMPARE(view->model()->rowCount(), 3);
        QVERIFY(QMetaObject::invokeMethod(panel, "addGroupRequested", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("Inner")), Q_ARG(int, 3)));
        QCOMPARE(window.document()->boardData().groups.size(), 4);
        QVERIFY(window.document()->canUndo());
        QCOMPARE(view->model()->rowCount(), 4);
        window.document()->undo();
        QCOMPARE(window.document()->boardData().groups.size(), 3);
        window.document()->redo();

        auto* groupView = window.findChild<QTreeView*>(QStringLiteral("layerList"));
        QVERIFY(groupView);
        QCOMPARE(groupView->model()->rowCount(), 4);
        eda::LayerGroupId innerId = 0;
        for (const eda::LayerGroup& group : window.document()->boardData().groups) {
            if (group.name == QStringLiteral("Inner"))
                innerId = group.id;
        }
        QVERIFY(innerId > 0);
        const int beforeLayers = window.document()->boardData().layers.size();
        QVERIFY(QMetaObject::invokeMethod(panel, "addLayerRequested", Qt::DirectConnection,
                                          Q_ARG(int, innerId), Q_ARG(QString, QStringLiteral("Inner Copper")),
                                          Q_ARG(int, static_cast<int>(eda::BoardLayerType::Copper)),
                                          Q_ARG(QColor, QColor(QStringLiteral("#3C8D8A"))), Q_ARG(int, 0)));
        QCOMPARE(window.document()->boardData().layers.size(), beforeLayers + 1);
        const eda::LayerId innerLayer = window.document()->boardData().layers.back().id;
        QVERIFY(QMetaObject::invokeMethod(panel, "editLayerRequested", Qt::DirectConnection,
                                          Q_ARG(int, innerLayer), Q_ARG(QString, QStringLiteral("Inner Signal")),
                                          Q_ARG(QColor, QColor(QStringLiteral("#8A5B3C"))), Q_ARG(int, 1)));
        QCOMPARE(window.document()->boardData().layers.back().name, QStringLiteral("Inner Signal"));
        QCOMPARE(window.document()->boardData().layers.back().stackOrder, 1);
        window.document()->undo();
        QCOMPARE(window.document()->boardData().layers.size(), beforeLayers + 1);
        window.document()->undo();
        QCOMPARE(window.document()->boardData().layers.size(), beforeLayers);

        QVERIFY(QMetaObject::invokeMethod(panel, "editGroupRequested", Qt::DirectConnection,
                                          Q_ARG(int, innerId), Q_ARG(QString, QStringLiteral("Inner Edited")),
                                          Q_ARG(int, 4)));
        const auto editedGroup =
            std::find_if(window.document()->boardData().groups.begin(),
                         window.document()->boardData().groups.end(),
                         [&](const eda::LayerGroup& group) { return group.id == innerId; });
        QVERIFY(editedGroup != window.document()->boardData().groups.end());
        QCOMPARE(editedGroup->name, QStringLiteral("Inner Edited"));
        QCOMPARE(editedGroup->stackOrder, 4);
        QVERIFY(QMetaObject::invokeMethod(panel, "removeGroupRequested", Qt::DirectConnection,
                                          Q_ARG(int, innerId)));
        QCOMPARE(window.document()->boardData().groups.size(), 3);
        closer.stop();
    }

    void drcAndConnectivityPanelsMapObjects() {
        QTemporaryDir directory(QDir::currentPath() + "/b-analysis-XXXXXX");
        QVERIFY(directory.isValid());
        LocalLibraryService library(directory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
        MainWindow window(&library, &layers, &validation);
        QVERIFY(window.loadExampleBoard());

        const auto before = window.document()->boardData();
        auto after = before;
        eda::Track shortingTrack;
        shortingTrack.id = after.allocateTrackId();
        shortingTrack.layerId = eda::kTopCopper;
        shortingTrack.width = 12;
        shortingTrack.net = QStringLiteral("GND");
        shortingTrack.points = {QPointF(600, 520), QPointF(900, 520)};
        QVERIFY(after.insertTrack(shortingTrack).ok);
        QVERIFY(window.document()
                    ->pushCommand(new ReplaceBoardCommand(window.document(), before, after, QStringLiteral("构造短路")))
                    .ok);

        QVERIFY(!window.runDrcCheck());
        auto* list = window.findChild<QTreeWidget*>(QStringLiteral("analysisList"));
        QVERIFY(list);
        QCOMPARE(list->topLevelItemCount(), 2);
        auto* violations = list->topLevelItem(0);
        QVERIFY(violations);
        QVERIFY(violations->childCount() >= 1);
        QVERIFY(violations->child(0)->data(0, Qt::UserRole + 100).toInt() >
                static_cast<int>(ObjectType::None));

        QVERIFY(!window.runConnectivityCheck());
        auto* shorts = list->topLevelItem(1);
        QVERIFY(shorts);
        QVERIFY(shorts->text(0).contains(QStringLiteral("短路")));
        QVERIFY(shorts->childCount() >= 1);
    }

    void netlistImportRejectsIncompatibleFootprintReplacement() {
        QTemporaryDir temporaryDirectory(QDir::currentPath() + "/b-netlist-invalid-XXXXXX");
        QVERIFY(temporaryDirectory.isValid());
        LocalLibraryService library(temporaryDirectory.path());
        QVERIFY(seedExampleIfEmpty(library).ok);
        auto singlePad = eda::exampleFootprint();
        singlePad.id = QStringLiteral("single");
        singlePad.name = QStringLiteral("Single pad");
        singlePad.pads = {singlePad.pads.first()};
        singlePad.pins = {singlePad.pins.first()};
        singlePad.holes.clear();
        singlePad.silk.clear();
        QVERIFY(library.save(singlePad).ok);
        QVERIFY(library.refresh().ok);

        LayerModelService layers;
        ManufacturingValidationService validation(
            [&](const QString& id) { return library.load(id); });
        MainWindow window(&library, &layers, &validation);
        QVERIFY(window.loadExampleBoard());
        QCOMPARE(window.document()->boardData().instances.first().footprintId, QStringLiteral("example"));

        const QString netlistPath = temporaryDirectory.filePath(QStringLiteral("invalid.net"));
        QFile netlist(netlistPath);
        QVERIFY(netlist.open(QIODevice::WriteOnly | QIODevice::Text));
        netlist.write("COMP U1 single\nNET NNEW U1.1\n");
        netlist.close();

        QTimer closer;
        connect(&closer, &QTimer::timeout, [] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (auto* box = qobject_cast<QMessageBox*>(widget))
                    box->reject();
            }
        });
        closer.start(10);
        QVERIFY(!window.importNetlistFile(netlistPath));
        closer.stop();
        QCOMPARE(window.document()->boardData().instances.first().footprintId, QStringLiteral("example"));
        QVERIFY(!window.document()->canUndo());
    }
};

QTEST_MAIN(GuiTest)
#include "tst_b.moc"
