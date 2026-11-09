#pragma once

#include "app/commands.h"
#include "core/board.h"

class Document;

// UI thread. Board snapshot command keeps stable track/via/pour IDs and undoable drags atomic.
class ReplaceBoardCommand : public EdaCommand {
  public:
    ReplaceBoardCommand(Document* document, eda::Board before, eda::Board after, QString text);

    [[nodiscard]] eda::Status canApply() const override;
    void undo() override;
    void redo() override;

  private:
    Document* m_document = nullptr;
    eda::Board m_before;
    eda::Board m_after;
};
