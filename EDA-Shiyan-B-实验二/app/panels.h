#pragma once

#include "core/footprint.h"

#include <QWidget>

class DocumentContext;
class ILayerService;
class ILibraryService;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QSpinBox;
class QStandardItemModel;
class QToolButton;
class QTreeView;

class LibraryPanel : public QWidget {
    Q_OBJECT

  public:
    explicit LibraryPanel(QWidget* parent = nullptr);
    void setService(ILibraryService* service);
    ILibraryService* service() const;
    QString selectedId() const;
    void refresh();

  signals:
    void openRequested(const QString& id);
    void importRequested();
    void deleteRequested(const QString& id);
    void statusMessageRequested(const QString& message);

  private:
    void applyFilter();
    void activateIndex(const QModelIndex& index);
    void connectModel();
    ILibraryService* m_service = nullptr;
    QLineEdit* m_search = nullptr;
    QTreeView* m_tree = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QLabel* m_pathLabel = nullptr;
};

class LayerPanel : public QWidget {
    Q_OBJECT

  public:
    explicit LayerPanel(QWidget* parent = nullptr);
    void setDocumentContext(DocumentContext* context);
    void setService(ILayerService* service);
    ILayerService* service() const;
    void refresh();

  signals:
    void activeLayerRequested(eda::LayerId id);
    void layerVisibilityRequested(eda::LayerId id, bool visible);
    void statusMessageRequested(const QString& message);

  private:
    void rebuild();
    void connectModel();
    void syncVisibility(eda::LayerId id, bool visible);
    void setActiveRow(eda::LayerId id);
    DocumentContext* m_context = nullptr;
    ILayerService* m_service = nullptr;
    QTreeView* m_list = nullptr;
    QStandardItemModel* m_displayModel = nullptr;
    QLabel* m_hint = nullptr;
    bool m_syncing = false;
};

class PropertyPanel : public QWidget {
    Q_OBJECT

  public:
    explicit PropertyPanel(QWidget* parent = nullptr);
    void setDocumentContext(DocumentContext* context);
    void refresh();

  signals:
    void padEditRequested(const eda::Pad& pad);

  private:
    void apply();
    void updateShapeState();
    DocumentContext* m_context = nullptr;
    eda::Pad m_pad;
    bool m_hasPad = false;
    QLabel* m_summaryLabel = nullptr;
    QGroupBox* m_docGroup = nullptr;
    QLabel* m_idValue = nullptr;
    QLabel* m_nameValue = nullptr;
    QLabel* m_boardInfo = nullptr;
    QGroupBox* m_padGroup = nullptr;
    QFormLayout* m_form = nullptr;
    QSpinBox* m_padNumber = nullptr;
    QComboBox* m_shape = nullptr;
    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QDoubleSpinBox* m_drill = nullptr;
    QDoubleSpinBox* m_mask = nullptr;
    QDoubleSpinBox* m_clearance = nullptr;
    QDoubleSpinBox* m_corner = nullptr;
    QComboBox* m_layer = nullptr;
    QPushButton* m_applyButton = nullptr;
    bool m_syncing = false;
};
