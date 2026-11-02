#pragma once
#include "units.h"
#include <QPointF>
#include <QSizeF>
#include <QRectF>
#include <QColor>
#include <QPolygonF>
#include <QString>
#include <QVector>
namespace eda {
using PadId = quint64;
using LayerId = int;
constexpr quint64 kInvalidId = 0;
constexpr LayerId kTopCopper = 0;
constexpr LayerId kBottomCopper = 1;
constexpr LayerId kTopSilk = 2;
constexpr LayerId kBottomSilk = 3;
constexpr LayerId kTopMask = 4;
constexpr LayerId kBottomMask = 5;
constexpr LayerId kBoardOutline = 6;
enum class PadShape { Circle, Rectangle, Oblong, RoundRect };
enum class ElectricalType { Input, Output, Bidirectional, Passive, Power, NoConnect };
} // namespace eda
