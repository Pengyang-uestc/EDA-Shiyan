#pragma once
#include "board.h"
#include "footprint.h"
#include <QByteArray>
#include <functional>
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

// Synchronous lookup returning an owned model value; callers can adapt FootprintLibrary::load directly.
using FootprintResolver = std::function<Result<Footprint>(const QString& footprintId)>;

struct NetlistComponent {
    QString refdes;
    QString footprintId;
};

struct Netlist {
    QVector<NetlistComponent> components;
    QVector<Net> nets;
};

struct ResolvedNetPin {
    QString netName;
    PinRef pin;
    QString footprintId;
    PadId padId = kInvalidId;
};

// Standalone COMP/NET text; declarations can appear in either order.
[[nodiscard]] Result<Netlist> decodeNetlist(const QByteArray& data);
[[nodiscard]] Result<Netlist> loadNetlist(const QString& path);
// Resolve all components against the library, then return pin-to-pad mappings in input order.
[[nodiscard]] Result<QVector<ResolvedNetPin>> resolveNetlistPins(const Netlist& netlist,
                                                               const FootprintResolver& resolveFootprint);

// Board APIs validate library references as well as the Board's own structure.
[[nodiscard]] Status validateBoard(const Board& board, const FootprintResolver& resolveFootprint);
[[nodiscard]] Result<QByteArray> encodeBoard(const Board& board, const FootprintResolver& resolveFootprint);
[[nodiscard]] Result<Board> decodeBoard(const QByteArray& data, const FootprintResolver& resolveFootprint);
[[nodiscard]] Status saveBoard(const QString& path, const Board& board,
                               const FootprintResolver& resolveFootprint);
[[nodiscard]] Result<Board> loadBoard(const QString& path, const FootprintResolver& resolveFootprint);
} // namespace eda
