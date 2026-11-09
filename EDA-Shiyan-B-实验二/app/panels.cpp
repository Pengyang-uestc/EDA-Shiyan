#include "app/panels.h"

#include "app/document_context.h"
#include "app/services.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {
QDoubleSpinBox* lengthSpin(QWidget* parent, double minimum = 0.0, double maximum = 100000.0) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(3);
    spin->setRange(minimum, maximum);
    spin->setSuffix(QObject::tr(" mil"));
    spin->setKeyboardTracking(false);
    spin->setSingleStep(1.0);
    return spin;
}
} // namespace

LibraryPanel::LibraryPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);
    m_search = new QLineEdit(header);
    m_search->setPlaceholderText(tr("筛选封装"));
    m_search->setClearButtonEnabled(true);
    auto* refresh = new QToolButton(header);
    refresh->setText(tr("刷新"));
    refresh->setToolTip(tr("重新扫描元件库"));
    auto* import = new QToolButton(header);
    import->setText(tr("导入"));
    import->setToolTip(tr("导入 .fp 封装文件"));
    auto* remove = new QToolButton(header);
    remove->setText(tr("删除"));
    remove->setToolTip(tr("从元件库删除选中封装"));
    headerLayout->addWidget(m_search, 1);
    headerLayout->addWidget(refresh);
    headerLayout->addWidget(import);
    headerLayout->addWidget(remove);
    layout->addWidget(header);

    m_tree = new QTreeView(this);
    m_tree->setObjectName(QStringLiteral("libraryTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(0);
    m_proxy->setDynamicSortFilter(true);
    m_tree->setModel(m_proxy);
    layout->addWidget(m_tree, 1);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setObjectName(QStringLiteral("libraryPathLabel"));
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setStyleSheet(QStringLiteral("color:#7B848A; font-size:10px;"));
    layout->addWidget(m_pathLabel);

    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(refresh, &QToolButton::clicked, this, &LibraryPanel::refresh);
    connect(import, &QToolButton::clicked, this, &LibraryPanel::importRequested);
    connect(remove, &QToolButton::clicked, this, [this] {
        const QString id = selectedId();
        if (!id.isEmpty())
            emit deleteRequested(id);
    });
    connect(m_tree, &QTreeView::doubleClicked, this, &LibraryPanel::activateIndex);
    connect(m_tree, &QTreeView::activated, this, &LibraryPanel::activateIndex);
}

void LibraryPanel::setService(ILibraryService* service) {
    if (m_service)
        disconnect(m_service->model(), nullptr, this, nullptr);
    m_service = service;
    connectModel();
    m_proxy->setSourceModel(m_service ? m_service->model() : nullptr);
    if (m_service) {
        const QString path = m_service->rootPath();
        m_pathLabel->setText(tr("库：%1").arg(path));
        m_pathLabel->setToolTip(path);
    } else {
        m_pathLabel->setText(tr("未注入元件库服务"));
    }
    applyFilter();
}

ILibraryService* LibraryPanel::service() const {
    return m_service;
}

QString LibraryPanel::selectedId() const {
    const QModelIndex current = m_tree->currentIndex();
    return current.isValid() ? current.data(ServiceRoles::IdRole).toString() : QString();
}

void LibraryPanel::refresh() {
    if (!m_service) {
        emit statusMessageRequested(tr("未注入元件库服务"));
        return;
    }
    const auto status = m_service->refresh();
    if (!status.ok)
        emit statusMessageRequested(status.message);
    applyFilter();
}

void LibraryPanel::applyFilter() {
    m_proxy->setFilterFixedString(m_search->text().trimmed());
}

void LibraryPanel::activateIndex(const QModelIndex& index) {
    if (!index.isValid())
        return;
    const QString id = index.data(ServiceRoles::IdRole).toString();
    if (!id.isEmpty())
        emit openRequested(id);
}

void LibraryPanel::connectModel() {
    if (!m_service)
        return;
    QAbstractItemModel* model = m_service->model();
    connect(model, &QAbstractItemModel::modelReset, this, &LibraryPanel::applyFilter);
    connect(model, &QAbstractItemModel::layoutChanged, this, &LibraryPanel::applyFilter);
    connect(model, &QAbstractItemModel::dataChanged, this, [this] { applyFilter(); });
}

LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto* hint = new QLabel(tr("勾选控制可见性，单击设为当前层。"), header);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#657177;"));
    auto* refresh = new QToolButton(header);
    refresh->setText(tr("刷新"));
    refresh->setToolTip(tr("从图层服务重新加载"));
    headerLayout->addWidget(hint, 1);
    headerLayout->addWidget(refresh);
    layout->addWidget(header);

    m_list = new QTreeView(this);
    m_list->setObjectName(QStringLiteral("layerList"));
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_displayModel = new QStandardItemModel(this);
    m_list->setModel(m_displayModel);
    layout->addWidget(m_list, 1);
    m_hint = new QLabel(tr("当前层：未注入图层服务"), this);
    m_hint->setStyleSheet(QStringLiteral("font-weight:600; color:#284C63;"));
    layout->addWidget(m_hint);

    connect(refresh, &QToolButton::clicked, this, &LayerPanel::refresh);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        if (!m_syncing && current.isValid())
            emit activeLayerRequested(current.data(ServiceRoles::LayerIdRole).toInt());
    });
    connect(m_displayModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item) {
        if (!m_syncing && item)
            emit layerVisibilityRequested(item->data(ServiceRoles::LayerIdRole).toInt(),
                                          item->checkState() == Qt::Checked);
    });
}

