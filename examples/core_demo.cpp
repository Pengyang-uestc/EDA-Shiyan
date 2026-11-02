#include <QCoreApplication>
#include <QTextStream>
#include "core/library.h"
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout), err(stderr);
    if (app.arguments().size() != 2) {
        err << "Usage: eda_core_demo <output-library-directory>\n";
        return 2;
    }
    eda::FootprintLibrary library(app.arguments()[1]);
    const auto original = eda::exampleFootprint();
    auto saved = library.save(original);
    if (!saved.ok) {
        err << saved.message << '\n';
        return 1;
    }
    auto indexed = library.rebuildIndex();
    if (!indexed.ok) {
        err << indexed.message << '\n';
        return 1;
    }
    auto loaded = library.load(original.id);
    if (!loaded.status.ok) {
        err << loaded.status.message << '\n';
        return 1;
    }
    const auto& restored = *loaded.value;
    if (*eda::encodeFootprint(original).value != *eda::encodeFootprint(restored).value) {
        err << "Round-trip mismatch\n";
        return 1;
    }
    out << "PASS: saved, indexed, reloaded; " << restored.pads.size() << " pads, " << restored.pins.size() << " pins, "
        << restored.silk.size() << " primitives\n";
    return 0;
}
