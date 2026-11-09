#include "feat_pcb/board_commands.h"

#include "app/document.h"

#include <utility>

ReplaceBoardCommand::ReplaceBoardCommand(Document* document, eda::Board before, eda::Board after, QString text)
    : EdaCommand(std::move(text)), m_document(document), m_before(std::move(before)), m_after(std::move(after)) {}

eda::Status ReplaceBoardCommand::canApply() const {
    if (!m_document || !m_document->board())
        return eda::Status::failure(QStringLiteral("没有可编辑的 PCB 文档"));
    return eda::validateBoardStructure(m_after);
}

void ReplaceBoardCommand::undo() {
    if (!m_document || !m_document->board())
        return;
    *m_document->board() = m_before;
    m_document->notifyModelChanged();
}

void ReplaceBoardCommand::redo() {
    if (!m_document || !m_document->board())
        return;
    *m_document->board() = m_after;
    m_document->notifyModelChanged();
}