void LayerPanel::setDocumentContext(DocumentContext* context) {
    if (m_context)
        disconnect(m_context, nullptr, this, nullptr);
    m_context = context;
    if (m_context) {
        connect(m_context, &DocumentContext::activeLayerChanged, this, [this](eda::LayerId id) { setActiveRow(id); });
        connect(m_context, &DocumentContext::layerVisibilityChanged, this,
                [this](eda::LayerId id, bool visible) { syncVisibility(id, visible); });
    }
    rebuild();
}

void LayerPanel::setService(ILayerService* service) {
    if (m_service)
        disconnect(m_service->model(), nullptr, this, nullptr);
    m_service = service;
    connectModel();
    rebuild();
}

ILayerService* LayerPanel::service() const {
    return m_service;
}

void LayerPanel::refresh() {
    if (!m_service) {
        emit statusMessageRequested(tr("未注入图层服务"));
        return;
    }
    const auto status = m_service->refresh();
    if (!status.ok)
        emit statusMessageRequested(status.message);
    rebuild();
}

void LayerPanel::connectModel() {
    if (!m_service)
        return;
    QAbstractItemModel* model = m_service->model();
    connect(model, &QAbstractItemModel::modelReset, this, &LayerPanel::rebuild);
    connect(model, &QAbstractItemModel::layoutChanged, this, &LayerPanel::rebuild);
    connect(model, &QAbstractItemModel::dataChanged, this, [this] { rebuild(); });
}

void LayerPanel::rebuild() {
    m_syncing = true;
    m_displayModel->clear();
    m_displayModel->setHorizontalHeaderLabels({tr("层")});
    if (m_service) {
        QAbstractItemModel* source = m_service->model();
        for (int row = 0; row < source->rowCount(); ++row) {
            const QModelIndex index = source->index(row, 0);
            const int id = index.data(ServiceRoles::LayerIdRole).toInt();
            auto* item = new QStandardItem(index.data(Qt::DecorationRole).value<QIcon>(),
                                           index.data(Qt::DisplayRole).toString());
            item->setData(id, ServiceRoles::LayerIdRole);
            item->setData(index.data(ServiceRoles::NameRole), ServiceRoles::NameRole);
            item->setData(index.data(ServiceRoles::LayerColorRole), ServiceRoles::LayerColorRole);
            item->setEditable(false);
            item->setCheckable(true);
            item->setCheckState(!m_context || m_context->isLayerVisible(id) ? Qt::Checked : Qt::Unchecked);
            m_displayModel->appendRow(item);
        }
    }
    m_syncing = false;
    if (m_context)
        setActiveRow(m_context->activeLayer());
    else
        m_hint->setText(m_service ? tr("未选择活动层") : tr("当前层：未注入图层服务"));
}

void LayerPanel::syncVisibility(eda::LayerId id, bool visible) {
    m_syncing = true;
    for (int row = 0; row < m_displayModel->rowCount(); ++row) {
        auto* item = m_displayModel->item(row, 0);
        if (item && item->data(ServiceRoles::LayerIdRole).toInt() == id)
            item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    }
    m_syncing = false;
}

void LayerPanel::setActiveRow(eda::LayerId id) {
    m_syncing = true;
    bool found = false;
    for (int row = 0; row < m_displayModel->rowCount(); ++row) {
        const QModelIndex index = m_displayModel->index(row, 0);
        if (index.data(ServiceRoles::LayerIdRole).toInt() == id) {
            m_list->setCurrentIndex(index);
            m_hint->setText(tr("当前层：%1").arg(index.data(Qt::DisplayRole).toString()));
            found = true;
            break;
        }
    }
    m_syncing = false;
    if (!found)
        m_hint->setText(tr("当前层不可用"));
}

