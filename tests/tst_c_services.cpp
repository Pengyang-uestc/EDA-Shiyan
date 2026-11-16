#include <QtTest>

#include "app/demo_documents.h"
#include "app/document.h"
#include "app/manufacturing_services.h"
#include "core/footprint.h"
#include "feat_fp/footprint_templates.h"
#include "feat_pcb/drc_report.h"

using namespace eda;

class TestCServices : public QObject {
    Q_OBJECT

private slots:
    void footprintRulesMapped();
    void boardRulesUseResolver();
    void boardRulesWithoutResolver();
    void layerFlatAndTree();
    void layerWithoutContext();
    void drcIssuesMapped();
};

void TestCServices::footprintRulesMapped()
{
    const auto cleanTemplate = makeDip8Footprint();
    QVERIFY2(cleanTemplate.status.ok, qPrintable(cleanTemplate.status.message));

    ManufacturingValidationService service;
    QVERIFY(service.validateFootprint(*cleanTemplate.value).isEmpty());

    Footprint bad = *cleanTemplate.value;
    bad.pads[0].drill = 3.0;
    const auto issues = service.validateFootprint(bad);
    QVERIFY(!issues.isEmpty());
    bool found = false;
    for (const ValidationIssue& issue : issues) {
        if (issue.severity != ValidationSeverity::Error)
            continue;
        if (issue.message.contains(QStringLiteral("DRILL"))) {
            found = true;
            QCOMPARE(issue.padId, bad.pads[0].id);
        }
    }
    QVERIFY(found);
}

void TestCServices::boardRulesUseResolver()
{
    const Board board = exampleBoard();
    ManufacturingValidationService service([](const QString&) {
        Result<Footprint> result;
        result.status = Status::success();
        result.value = exampleFootprint();
        return result;
    });

    const auto issues = service.validateBoard(board);
    for (const ValidationIssue& issue : issues)
        QVERIFY2(issue.severity != ValidationSeverity::Error, qPrintable(issue.message));
}

void TestCServices::boardRulesWithoutResolver()
{
    Board board = exampleBoard();
    board.instances[0].footprintId = QStringLiteral("missing");
    ManufacturingValidationService service;
    const auto issues = service.validateBoard(board);
    bool found = false;
    for (const ValidationIssue& issue : issues) {
        if (issue.severity == ValidationSeverity::Error && issue.message.contains(QStringLiteral("missing")))
            found = true;
    }
    QVERIFY(found);
}

void TestCServices::layerFlatAndTree()
{
    Document document;
    document.installBoard(exampleBoard());

    LayerModelService service;
    service.setDocumentContext(&document);
    QVERIFY(service.refresh().ok);

    QCOMPARE(service.treeModel()->rowCount(), 2);
    QCOMPARE(service.treeModel()->rowCount(service.treeModel()->index(0, 0)), 4);

    QAbstractItemModel* flat = service.model();
    QCOMPARE(flat->rowCount(), 7);
    const QModelIndex first = flat->index(0, 0);
    QCOMPARE(first.data(ServiceRoles::LayerIdRole).toInt(), int(kTopCopper));
    QCOMPARE(first.data(ServiceRoles::NameRole).toString(), QStringLiteral("Top / Top Copper"));
    QVERIFY(first.data(ServiceRoles::LayerColorRole).value<QColor>().isValid());
    QCOMPARE(first.data(ServiceRoles::LayerTypeRole).toInt(), int(BoardLayerType::Copper));
}

void TestCServices::layerWithoutContext()
{
    LayerModelService service;
    QVERIFY(service.refresh().ok);
    QCOMPARE(service.model()->rowCount(), 6);
    QCOMPARE(service.treeModel()->rowCount(), 0);
}

void TestCServices::drcIssuesMapped()
{
    DrcResult result;
    DrcViolation violation;
    violation.code = drc_code::kPadTrackClearance;
    violation.message = QStringLiteral("焊盘与走线间距不足");
    violation.padId = 4;
    violation.trackIdA = 9;
    violation.actualMil = 5.0;
    violation.requiredMil = 7.874;
    result.violations.append(violation);
    result.errors.append(QStringLiteral("找不到封装 missing"));

    const auto issues = collectDrcIssues(result);
    QCOMPARE(issues.size(), 2);
    QCOMPARE(issues[0].kind, DrcIssueKind::Clearance);
    QCOMPARE(issues[0].padId, PadId(4));
    QCOMPARE(issues[0].trackId, TrackId(9));
    QCOMPARE(issues[0].requiredMil, 7.874);
    QCOMPARE(issues[1].kind, DrcIssueKind::Resolution);
    QCOMPARE(issues[1].code, kDrcResolveError);
    QVERIFY(issues[1].message.contains(QStringLiteral("missing")));
}

QTEST_MAIN(TestCServices)

#include "tst_c_services.moc"