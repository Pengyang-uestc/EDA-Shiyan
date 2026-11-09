#pragma once
// Implementation-only header. B/C must include io.h instead.
#include "io.h"
namespace eda::detail {
bool validateStructureImpl(const Footprint&, QString*);
QByteArray encodeFootprintImpl(const Footprint&);
bool decodeFootprintImpl(const QByteArray&, Footprint&, QString*);
bool saveFootprintImpl(const QString&, const Footprint&, QString*);
bool loadFootprintImpl(const QString&, Footprint&, QString*);

bool validateBoardReferencesImpl(const Board&, const FootprintResolver&, QString*);
bool encodeBoardImpl(const Board&, QByteArray&, QString*);
bool decodeBoardImpl(const QByteArray&, Board&, const FootprintResolver&, QString*);
bool saveBoardImpl(const QString&, const Board&, const FootprintResolver&, QString*);
bool loadBoardImpl(const QString&, Board&, const FootprintResolver&, QString*);
} // namespace eda::detail