PropertyPanel::PropertyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    m_docGroup = new QGroupBox(tr("文档"), this);
    auto* docForm = new QFormLayout(m_docGroup);
    m_idValue = new QLabel(m_docGroup);
    m_nameValue = new QLabel(m_docGroup);
    m_nameValue->setWordWrap(true);
    m_boardInfo = new QLabel(m_docGroup);
    m_boardInfo->setWordWrap(true);
    m_boardInfo->setVisible(false);
    docForm->addRow(tr("ID"), m_idValue);
    docForm->addRow(tr("名称"), m_nameValue);
    docForm->addRow(tr("板级信息"), m_boardInfo);
    layout->addWidget(m_docGroup);

    m_summaryLabel = new QLabel(tr("未选择对象"), this);
    m_summaryLabel->setObjectName(QStringLiteral("selectionSummary"));
    m_summaryLabel->setStyleSheet(QStringLiteral("font-weight:600; color:#284C63;"));
    layout->addWidget(m_summaryLabel);

    m_padGroup = new QGroupBox(tr("焊盘属性"), this);
    m_form = new QFormLayout(m_padGroup);
    m_padNumber = new QSpinBox(m_padGroup);
    m_padNumber->setRange(1, 9999);
    m_shape = new QComboBox(m_padGroup);
    m_shape->addItem(tr("圆形"), static_cast<int>(eda::PadShape::Circle));
    m_shape->addItem(tr("矩形"), static_cast<int>(eda::PadShape::Rectangle));
    m_shape->addItem(tr("长圆"), static_cast<int>(eda::PadShape::Oblong));
    m_shape->addItem(tr("圆角矩形"), static_cast<int>(eda::PadShape::RoundRect));
    m_x = lengthSpin(m_padGroup, -100000.0);
    m_y = lengthSpin(m_padGroup, -100000.0);
    m_width = lengthSpin(m_padGroup, 0.001);
    m_height = lengthSpin(m_padGroup, 0.001);
    m_drill = lengthSpin(m_padGroup);
    m_mask = lengthSpin(m_padGroup);
    m_clearance = lengthSpin(m_padGroup);
    m_corner = lengthSpin(m_padGroup);
    m_layer = new QComboBox(m_padGroup);
    m_layer->addItem(tr("顶层铜"), eda::kTopCopper);
    m_layer->addItem(tr("底层铜"), eda::kBottomCopper);

    m_form->addRow(tr("引脚编号"), m_padNumber);
    m_form->addRow(tr("形状"), m_shape);
    m_form->addRow(tr("中心 X"), m_x);
    m_form->addRow(tr("中心 Y"), m_y);
    m_form->addRow(tr("宽"), m_width);
    m_form->addRow(tr("高"), m_height);
    m_form->addRow(tr("钻孔"), m_drill);
    m_form->addRow(tr("阻焊扩展"), m_mask);
    m_form->addRow(tr("安全间距"), m_clearance);
    m_form->addRow(tr("圆角半径"), m_corner);
    m_form->addRow(tr("所属层"), m_layer);

    m_applyButton = new QPushButton(tr("应用属性"), m_padGroup);
    m_applyButton->setObjectName(QStringLiteral("applyPadProperties"));
    m_applyButton->setEnabled(false);
    m_form->addRow(QString(), m_applyButton);
    layout->addWidget(m_padGroup);
    layout->addStretch(1);

    connect(m_applyButton, &QPushButton::clicked, this, &PropertyPanel::apply);
    connect(m_shape, &QComboBox::currentIndexChanged, this, [this] { updateShapeState(); });
    updateShapeState();
}

void PropertyPanel::setDocumentContext(DocumentContext* context) {
    if (m_context)
        disconnect(m_context, nullptr, this, nullptr);
    m_context = context;
    if (m_context) {
        connect(m_context, &DocumentContext::selectionChanged, this, &PropertyPanel::refresh);
        connect(m_context, &DocumentContext::modelChanged, this, &PropertyPanel::refresh);
        connect(m_context, &DocumentContext::activeLayerChanged, this, &PropertyPanel::refresh);
    }
    refresh();
}

