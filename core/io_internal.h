#pragma once
// Implementation-only header. B/C must include io.h instead.
#include "footprint.h"
#include <QByteArray>
namespace eda::detail {
bool validateStructureImpl(const Footprint&, QString*);
QByteArray encodeFootprintImpl(const Footprint&);
bool decodeFootprintImpl(const QByteArray&, Footprint&, QString*);
bool saveFootprintImpl(const QString&, const Footprint&, QString*);
bool loadFootprintImpl(const QString&, Footprint&, QString*);
} // namespace eda::detail
