#pragma once

#include "core/status.h"

#include <QUndoCommand>

// UI thread; every document edit is applied through this command base.
class EdaCommand : public QUndoCommand {
  public:
    using QUndoCommand::QUndoCommand;
    ~EdaCommand() override = default;

    // Checked before the command enters QUndoStack. Failure keeps the model unchanged.
    [[nodiscard]] virtual eda::Status canApply() const = 0;
};
