#include "app/main_window.h"

#include "app/demo_documents.h"
#include "app/panels.h"
#include "app/services.h"
#include "core/io.h"
#include "core/library.h"
#include "feat_fp/basic_commands.h"
#include "feat_pcb/board_commands.h"
#include "render/canvas.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QDoubleSpinBox* dialogLength(QWidget* parent, double value, double minimum, double maximum) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(3);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setSuffix(QObject::tr(" mil"));
    return spin;
}

QDoubleSpinBox* dialogAngle(QWidget* parent, double value) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(2);
    spin->setRange(-3600.0, 3600.0);
    spin->setValue(value);
    spin->setSuffix(QObject::tr("°"));
    return spin;
}

eda::Footprint blankFootprint() {
    eda::Footprint footprint;
    footprint.id = QStringLiteral("untitled");
    footprint.name = QObject::tr("未命名封装");
    return footprint;
}
} // namespace

MainWindow::MainWindow(ILibraryService* libraryService, ILayerService* layerService,
                       IValidationService* validationService, QWidget* parent)
    : QMainWindow(parent), m_libraryService(libraryService), m_layerService(layerService),
      m_validationService(validationService) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("EDA-Shiyan · 封装编辑器"));
    resize(1380, 860);
    setMinimumSize(1000, 640);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
    applyStyle();

    m_canvas = new CanvasWidget(this);
    m_canvas->setObjectName(QStringLiteral("mainCanvas"));
    setCentralWidget(m_canvas);
    m_canvas->setDocumentContext(&m_document);
    m_canvas->setFootprintResolver([this](const QString& id) {
        if (!m_libraryService)
            return eda::Result<eda::Footprint>{eda::Status::failure(tr("未注入元件库服务")), std::nullopt};
        return m_libraryService->load(id);
    });

    createActions();
    createToolBar();
    createDockPanels();
    createStatusBar();
    createMenus();
    connectSignals();

    refreshLibrary();
    if (m_layerPanel)
        m_layerPanel->refresh();
    loadExampleFootprint();
    updateTitle();
}

MainWindow::~MainWindow() = default;

Document* MainWindow::document() {
    return &m_document;
}

CanvasWidget* MainWindow::canvas() {
    return m_canvas;
}

bool MainWindow::loadExampleFootprint() {
    m_document.installFootprint(eda::exampleFootprint());
    setEditorMode(static_cast<int>(CanvasWidget::EditorMode::Select));
    m_selectModeAction->setChecked(true);
    QTimer::singleShot(0, m_canvas, [this] { m_canvas->fitToView(); });
    return true;
}

bool MainWindow::loadExampleBoard() {
    if (!m_libraryService) {
        QMessageBox::critical(this, tr("无法打开 PCB 示例"), tr("未注入元件库服务"));
        return false;
    }
    const auto existing = m_libraryService->load(QStringLiteral("example"));
    if (!existing.status.ok) {
        const auto saved = m_libraryService->save(eda::exampleFootprint());
        if (!saved.ok) {
            QMessageBox::critical(this, tr("无法打开 PCB 示例"), saved.message);
            return false;
        }
        m_libraryService->refresh();
    }
    m_document.installBoard(exampleBoard());
    setEditorMode(static_cast<int>(CanvasWidget::EditorMode::Select));
    m_selectModeAction->setChecked(true);
    QTimer::singleShot(0, m_canvas, [this] { m_canvas->fitToView(); });
    statusBar()->showMessage(tr("已打开 PCB 示例"), 3000);
    return true;
}

bool MainWindow::importNetlistFile(const QString& path) {
    if (!m_libraryService) {
        QMessageBox::critical(this, tr("网表导入失败"), tr("未注入元件库服务"));
        return false;
    }
    const auto netlist = eda::loadNetlist(path);
    if (!netlist.status.ok || !netlist.value.has_value()) {
        QMessageBox::critical(this, tr("网表导入失败"), netlist.status.message);
        return false;
    }
    const auto resolver = [this](const QString& id) { return m_libraryService->load(id); };
    const auto resolved = eda::resolveNetlistPins(*netlist.value, resolver);
    if (!resolved.status.ok || !resolved.value.has_value()) {
        QMessageBox::critical(this, tr("网表导入失败"), resolved.status.message);
        return false;
    }

    const bool replaceCurrent = m_document.mode() != Document::Mode::Board;
    eda::Board before = replaceCurrent ? emptyBoardTemplate() : m_document.boardData();
    eda::Board after = before;
    QSet<QString> existingRefdes;
    for (const auto& instance : after.instances)
        existingRefdes.insert(instance.refdes);

    const QRectF outlineBounds = after.outline.boundingRect();
    constexpr double margin = 280.0;
    constexpr double spacing = 360.0;
    const int columns =
        qMax(1, static_cast<int>(std::floor(qMax(1.0, outlineBounds.width() - margin * 2.0) / spacing)));
    int slot = static_cast<int>(after.instances.size());
    int added = 0;
    for (const auto& component : netlist.value->components) {
        auto existing = std::find_if(after.instances.begin(), after.instances.end(),
                                     [&](const eda::Instance& item) { return item.refdes == component.refdes; });
        if (existing != after.instances.end()) {
            existing->footprintId = component.footprintId;
            continue;
        }
        const int row = slot / columns;
        const int column = slot % columns;
        eda::Instance instance;
        instance.refdes = component.refdes;
        instance.footprintId = component.footprintId;
        instance.pos =
            QPointF(outlineBounds.left() + margin + column * spacing, outlineBounds.bottom() - margin - row * spacing);
        after.instances.append(instance);
        ++slot;
        ++added;
    }

    for (const auto& sourceNet : netlist.value->nets) {
        auto target = std::find_if(after.nets.begin(), after.nets.end(),
                                   [&](const eda::Net& net) { return net.name == sourceNet.name; });
        if (target == after.nets.end()) {
            after.nets.append(sourceNet);
            continue;
        }
        for (const auto& pin : sourceNet.pins) {
            const bool exists = std::any_of(target->pins.begin(), target->pins.end(), [&](const eda::PinRef& item) {
                return item.refdes == pin.refdes && item.pinNumber == pin.pinNumber;
            });
            if (!exists)
                target->pins.append(pin);
        }
    }

    const auto structure = eda::validateBoardStructure(after);
    if (!structure.ok) {
        QMessageBox::critical(this, tr("网表导入失败"), structure.message);
        return false;
    }

    if (replaceCurrent) {
        m_document.installBoard(after);
        setEditorMode(static_cast<int>(CanvasWidget::EditorMode::Select));
        m_selectModeAction->setChecked(true);
        m_canvas->fitToView();
    } else {
        const auto status = m_document.pushCommand(new ReplaceBoardCommand(&m_document, before, after, tr("导入网表")));
        if (!status.ok) {
            QMessageBox::critical(this, tr("网表导入失败"), status.message);
            return false;
        }
    }
    statusBar()->showMessage(tr("已导入 %1 个元件、%2 个网络，新建 %3 个实例")
                                 .arg(netlist.value->components.size())
                                 .arg(netlist.value->nets.size())
                                 .arg(added),
                             6000);
    return true;
}

