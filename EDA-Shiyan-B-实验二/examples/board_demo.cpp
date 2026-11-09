#include "core/io.h"
#include "core/library.h"

#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout), error(stderr);
    if (app.arguments().size() != 2) {
        error << "Usage: eda_board_demo <examples-directory>\n";
        return 2;
    }
    const QDir examples(app.arguments()[1]);
    const eda::FootprintLibrary library(examples.absolutePath());
    const eda::FootprintResolver resolve = [&](const QString& id) { return library.load(id); };

    const auto netlist = eda::loadNetlist(examples.filePath(QStringLiteral("board-demo.net")));
    if (!netlist.status.ok) {
        error << netlist.status.message << '\n';
        return 1;
    }
    const auto pins = eda::resolveNetlistPins(*netlist.value, resolve);
    if (!pins.status.ok) {
        error << pins.status.message << '\n';
        return 1;
    }
    const auto board = eda::loadBoard(examples.filePath(QStringLiteral("board-demo.brd")), resolve);
    if (!board.status.ok) {
        error << board.status.message << '\n';
        return 1;
    }
    out << "NETLIST: " << netlist.value->components.size() << " components, "
        << netlist.value->nets.size() << " nets\n";
    for (const auto& pin : *pins.value)
        out << pin.netName << ": " << pin.pin.refdes << '.' << pin.pin.pinNumber
            << " -> " << pin.footprintId << " pad " << pin.padId << '\n';
    out << "BOARD: " << board.value->layers.size() << " layers, " << board.value->instances.size()
        << " instances, " << board.value->tracks.size() << " tracks, " << board.value->vias.size()
        << " vias, " << board.value->pours.size() << " pours\n";
    return 0;
}