void PropertyPanel::refresh() {
    if (m_syncing)
        return;
    m_syncing = true;
    const eda::Footprint* footprint = m_context ? m_context->footprint() : nullptr;
    const eda::Board* board = m_context ? m_context->board() : nullptr;
    m_boardInfo->setVisible(board != nullptr);
    m_padGroup->setVisible(board == nullptr);
    m_docGroup->setTitle(board ? tr("PCB 文档") : tr("封装"));
    if (board) {
        m_idValue->setText(QStringLiteral("PCB"));
        m_nameValue->setText(QStringLiteral("EDA-BRD"));
        m_boardInfo->setText(tr("%1 个层组 · %2 个层 · %3 个实例 · %4 条走线")
                                 .arg(board->groups.size())
                                 .arg(board->layers.size())
                                 .arg(board->instances.size())
                                 .arg(board->tracks.size()));
        const auto selection = m_context->selection();
        if (selection.isEmpty()) {
            m_summaryLabel->setText(tr("未选择 PCB 对象"));
        } else {
            const ObjectRef& first = selection.first();
            QString type;
            switch (first.type) {
            case ObjectType::Instance:
                type = tr("实例 %1").arg(first.refdes);
                break;
            case ObjectType::Track:
                type = tr("走线 #%1").arg(first.objectId);
                break;
            case ObjectType::Via:
                type = tr("过孔 #%1").arg(first.objectId);
                break;
            case ObjectType::Pour:
                type = tr("覆铜 #%1").arg(first.objectId);
                break;
            default:
                type = tr("对象");
                break;
            }
            m_summaryLabel->setText(selection.size() == 1
                                        ? tr("已选择 %1").arg(type)
                                        : tr("已选择 %1 个对象，首个为 %2").arg(selection.size()).arg(type));
        }
        m_hasPad = false;
        m_applyButton->setEnabled(false);
        updateShapeState();
        m_syncing = false;
        return;
    }
    m_idValue->setText(footprint ? footprint->id : QStringLiteral("-"));
    m_nameValue->setText(footprint ? footprint->name : QStringLiteral("-"));

    const auto selection = m_context ? m_context->selection() : QVector<ObjectRef>{};
    m_hasPad = false;
    if (footprint && !selection.isEmpty() && selection.first().type == ObjectType::Pad) {
        const auto* pad = footprint->findPad(selection.first().padId);
        if (pad) {
            m_pad = *pad;
            m_hasPad = true;
        }
    }

    if (m_hasPad) {
        m_summaryLabel->setText(selection.size() == 1 ? tr("已选择焊盘 #%1").arg(m_pad.number)
                                                      : tr("已选择 %1 个对象，显示首个焊盘").arg(selection.size()));
        m_padNumber->setValue(m_pad.number);
        m_shape->setCurrentIndex(m_shape->findData(static_cast<int>(m_pad.shape)));
        m_x->setValue(m_pad.pos.x());
        m_y->setValue(m_pad.pos.y());
        m_width->setValue(m_pad.size.width());
        m_height->setValue(m_pad.size.height());
        m_drill->setValue(m_pad.drill);
        m_mask->setValue(m_pad.maskMargin);
        m_clearance->setValue(m_pad.clearance);
        m_corner->setValue(m_pad.cornerRadius);
        m_layer->setCurrentIndex(m_layer->findData(m_pad.layer));
    } else {
        m_summaryLabel->setText(footprint && footprint->pads.isEmpty() ? tr("封装中还没有焊盘") : tr("未选择焊盘"));
    }
    m_form->setEnabled(m_hasPad);
    m_applyButton->setEnabled(m_hasPad);
    updateShapeState();
    m_syncing = false;
}

void PropertyPanel::apply() {
    if (!m_hasPad || m_syncing)
        return;
    eda::Pad pad = m_pad;
    pad.number = m_padNumber->value();
    pad.shape = static_cast<eda::PadShape>(m_shape->currentData().toInt());
    pad.pos = {m_x->value(), m_y->value()};
    pad.size = {m_width->value(), m_height->value()};
    pad.drill = m_drill->value();
    pad.maskMargin = m_mask->value();
    pad.clearance = m_clearance->value();
    pad.cornerRadius = pad.shape == eda::PadShape::RoundRect ? m_corner->value() : 0.0;
    pad.layer = m_layer->currentData().toInt();
    emit padEditRequested(pad);
}

void PropertyPanel::updateShapeState() {
    const bool roundRect = static_cast<eda::PadShape>(m_shape->currentData().toInt()) == eda::PadShape::RoundRect;
    m_corner->setEnabled(roundRect && m_hasPad);
}
