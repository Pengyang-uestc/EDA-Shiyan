#include "app/main_window.h"
#include "app/local_services.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QPalette>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("EDA-Shiyan"));
    QApplication::setOrganizationName(QStringLiteral("EDA-Software-Design-I"));
    QApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#E5E8E8")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#26343B")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#FBFCFB")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#F3F5F4")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#26343B")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#F4F6F6")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#26343B")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2E6874")));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    QApplication::setPalette(palette);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("EDA-Shiyan footprint and PCB editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a window screenshot and exit."), QStringLiteral("path"));
    parser.addOption(screenshotOption);
    QCommandLineOption boardDemoOption(QStringLiteral("board-demo"),
                                       QStringLiteral("Open the built-in PCB demonstration."));
    parser.addOption(boardDemoOption);
    parser.process(app);

    QString libraryRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (libraryRoot.isEmpty())
        libraryRoot = QDir::current().filePath(QStringLiteral("library"));
    LocalLibraryService libraryService(libraryRoot);
    const auto seeded = seedExampleIfEmpty(libraryService);
    if (!seeded.ok)
        qWarning("Failed to initialize example library: %s", qPrintable(seeded.message));
    LocalLayerService layerService;
    StructuralValidationService validationService;
    MainWindow window(&libraryService, &layerService, &validationService);
    if (parser.isSet(boardDemoOption))
        window.loadExampleBoard();
    window.show();

    const QString screenshotPath = parser.value(screenshotOption);
    if (!screenshotPath.isEmpty()) {
        if (window.document()->mode() == Document::Mode::Board && !window.document()->boardData().instances.isEmpty()) {
            window.document()->setSelection({ObjectRef{ObjectType::Instance, eda::kInvalidId,
                                                       window.document()->boardData().instances.first().refdes, 0}});
        } else if (!window.document()->footprintData().pads.isEmpty()) {
            const auto padId = window.document()->footprintData().pads.first().id;
            window.document()->setSelection({ObjectRef{ObjectType::Pad, padId, {}, 0}});
        }
        QTimer::singleShot(700, &window, [&window, screenshotPath] {
            const QFileInfo info(screenshotPath);
            const bool saved = window.grab().save(screenshotPath);
            qInfo("Screenshot %s: %s", saved ? "saved" : "failed", qPrintable(info.absoluteFilePath()));
            QApplication::exit(saved ? 0 : 1);
        });
    }
    return app.exec();
}
