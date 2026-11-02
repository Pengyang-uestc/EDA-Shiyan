#pragma once

#include "app/document.h"

#include <QMainWindow>

class QAction;
class QActionGroup;
class QCloseEvent;
class QDockWidget;
class QLabel;
class ILayerService;
class ILibraryService;
class IValidationService;
class CanvasWidget;
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

    Document m_document;
    ILibraryService* m_libraryService = nullptr;
    ILayerService* m_layerService = nullptr;
    IValidationService* m_validationService = nullptr;
    CanvasWidget* m_canvas = nullptr;
    LibraryPanel* m_libraryPanel = nullptr;
    PropertyPanel* m_propertyPanel = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    QDockWidget* m_libraryDock = nullptr;
    QDockWidget* m_propertyDock = nullptr;
    QDockWidget* m_layerDock = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QAction* m_selectModeAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_snapAction = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QVector<QAction*> m_footprintOnlyActions;
};