void MainWindow::applyStyle() {
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            color: #26343B;
            font-family: "Segoe UI", "Microsoft YaHei UI";
            font-size: 12px;
        }
        QMainWindow { background: #E5E8E8; }
        QToolBar {
            background: #F4F6F6;
            border: none;
            border-bottom: 1px solid #C9CFD1;
            padding: 4px 8px;
            spacing: 3px;
        }
        QToolBar::separator {
            background: #C9CFD1;
            width: 1px;
            margin: 5px 7px;
        }
        QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 5px;
        }
        QToolButton:hover { background: #E2E7E8; border-color: #C9D0D2; }
        QToolButton:checked { background: #D9E7E8; border-color: #7EA4A8; }
        QToolButton:pressed { background: #CBD9DB; }
        QDockWidget {
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
            font-weight: 600;
        }
        QDockWidget::title {
            background: #EDF0F0;
            border-bottom: 1px solid #C9CFD1;
            padding: 7px 9px;
            text-align: left;
        }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
            background: #FFFFFF;
            border: 1px solid #BEC7CA;
            border-radius: 3px;
            min-height: 25px;
            padding: 1px 5px;
            selection-background-color: #2A6F79;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
            border-color: #2A6F79;
        }
        QPushButton {
            background: #2E5868;
            color: #FFFFFF;
            border: none;
            border-radius: 4px;
            min-height: 28px;
            padding: 2px 12px;
        }
        QPushButton:hover { background: #396C7E; }
        QPushButton:disabled { background: #BCC5C8; color: #F1F3F3; }
        QGroupBox {
            border: 1px solid #CFD5D6;
            border-radius: 4px;
            margin-top: 10px;
            padding-top: 8px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px;
        }
        QTreeWidget, QListWidget {
            background: #FBFCFB;
            border: 1px solid #D1D7D8;
            alternate-background-color: #F3F5F4;
            outline: none;
        }
        QTreeWidget::item, QListWidget::item { min-height: 25px; }
        QTreeWidget::item:selected, QListWidget::item:selected {
            background: #D8E7E8;
            color: #203A43;
        }
        QStatusBar {
            background: #EEF1F1;
            border-top: 1px solid #C9CFD1;
        }
        QStatusBar QLabel {
            color: #5C686E;
            padding: 0 8px;
        }
        QMenuBar { background: #F4F6F6; }
        QMenuBar::item { padding: 5px 9px; background: transparent; }
        QMenuBar::item:selected { background: #E0E5E6; }
        QMenu {
            background: #FBFCFB;
            border: 1px solid #C5CCCE;
            padding: 4px;
        }
        QMenu::item { padding: 6px 28px 6px 24px; }
        QMenu::item:selected { background: #D8E7E8; }
    )"));
}

void MainWindow::createActions() {
    m_undoAction = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("撤销"), this);
    m_undoAction->setObjectName(QStringLiteral("undoAction"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setEnabled(false);

    m_redoAction = new QAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("重做"), this);
    m_redoAction->setObjectName(QStringLiteral("redoAction"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setEnabled(false);

    m_saveAction = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("保存"), this);
    m_saveAction->setObjectName(QStringLiteral("saveAction"));
    m_saveAction->setShortcut(QKeySequence::Save);

    auto* newAction = new QAction(style()->standardIcon(QStyle::SP_FileIcon), tr("新建"), this);
    newAction->setObjectName(QStringLiteral("newAction"));
    newAction->setShortcut(QKeySequence::New);
    auto* openAction = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("打开"), this);
    openAction->setObjectName(QStringLiteral("openAction"));
    openAction->setShortcut(QKeySequence::Open);
    auto* saveAsAction = new QAction(tr("另存为"), this);
    saveAsAction->setObjectName(QStringLiteral("saveAsAction"));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    auto* exampleAction = new QAction(tr("打开示例封装"), this);
    exampleAction->setObjectName(QStringLiteral("exampleAction"));
    auto* exampleBoardAction = new QAction(tr("打开 PCB 示例"), this);
    exampleBoardAction->setObjectName(QStringLiteral("exampleBoardAction"));
    auto* importNetlistAction = new QAction(tr("导入网表"), this);
    importNetlistAction->setObjectName(QStringLiteral("importNetlistAction"));
    auto* exitAction = new QAction(tr("退出"), this);
    exitAction->setObjectName(QStringLiteral("exitAction"));
    exitAction->setShortcut(QKeySequence::Quit);

    connect(newAction, &QAction::triggered, this, &MainWindow::newFootprint);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFootprint);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveFootprint);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveFootprintAs);
    connect(exampleAction, &QAction::triggered, this, [this] {
        if (maybeSave())
            loadExampleFootprint();
    });
    connect(exampleBoardAction, &QAction::triggered, this, [this] {
        if (maybeSave())
            loadExampleBoard();
    });
    connect(importNetlistAction, &QAction::triggered, this, &MainWindow::importNetlist);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* deleteAction = new QAction(style()->standardIcon(QStyle::SP_TrashIcon), tr("删除选中对象"), this);
    deleteAction->setObjectName(QStringLiteral("deleteAction"));
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteSelection);

    m_selectModeAction = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("选择"), this);
    m_selectModeAction->setObjectName(QStringLiteral("selectModeAction"));
    m_selectModeAction->setCheckable(true);
    m_selectModeAction->setChecked(true);
    m_selectModeAction->setToolTip(tr("选择、框选和拖动焊盘"));

    auto* lineMode = new QAction(tr("丝印线"), this);
    lineMode->setObjectName(QStringLiteral("lineModeAction"));
    lineMode->setCheckable(true);
    lineMode->setToolTip(tr("拖动绘制丝印线"));
    auto* rectMode = new QAction(tr("丝印矩形"), this);
    rectMode->setObjectName(QStringLiteral("rectModeAction"));
    rectMode->setCheckable(true);
    rectMode->setToolTip(tr("拖动绘制丝印矩形"));
    auto* polygonMode = new QAction(tr("丝印多边形"), this);
    polygonMode->setObjectName(QStringLiteral("polygonModeAction"));
    polygonMode->setCheckable(true);
    polygonMode->setToolTip(tr("单击添加顶点，右键或双击完成"));

    m_modeGroup = new QActionGroup(this);
    m_modeGroup->setExclusive(true);
    m_modeGroup->addAction(m_selectModeAction);
    m_modeGroup->addAction(lineMode);
    m_modeGroup->addAction(rectMode);
    m_modeGroup->addAction(polygonMode);
    m_footprintOnlyActions << lineMode << rectMode << polygonMode;

    connect(m_selectModeAction, &QAction::triggered, this,
            [this] { setEditorMode(static_cast<int>(CanvasWidget::EditorMode::Select)); });
    connect(lineMode, &QAction::triggered, this,
            [this] { setEditorMode(static_cast<int>(CanvasWidget::EditorMode::AddSilkLine)); });
    connect(rectMode, &QAction::triggered, this,
            [this] { setEditorMode(static_cast<int>(CanvasWidget::EditorMode::AddSilkRectangle)); });
    connect(polygonMode, &QAction::triggered, this,
            [this] { setEditorMode(static_cast<int>(CanvasWidget::EditorMode::AddSilkPolygon)); });

    auto* addPadAction = new QAction(tr("添加焊盘"), this);
    addPadAction->setObjectName(QStringLiteral("addPadAction"));
    addPadAction->setShortcut(QKeySequence(Qt::Key_P));
    auto* addPinAction = new QAction(tr("添加引脚"), this);
    addPinAction->setObjectName(QStringLiteral("addPinAction"));
    auto* addHoleAction = new QAction(tr("添加安装孔"), this);
    addHoleAction->setObjectName(QStringLiteral("addHoleAction"));
    auto* addArcAction = new QAction(tr("添加丝印圆弧"), this);
    addArcAction->setObjectName(QStringLiteral("addArcAction"));
    auto* addTextAction = new QAction(tr("添加丝印文字"), this);
    addTextAction->setObjectName(QStringLiteral("addTextAction"));
    connect(addPadAction, &QAction::triggered, this, &MainWindow::addPad);
    connect(addPinAction, &QAction::triggered, this, &MainWindow::addPin);
    connect(addHoleAction, &QAction::triggered, this, &MainWindow::addHole);
    connect(addArcAction, &QAction::triggered, this, &MainWindow::addSilkArc);
    connect(addTextAction, &QAction::triggered, this,
            [this] { addSilkText(m_canvas->screenToWorld(m_canvas->canvasViewport().center())); });
    m_footprintOnlyActions << addPadAction << addPinAction << addHoleAction << addArcAction << addTextAction;

    auto* zoomInAction = new QAction(style()->standardIcon(QStyle::SP_ArrowUp), tr("放大"), this);
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    auto* zoomOutAction = new QAction(style()->standardIcon(QStyle::SP_ArrowDown), tr("缩小"), this);
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    auto* fitAction = new QAction(tr("适合窗口"), this);
    fitAction->setObjectName(QStringLiteral("fitAction"));
    fitAction->setShortcut(QKeySequence(Qt::Key_F));
    m_snapAction = new QAction(tr("网格吸附"), this);
    m_snapAction->setObjectName(QStringLiteral("snapAction"));
    m_snapAction->setCheckable(true);
    m_snapAction->setChecked(true);
    auto* rulerAction = new QAction(tr("显示标尺"), this);
    rulerAction->setCheckable(true);
    rulerAction->setChecked(true);
    connect(zoomInAction, &QAction::triggered, m_canvas, &CanvasWidget::zoomIn);
    connect(zoomOutAction, &QAction::triggered, m_canvas, &CanvasWidget::zoomOut);
    connect(fitAction, &QAction::triggered, m_canvas, &CanvasWidget::fitToView);
    connect(m_snapAction, &QAction::toggled, this, [this](bool checked) { m_document.setSnapEnabled(checked); });
    connect(rulerAction, &QAction::toggled, m_canvas, &CanvasWidget::setRulersVisible);

    auto* aboutAction = new QAction(tr("关于"), this);
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("关于 EDA-Shiyan"),
                           tr("EDA 软件设计 I\n实验一封装编辑器 · B 图形与交互模块\n\n"
                              "Qt 6 Widgets / QPainter / C++17"));
    });

    m_undoAction->setProperty("menuText", tr("撤销"));
    m_redoAction->setProperty("menuText", tr("重做"));
}

