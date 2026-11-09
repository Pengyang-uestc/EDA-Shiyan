#pragma once

#include "app/commands.h"
#include "core/footprint.h"

class Document;

// Generic UI-facing snapshot command. It keeps stable IDs and makes drag/property edits atomic.
class ReplaceFootprintCommand : public EdaCommand {
  public:
    ReplaceFootprintCommand(Document* document, eda::Footprint before, eda::Footprint after, QString text);

    [[nodiscard]] eda::Status canApply() const override;
    void undo() override;
    void redo() override;

  private:
    Document* m_document = nullptr;
    eda::Footprint m_before;
    eda::Footprint m_after;
};
