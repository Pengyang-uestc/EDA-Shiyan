#pragma once

#include "app/document.h"
#include "feat_pcb/orthogonal_router.h"

#include <QMainWindow>

#include <functional>

class QAction;
class QActionGroup;
class QCloseEvent;
class QDockWidget;
class QLabel;
class ILayerService;
class ILibraryService;
class IValidationService;
class CanvasWidget;
class AnalysisPanel;
class LayerPanel;
class LibraryPanel;
class PropertyPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    MainWindow(ILibraryService* libraryService, ILayerService* layerService, IValidationService* validationService,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    Document* document();
    CanvasWidget* canvas();
    bool loadExampleFootprint();
    bool loadExampleBoard();
    bool importNetlistFile(const QString& path);
    bool runDrcCheck();
    bool runConnectivityCheck();
    bool captureWorkflowScreenshots(const QString& directory);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void createActions();
    void createToolBar();
    void createDockPanels();
    void createStatusBar();
    void createMenus();
    void connectSignals();
    void applyStyle();

    void newFootprint();
    void openFootprint();
    void openBoardFile(const QString& path);
    void importNetlist();
    void saveFootprint();
    void saveFootprintAs();
    void saveBoardFile(const QString& path);
    bool maybeSave();
    void refreshLibrary();
    void openLibraryFootprint(const QString& id);
    void importFootprint();
    void deleteLibraryFootprint(const QString& id);
    bool validateBeforeSave();
    void applyEdit(const eda::Footprint& before, const eda::Footprint& after, const QString& text);
    void applyBoardEdit(const eda::Board& before, const eda::Board& after, const QString& text);
    bool applyLayerChange(const QString& text, const std::function<eda::Status(eda::Board&)>& action);
    void addLayerGroup(const QString& name, int stackOrder);
    void removeLayerGroup(eda::LayerGroupId id);
    void editLayerGroup(eda::LayerGroupId id, const QString& name, int stackOrder);
    void addBoardLayer(eda::LayerGroupId groupId, const QString& name, int type, const QColor& color,
                       int stackOrder);
    void removeBoardLayer(eda::LayerId id);
    void editBoardLayer(eda::LayerId id, const QString& name, const QColor& color, int stackOrder);
    void applyPadEdit(const eda::Pad& pad);
    void deleteSelection();
    void deleteBoardSelection();
    void addPad();
    void addPin();
    void addHole();
    void addSilkArc();
    void addSilkText(const QPointF& position);
    void setEditorMode(int mode);
    void updateTitle();
    void updateSelectionLabel();
    void updateDocumentModeActions();
    void selectAnalysisObject(const ObjectRef& ref);

    Document m_document;
    ILibraryService* m_libraryService = nullptr;
    ILayerService* m_layerService = nullptr;
    IValidationService* m_validationService = nullptr;
    CanvasWidget* m_canvas = nullptr;
    LibraryPanel* m_libraryPanel = nullptr;
    PropertyPanel* m_propertyPanel = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    AnalysisPanel* m_analysisPanel = nullptr;
    QDockWidget* m_libraryDock = nullptr;
    QDockWidget* m_propertyDock = nullptr;
    QDockWidget* m_layerDock = nullptr;
    QDockWidget* m_analysisDock = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QActionGroup* m_routeModeGroup = nullptr;
    QAction* m_selectModeAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_snapAction = nullptr;
    QAction* m_drcAction = nullptr;
    QAction* m_connectivityAction = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QVector<QAction*> m_footprintOnlyActions;
    QVector<QAction*> m_boardOnlyActions;
    QVector<QAction*> m_routeModeActions;
    eda::OrthoMode m_routeMode = eda::OrthoMode::HV;
};
