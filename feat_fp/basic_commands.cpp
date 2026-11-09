#include "feat_fp/basic_commands.h"

#include "app/document.h"

#include <utility>

ReplaceFootprintCommand::ReplaceFootprintCommand(Document* document, eda::Footprint before, eda::Footprint after,
                                                 QString text)
    : EdaCommand(std::move(text)), m_document(document), m_before(std::move(before)), m_after(std::move(after)) {}

eda::Status ReplaceFootprintCommand::canApply() const {
    if (!m_document || !m_document->footprint())
        return eda::Status::failure(QStringLiteral("没有可编辑的封装文档"));
    return eda::validateStructure(m_after);
}

void ReplaceFootprintCommand::undo() {
    if (!m_document || !m_document->footprint())
        return;
    *m_document->footprint() = m_before;
    m_document->notifyModelChanged();
}

void ReplaceFootprintCommand::redo() {
    if (!m_document || !m_document->footprint())
        return;
    *m_document->footprint() = m_after;
    m_document->notifyModelChanged();
}
