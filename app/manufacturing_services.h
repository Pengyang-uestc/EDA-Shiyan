#pragma once

#include "app/services.h"
#include "core/io.h"
#include "feat_pcb/layer_model.h"

#include <QStandardItemModel>

class DocumentContext;

// 把 C 的制造规则接到 B 的服务接口上。
// validateFootprint 走 feat_fp；validateBoard 走 feat_pcb 的板级校验，并把 C 的
// 封装规则作为回调并入，实例焊盘不会退化成“未检查”。
// resolver 通常由 ILibraryService::load 包装，用于按 footprintId 取封装。
class ManufacturingValidationService : public IValidationService {
  public:
    explicit ManufacturingValidationService(eda::FootprintResolver resolver = {});

    QVector<ValidationIssue> validateFootprint(const eda::Footprint& footprint) const override;
    QVector<ValidationIssue> validateBoard(const eda::Board& board) const override;

  private:
    eda::FootprintResolver m_resolver;
};

// 层树服务：treeModel() 给两级 QTreeView（B-3 用），model() 维持当前扁平层面板。
// 两者都是只读；增删改层请调 eda::LayerService，并把结果包进 ReplaceBoardCommand。
// active/visible 仍归 DocumentContext，不在模型里。
class LayerModelService : public ILayerService {
  public:
    LayerModelService();

    QAbstractItemModel* model() const override;
    void setDocumentContext(DocumentContext* context) override;
    eda::Status refresh() override;

    eda::LayerModel* treeModel() { return &m_tree; }
    const eda::LayerModel* treeModel() const { return &m_tree; }

  private:
    void rebuildFlat();

    DocumentContext* m_context = nullptr;
    eda::LayerModel m_tree;
    mutable QStandardItemModel m_flat;
};