void MainWindow::createToolBar() {
    auto* toolbar = addToolBar(tr("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto findAction = [this](const QString& name) { return findChild<QAction*>(name); };
    toolbar->addAction(findAction(QStringLiteral("newAction")));
    toolbar->addAction(findAction(QStringLiteral("openAction")));
    toolbar->addAction(m_saveAction);
    toolbar->addSeparator();
    toolbar->addAction(m_undoAction);
    toolbar->addAction(m_redoAction);
    toolbar->addAction(findAction(QStringLiteral("deleteAction")));
    toolbar->addSeparator();
    toolbar->addAction(m_selectModeAction);
    toolbar->addAction(findAction(QStringLiteral("lineModeAction")));
    toolbar->addAction(findAction(QStringLiteral("rectModeAction")));
    toolbar->addAction(findAction(QStringLiteral("polygonModeAction")));
    toolbar->addSeparator();
    toolbar->addAction(findAction(QStringLiteral("addPadAction")));
    toolbar->addAction(findAction(QStringLiteral("addPinAction")));
    toolbar->addAction(findAction(QStringLiteral("addHoleAction")));
    toolbar->addSeparator();
    toolbar->addAction(findAction(QStringLiteral("fitAction")));
    toolbar->addAction(m_snapAction);

    for (auto* action : toolbar->actions()) {
        if (!action->icon().isNull())
            continue;
        if (auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(action)))
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
}

void MainWindow::createDockPanels() {
    m_libraryPanel = new LibraryPanel(this);
    m_libraryPanel->setObjectName(QStringLiteral("libraryPanel"));
    m_libraryDock = new QDockWidget(tr("元件库"), this);
    m_libraryDock->setObjectName(QStringLiteral("libraryDock"));
    m_libraryDock->setWidget(m_libraryPanel);
    m_libraryDock->setMinimumWidth(230);
    addDockWidget(Qt::LeftDockWidgetArea, m_libraryDock);

    m_propertyPanel = new PropertyPanel(this);
    m_propertyPanel->setObjectName(QStringLiteral("propertyPanel"));
    m_propertyDock = new QDockWidget(tr("属性"), this);
    m_propertyDock->setObjectName(QStringLiteral("propertyDock"));
    auto* propertyScroll = new QScrollArea(m_propertyDock);
    propertyScroll->setObjectName(QStringLiteral("propertyScrollArea"));
    propertyScroll->setWidgetResizable(true);
    propertyScroll->setFrameShape(QFrame::NoFrame);
    propertyScroll->setWidget(m_propertyPanel);
    m_propertyDock->setWidget(propertyScroll);
    m_propertyDock->setMinimumWidth(270);
    addDockWidget(Qt::RightDockWidgetArea, m_propertyDock);

    m_layerPanel = new LayerPanel(this);
    m_layerPanel->setObjectName(QStringLiteral("layerPanel"));
    m_layerDock = new QDockWidget(tr("层"), this);
    m_layerDock->setObjectName(QStringLiteral("layerDock"));
    m_layerDock->setWidget(m_layerPanel);
    m_layerDock->setMinimumWidth(240);
    addDockWidget(Qt::RightDockWidgetArea, m_layerDock);
    splitDockWidget(m_propertyDock, m_layerDock, Qt::Vertical);

    m_propertyPanel->setDocumentContext(&m_document);
    m_layerPanel->setDocumentContext(&m_document);
    m_libraryPanel->setService(m_libraryService);
    if (m_layerService)
        m_layerService->setDocumentContext(&m_document);
    m_layerPanel->setService(m_layerService);
}

void MainWindow::createStatusBar() {
    m_cursorLabel = new QLabel(tr("X 0.000   Y 0.000 mil"), this);
    m_cursorLabel->setObjectName(QStringLiteral("cursorLabel"));
    m_cursorLabel->setMinimumWidth(195);
    m_zoomLabel = new QLabel(tr("缩放 100%"), this);
    m_zoomLabel->setObjectName(QStringLiteral("zoomLabel"));
    m_zoomLabel->setMinimumWidth(95);
    m_selectionLabel = new QLabel(tr("未选择"), this);
    m_selectionLabel->setObjectName(QStringLiteral("selectionLabel"));
    m_selectionLabel->setMinimumWidth(80);
    statusBar()->addPermanentWidget(m_cursorLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
    statusBar()->addPermanentWidget(m_selectionLabel);
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("newAction")));
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("openAction")));
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("saveAsAction")));
    fileMenu->addSeparator();
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("exampleAction")));
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("exampleBoardAction")));
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("importNetlistAction")));
    fileMenu->addSeparator();
    fileMenu->addAction(findChild<QAction*>(QStringLiteral("exitAction")));

    auto* editMenu = menuBar()->addMenu(tr("编辑"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(findChild<QAction*>(QStringLiteral("deleteAction")));

    auto* insertMenu = menuBar()->addMenu(tr("插入"));
    insertMenu->addAction(findChild<QAction*>(QStringLiteral("addPadAction")));
    insertMenu->addAction(findChild<QAction*>(QStringLiteral("addPinAction")));
    insertMenu->addAction(findChild<QAction*>(QStringLiteral("addHoleAction")));
    insertMenu->addSeparator();
    insertMenu->addAction(findChild<QAction*>(QStringLiteral("lineModeAction")));
    insertMenu->addAction(findChild<QAction*>(QStringLiteral("rectModeAction")));
    insertMenu->addAction(findChild<QAction*>(QStringLiteral("polygonModeAction")));
    insertMenu->addAction(findChild<QAction*>(QStringLiteral("addArcAction")));
    insertMenu->addAction(findChild<QAction*>(QStringLiteral("addTextAction")));

    auto* viewMenu = menuBar()->addMenu(tr("视图"));
    viewMenu->addAction(m_libraryDock->toggleViewAction());
    viewMenu->addAction(m_propertyDock->toggleViewAction());
    viewMenu->addAction(m_layerDock->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(findChild<QAction*>(QStringLiteral("fitAction")));
    viewMenu->addAction(m_snapAction);

    auto* helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(tr("关于"), this, [this] {
        QMessageBox::about(this, tr("关于 EDA-Shiyan"), tr("EDA 软件设计 I\n实验一封装编辑器 · B 图形与交互模块"));
    });
}

void MainWindow::connectSignals() {
    connect(m_libraryPanel, &LibraryPanel::openRequested, this, &MainWindow::openLibraryFootprint);
    connect(m_libraryPanel, &LibraryPanel::importRequested, this, &MainWindow::importFootprint);
    connect(m_libraryPanel, &LibraryPanel::deleteRequested, this, &MainWindow::deleteLibraryFootprint);
    connect(m_libraryPanel, &LibraryPanel::statusMessageRequested, this,
            [this](const QString& message) { statusBar()->showMessage(message, 5000); });
    connect(m_layerPanel, &LayerPanel::statusMessageRequested, this,
            [this](const QString& message) { statusBar()->showMessage(message, 5000); });
    connect(m_propertyPanel, &PropertyPanel::padEditRequested, this, &MainWindow::applyPadEdit);
    connect(m_layerPanel, &LayerPanel::activeLayerRequested, &m_document, &Document::setActiveLayer);
    connect(m_layerPanel, &LayerPanel::layerVisibilityRequested, &m_document, &Document::setLayerVisible);
    connect(m_canvas, &CanvasWidget::footprintEditRequested, this, &MainWindow::applyEdit);
    connect(m_canvas, &CanvasWidget::boardEditRequested, this, &MainWindow::applyBoardEdit);
    connect(m_canvas, &CanvasWidget::textPlacementRequested, this, &MainWindow::addSilkText);
    connect(m_canvas, &CanvasWidget::deleteSelectionRequested, this, &MainWindow::deleteSelection);
    connect(m_canvas, &CanvasWidget::cursorWorldPositionChanged, this, [this](const QPointF& point) {
        m_cursorLabel->setText(tr("X %1   Y %2 mil").arg(point.x(), 0, 'f', 3).arg(point.y(), 0, 'f', 3));
    });
    connect(m_canvas, &CanvasWidget::zoomChanged, this, [this](double scale) {
        m_zoomLabel->setText(tr("缩放 %1%").arg(static_cast<int>(std::round(scale * 100.0 / 2.4))));
    });
    connect(m_canvas, &CanvasWidget::selectionCountChanged, this, [this](int) { updateSelectionLabel(); });
    connect(m_undoAction, &QAction::triggered, &m_document, &Document::undo);
    connect(m_redoAction, &QAction::triggered, &m_document, &Document::redo);
    connect(&m_document, &Document::undoAvailabilityChanged, this, [this](bool undo, bool redo) {
        m_undoAction->setEnabled(undo);
        m_redoAction->setEnabled(redo);
    });
    connect(&m_document, &Document::selectionChanged, this, &MainWindow::updateSelectionLabel);
    connect(&m_document, &Document::modelChanged, this, &MainWindow::updateTitle);
    connect(&m_document, &Document::documentPathChanged, this, [this](const QString&) { updateTitle(); });
    connect(&m_document, &Document::cleanChanged, this, [this](bool) { updateTitle(); });
    connect(&m_document, &Document::documentReset, this, [this] {
        updateDocumentModeActions();
        if (m_layerPanel)
            m_layerPanel->refresh();
    });
    connect(&m_document, &DocumentContext::statusMessageRequested, this,
            [this](const QString& text) { statusBar()->showMessage(text, 5000); });
    updateSelectionLabel();
    updateDocumentModeActions();
}

void MainWindow::newFootprint() {
    if (!maybeSave())
        return;
    m_document.installFootprint(blankFootprint());
    setEditorMode(static_cast<int>(CanvasWidget::EditorMode::Select));
    m_canvas->fitToView();
    statusBar()->showMessage(tr("已新建封装"), 3000);
}

void MainWindow::openFootprint() {
    if (!maybeSave())
        return;
    const QString path =
        QFileDialog::getOpenFileName(this, tr("打开 EDA 文档"), QDir::homePath(),
                                     tr("EDA 文档 (*.fp *.brd);;EDA 封装 (*.fp);;EDA PCB (*.brd);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().compare(QStringLiteral("brd"), Qt::CaseInsensitive) == 0) {
        openBoardFile(path);
        return;
    }
    auto loaded = eda::loadFootprint(path);
    if (!loaded.status.ok || !loaded.value.has_value()) {
        QMessageBox::critical(this, tr("打开失败"), loaded.status.message);
        return;
    }
    m_document.installFootprint(*loaded.value, path);
    m_canvas->fitToView();
    statusBar()->showMessage(tr("已打开 %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::openBoardFile(const QString& path) {
    if (!m_libraryService) {
        QMessageBox::critical(this, tr("打开失败"), tr("未注入元件库服务"));
        return;
    }
    const auto loaded = eda::loadBoard(path, [this](const QString& id) { return m_libraryService->load(id); });
    if (!loaded.status.ok || !loaded.value.has_value()) {
        QMessageBox::critical(this, tr("打开失败"), loaded.status.message);
        return;
    }
    m_document.installBoard(*loaded.value, path);
    setEditorMode(static_cast<int>(CanvasWidget::EditorMode::Select));
    m_selectModeAction->setChecked(true);
    m_canvas->fitToView();
    statusBar()->showMessage(tr("已打开 %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::importNetlist() {
    if (!maybeSave())
        return;
    const QString path =
        QFileDialog::getOpenFileName(this, tr("导入网表"), QDir::homePath(), tr("EDA 网表 (*.net);;所有文件 (*)"));
    if (!path.isEmpty())
        importNetlistFile(path);
}

void MainWindow::saveFootprint() {
    if (m_document.filePath().isEmpty()) {
        saveFootprintAs();
        return;
    }
    if (m_document.mode() == Document::Mode::Board) {
        saveBoardFile(m_document.filePath());
        return;
    }
    if (!validateBeforeSave())
        return;
    const auto saved = eda::saveFootprint(m_document.filePath(), m_document.footprintData());
    if (!saved.ok) {
        QMessageBox::critical(this, tr("保存失败"), saved.message);
        return;
    }
    m_document.markClean();
    statusBar()->showMessage(tr("已保存 %1").arg(QFileInfo(m_document.filePath()).fileName()), 4000);
    updateTitle();
}

void MainWindow::saveFootprintAs() {
    const bool boardMode = m_document.mode() == Document::Mode::Board;
    QString path = QFileDialog::getSaveFileName(this, boardMode ? tr("保存 PCB") : tr("保存封装"), QDir::homePath(),
                                                boardMode ? tr("EDA PCB (*.brd)") : tr("EDA 封装 (*.fp)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += boardMode ? QStringLiteral(".brd") : QStringLiteral(".fp");
    if (boardMode) {
        saveBoardFile(path);
        return;
    }
    if (!validateBeforeSave())
        return;
    const auto saved = eda::saveFootprint(path, m_document.footprintData());
    if (!saved.ok) {
        QMessageBox::critical(this, tr("保存失败"), saved.message);
        return;
    }
    m_document.setFilePath(path);
    m_document.markClean();
    statusBar()->showMessage(tr("已保存 %1").arg(QFileInfo(path).fileName()), 4000);
    updateTitle();
}

void MainWindow::saveBoardFile(const QString& path) {
    if (!m_libraryService) {
        QMessageBox::critical(this, tr("保存失败"), tr("未注入元件库服务"));
        return;
    }
    if (!validateBeforeSave())
        return;
    const auto saved =
        eda::saveBoard(path, m_document.boardData(), [this](const QString& id) { return m_libraryService->load(id); });
    if (!saved.ok) {
        QMessageBox::critical(this, tr("保存失败"), saved.message);
        return;
    }
    m_document.setFilePath(path);
    m_document.markClean();
    statusBar()->showMessage(tr("已保存 %1").arg(QFileInfo(path).fileName()), 4000);
    updateTitle();
}

bool MainWindow::maybeSave() {
    if (m_document.isClean())
        return true;
    const auto answer =
        QMessageBox::question(this, tr("未保存的更改"), tr("当前封装有未保存的更改，是否保存？"),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        saveFootprint();
        return m_document.isClean();
    }
    return true;
}

void MainWindow::refreshLibrary() {
    if (!m_libraryService) {
        statusBar()->showMessage(tr("未注入元件库服务"), 5000);
        return;
    }
    m_libraryPanel->refresh();
}

void MainWindow::openLibraryFootprint(const QString& id) {
    if (id.isEmpty() || !maybeSave())
        return;
    if (!m_libraryService) {
        QMessageBox::critical(this, tr("打开失败"), tr("未注入元件库服务"));
        return;
    }
    const auto loaded = m_libraryService->load(id);
    if (!loaded.status.ok || !loaded.value.has_value()) {
        QMessageBox::critical(this, tr("打开失败"), loaded.status.message);
        return;
    }
    m_document.installFootprint(*loaded.value);
    m_canvas->fitToView();
    statusBar()->showMessage(tr("已打开库封装 %1").arg(id), 4000);
}

void MainWindow::importFootprint() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("导入封装"), QDir::homePath(), tr("EDA 封装 (*.fp);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    if (!m_libraryService) {
        QMessageBox::critical(this, tr("导入失败"), tr("未注入元件库服务"));
        return;
    }
    const auto imported = m_libraryService->importFile(path);
    if (!imported.ok) {
        QMessageBox::critical(this, tr("导入失败"), imported.message);
        return;
    }
    refreshLibrary();
    statusBar()->showMessage(tr("已导入 %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::deleteLibraryFootprint(const QString& id) {
    if (!m_libraryService) {
        QMessageBox::critical(this, tr("删除失败"), tr("未注入元件库服务"));
        return;
    }
    const auto answer =
        QMessageBox::question(this, tr("删除封装"), tr("确定从元件库删除“%1”吗？此操作不进入撤销栈。").arg(id),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    const auto removed = m_libraryService->remove(id);
    if (!removed.ok) {
        QMessageBox::critical(this, tr("删除失败"), removed.message);
        return;
    }
    refreshLibrary();
    statusBar()->showMessage(tr("已删除库封装 %1").arg(id), 4000);
}

bool MainWindow::validateBeforeSave() {
    QVector<ValidationIssue> issues;
    if (m_document.mode() == Document::Mode::Board) {
        if (m_validationService) {
            issues = m_validationService->validateBoard(m_document.boardData());
        } else {
            const auto structural = eda::validateBoardStructure(m_document.boardData());
            if (!structural.ok)
                issues.append({ValidationSeverity::Error, structural.message, eda::kInvalidId});
        }
    } else if (m_validationService) {
        issues = m_validationService->validateFootprint(m_document.footprintData());
    } else {
        const auto structural = eda::validateStructure(m_document.footprintData());
        if (!structural.ok)
            issues.append({ValidationSeverity::Error, structural.message, eda::kInvalidId});
    }

    QStringList errors;
    QStringList warnings;
    for (const auto& issue : issues) {
        const QString row = issue.padId == eda::kInvalidId ? QStringLiteral("• %1").arg(issue.message)
                                                           : tr("• 焊盘 %1：%2").arg(issue.padId).arg(issue.message);
        if (issue.severity == ValidationSeverity::Error)
            errors.append(row);
        else
            warnings.append(row);
    }

    if (!errors.isEmpty()) {
        QMessageBox::critical(this, tr("校验未通过"), tr("存在阻止保存的错误：\n\n%1").arg(errors.join('\n')));
        return false;
    }
    if (!warnings.isEmpty()) {
        const auto answer = QMessageBox::warning(
            this, tr("校验警告"), tr("校验发现以下警告：\n\n%1\n\n是否仍然保存？").arg(warnings.join('\n')),
            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Save)
            return false;
    }
    return true;
}

void MainWindow::applyEdit(const eda::Footprint& before, const eda::Footprint& after, const QString& text) {
    auto* command = new ReplaceFootprintCommand(&m_document, before, after, text);
    const auto status = m_document.pushCommand(command);
    if (!status.ok) {
        QMessageBox::warning(this, tr("编辑失败"), status.message);
        return;
    }
    statusBar()->showMessage(text, 2200);
}

void MainWindow::applyBoardEdit(const eda::Board& before, const eda::Board& after, const QString& text) {
    auto* command = new ReplaceBoardCommand(&m_document, before, after, text);
    const auto status = m_document.pushCommand(command);
    if (!status.ok) {
        QMessageBox::warning(this, tr("编辑失败"), status.message);
        return;
    }
    statusBar()->showMessage(text, 2200);
}

void MainWindow::applyPadEdit(const eda::Pad& pad) {
    eda::Footprint before = m_document.footprintData();
    eda::Footprint after = before;
    bool found = false;
    for (auto& candidate : after.pads) {
        if (candidate.id == pad.id) {
            candidate = pad;
            found = true;
            break;
        }
    }
    if (!found) {
        QMessageBox::warning(this, tr("编辑失败"), tr("未找到要修改的焊盘"));
        return;
    }
    applyEdit(before, after, tr("修改焊盘属性"));
}

void MainWindow::deleteSelection() {
    if (m_document.mode() == Document::Mode::Board) {
        deleteBoardSelection();
        return;
    }
    const auto selection = m_document.selection();
    if (selection.isEmpty()) {
        statusBar()->showMessage(tr("没有选中对象"), 2500);
        return;
    }
    QVector<eda::PadId> padIds;
    for (const auto& ref : selection) {
        if (ref.type == ObjectType::Pad)
            padIds.append(ref.padId);
    }
    if (padIds.isEmpty())
        return;

    eda::Footprint before = m_document.footprintData();
    eda::Footprint after = before;
    after.pins.erase(std::remove_if(after.pins.begin(), after.pins.end(),
                                    [&](const eda::Pin& pin) { return padIds.contains(pin.padId); }),
                     after.pins.end());
    after.pads.erase(std::remove_if(after.pads.begin(), after.pads.end(),
                                    [&](const eda::Pad& pad) { return padIds.contains(pad.id); }),
                     after.pads.end());
    const auto status =
        m_document.pushCommand(new ReplaceFootprintCommand(&m_document, before, after, tr("删除焊盘及其引脚")));
    if (!status.ok) {
        QMessageBox::warning(this, tr("删除失败"), status.message);
        return;
    }
    m_document.setSelection({});
}

void MainWindow::deleteBoardSelection() {
    const auto selection = m_document.selection();
    if (selection.isEmpty()) {
        statusBar()->showMessage(tr("没有选中对象"), 2500);
        return;
    }
    eda::Board before = m_document.boardData();
    eda::Board after = before;
    QSet<QString> refdes;
    QSet<quint64> trackIds;
    QSet<quint64> viaIds;
    QSet<quint64> pourIds;
    for (const auto& ref : selection) {
        if (ref.type == ObjectType::Instance)
            refdes.insert(ref.refdes);
        else if (ref.type == ObjectType::Track)
            trackIds.insert(ref.objectId);
        else if (ref.type == ObjectType::Via)
            viaIds.insert(ref.objectId);
        else if (ref.type == ObjectType::Pour)
            pourIds.insert(ref.objectId);
    }
    after.instances.erase(std::remove_if(after.instances.begin(), after.instances.end(),
                                         [&](const eda::Instance& item) { return refdes.contains(item.refdes); }),
                          after.instances.end());
    after.tracks.erase(std::remove_if(after.tracks.begin(), after.tracks.end(),
                                      [&](const eda::Track& item) { return trackIds.contains(item.id); }),
                       after.tracks.end());
    after.vias.erase(std::remove_if(after.vias.begin(), after.vias.end(),
                                    [&](const eda::Via& item) { return viaIds.contains(item.id); }),
                     after.vias.end());
    after.pours.erase(std::remove_if(after.pours.begin(), after.pours.end(),
                                     [&](const eda::CopperPour& item) { return pourIds.contains(item.id); }),
                      after.pours.end());
    for (auto& net : after.nets) {
        net.pins.erase(std::remove_if(net.pins.begin(), net.pins.end(),
                                      [&](const eda::PinRef& pin) { return refdes.contains(pin.refdes); }),
                       net.pins.end());
    }
    const auto status =
        m_document.pushCommand(new ReplaceBoardCommand(&m_document, before, after, tr("删除 PCB 对象")));
    if (!status.ok) {
        QMessageBox::warning(this, tr("删除失败"), status.message);
        return;
    }
    m_document.setSelection({});
}

void MainWindow::addPad() {
    eda::Footprint before = m_document.footprintData();
    eda::Footprint after = before;
    eda::Pad pad;
    pad.pos = m_document.snap(m_canvas->screenToWorld(m_canvas->canvasViewport().center()));
    pad.shape = eda::PadShape::Circle;
    pad.size = {63, 63};
    pad.drill = 32;
    pad.maskMargin = 4;
    pad.clearance = 10;
    pad.layer = m_document.activeLayer() == eda::kBottomCopper ? eda::kBottomCopper : eda::kTopCopper;
    pad.number = 1;
    for (const auto& existing : after.pads)
        pad.number = qMax(pad.number, existing.number + 1);
    const auto added = after.addPad(pad);
    if (!added.status.ok || !added.value.has_value()) {
        QMessageBox::warning(this, tr("添加失败"), added.status.message);
        return;
    }
    applyEdit(before, after, tr("添加焊盘"));
    m_document.setSelection({ObjectRef{ObjectType::Pad, *added.value, {}, 0}});
}

void MainWindow::addPin() {
    const auto selection = m_document.selection();
    const auto it = std::find_if(selection.begin(), selection.end(),
                                 [](const ObjectRef& ref) { return ref.type == ObjectType::Pad; });
    if (it == selection.end()) {
        statusBar()->showMessage(tr("请先选择一个焊盘"), 3000);
        return;
    }
    eda::Footprint before = m_document.footprintData();
    eda::Footprint after = before;
    eda::Pin pin;
    pin.padId = it->padId;
    pin.number = 1;
    if (const auto* pad = after.findPad(pin.padId))
        pin.number = pad->number;
    for (const auto& existing : after.pins)
        pin.number = qMax(pin.number, existing.number + 1);
    pin.name = QString::number(pin.number);
    pin.type = eda::ElectricalType::Passive;
    after.pins.append(pin);
    applyEdit(before, after, tr("添加引脚"));
}

void MainWindow::addHole() {
    eda::Footprint before = m_document.footprintData();
    eda::Footprint after = before;
    after.holes.append(eda::MountingHole{m_document.snap(m_canvas->screenToWorld(m_canvas->canvasViewport().center())),
                                         125, false, 50});
    applyEdit(before, after, tr("添加安装孔"));
}

void MainWindow::addSilkArc() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("添加丝印圆弧"));
    auto* layout = new QFormLayout(&dialog);
    auto* cx = dialogLength(&dialog, 0, -100000, 100000);
    auto* cy = dialogLength(&dialog, 0, -100000, 100000);
    auto* radius = dialogLength(&dialog, 50, 0.001, 100000);
    auto* start = dialogAngle(&dialog, 0);
    auto* span = dialogAngle(&dialog, 180);
    auto* width = dialogLength(&dialog, 8, 0.001, 1000);
    layout->addRow(tr("圆心 X"), cx);
    layout->addRow(tr("圆心 Y"), cy);
    layout->addRow(tr("半径"), radius);
    layout->addRow(tr("起始角"), start);
    layout->addRow(tr("扫掠角"), span);
    layout->addRow(tr("线宽"), width);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    eda::Footprint before = m_document.footprintData();
    eda::Footprint after = before;
    auto arc = std::make_unique<eda::SilkArc>();
    arc->layer = (m_document.activeLayer() == eda::kTopSilk || m_document.activeLayer() == eda::kBottomSilk)
                     ? m_document.activeLayer()
                     : eda::kTopSilk;
    arc->center = {cx->value(), cy->value()};
    arc->radius = radius->value();
    arc->startAngle = start->value();
    arc->spanAngle = span->value();
    arc->width = width->value();
    after.silk.push_back(std::move(arc));
    applyEdit(before, after, tr("添加丝印圆弧"));
}

void MainWindow::addSilkText(const QPointF& position) {
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("添加丝印文字"), tr("文字内容"), QLineEdit::Normal,
                                               QStringLiteral("REF**"), &ok);
    if (!ok || text.isEmpty())
        return;
    eda::Footprint before = m_document.footprintData();
    eda::Footprint after = before;
    auto label = std::make_unique<eda::SilkText>();
    label->layer = (m_document.activeLayer() == eda::kTopSilk || m_document.activeLayer() == eda::kBottomSilk)
                       ? m_document.activeLayer()
                       : eda::kTopSilk;
    label->pos = m_document.snap(position);
    label->text = text;
    label->height = 50;
    after.silk.push_back(std::move(label));
    applyEdit(before, after, tr("添加丝印文字"));
    setEditorMode(static_cast<int>(CanvasWidget::EditorMode::Select));
    m_selectModeAction->setChecked(true);
}

void MainWindow::setEditorMode(int mode) {
    m_canvas->setEditorMode(static_cast<CanvasWidget::EditorMode>(mode));
}

void MainWindow::updateTitle() {
    const bool board = m_document.mode() == Document::Mode::Board;
    const QString untitled = board ? tr("未命名 PCB") : tr("未命名封装");
    const QString name = m_document.filePath().isEmpty() ? untitled : QFileInfo(m_document.filePath()).fileName();
    setWindowModified(!m_document.isClean());
    setWindowTitle(tr("%1[*] · EDA-Shiyan %2").arg(name, board ? tr("PCB 编辑器") : tr("封装编辑器")));
    updateDocumentModeActions();
}

void MainWindow::updateSelectionLabel() {
    const int count = m_document.selection().size();
    m_selectionLabel->setText(count == 0 ? tr("未选择") : tr("已选择 %1").arg(count));
}

void MainWindow::updateDocumentModeActions() {
    const bool footprintMode = m_document.mode() == Document::Mode::Footprint;
    for (auto* action : m_footprintOnlyActions)
        action->setEnabled(footprintMode);
    if (!footprintMode && m_selectModeAction) {
        m_selectModeAction->setChecked(true);
        m_canvas->setEditorMode(CanvasWidget::EditorMode::Select);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}
