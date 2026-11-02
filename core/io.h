#pragma once
#include "footprint.h"
#include <QByteArray>
namespace eda {
// All APIs: mil, footprint-local, Y up; UI thread; non-null references.
// No ownership retained, no input model mutation; caller owns results.
// Failure: status.ok=false, Chinese message, empty value; no exceptions cross modules.
// New/load bypass undo; B installs successful result in Document and clears undo.
[[nodiscard]] Result<QByteArray> encodeFootprint(const Footprint& footprint);
[[nodiscard]] Result<Footprint> decodeFootprint(const QByteArray& data);
// Atomic save validates structure. C must run manufacturing checks before calling.
[[nodiscard]] Status saveFootprint(const QString& path, const Footprint& footprint);
[[nodiscard]] Result<Footprint> loadFootprint(const QString& path);
} // namespace eda
