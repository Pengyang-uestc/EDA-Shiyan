#include <QtTest>

#include "core/validation.h"
#include "feat_fp/footprint_templates.h"
#include "feat_fp/footprint_validation.h"

using namespace eda;

namespace {

bool hasIssue(const QVector<Issue>& issues, const QString& code)
{
    for (const Issue& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

bool loadDip8(Footprint& footprint)
{
    const Result<Footprint> result = makeDip8Footprint();
    if (!result.status.ok || !result.value.has_value()) {
        return false;
    }
    footprint = *result.value;
    return true;
}

} // namespace

class TestFootprintValidation : public QObject {
    Q_OBJECT

private slots:
    void validTemplatePasses();
    void drillOutOfRange();
    void annularRingUsesShortSide();
    void maskMarginOutOfRange();
    void clearanceTooSmall();
    void duplicatePinPadReference();
    void padSpacingTooSmall();
    void differentLayersSkipSpacing();
    void structureErrorShortCircuits();
    void unreferencedPadIsWarning();
    void duplicatePadNumber();
    void holeDrillOutOfRange();
    void silkWidthTooSmall();
    void circleNotSquareIsWarning();
    void unsupportedShapeGivesWarning();
};

void TestFootprintValidation::validTemplatePasses()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    const QVector<Issue> issues = validateFootprint(fp);
    QVERIFY2(issues.isEmpty(), qPrintable(issues.isEmpty()
                                             ? QString()
                                             : issues.first().message));
}

void TestFootprintValidation::drillOutOfRange()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pads[0].drill = 3.0;
    QVERIFY(hasIssue(validateFootprint(fp), issue_code::kDrill));
}

void TestFootprintValidation::annularRingUsesShortSide()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pads[0].size = QSizeF(100, 40);
    fp.pads[0].drill = 32.0;
    QVERIFY(hasIssue(validateFootprint(fp), issue_code::kAnnularRing));
}

void TestFootprintValidation::maskMarginOutOfRange()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pads[0].maskMargin = 10.0;
    QVERIFY(hasIssue(validateFootprint(fp), issue_code::kMaskMargin));
}

void TestFootprintValidation::clearanceTooSmall()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pads[0].clearance = 1.0;
    QVERIFY(hasIssue(validateFootprint(fp), issue_code::kClearance));
}

void TestFootprintValidation::duplicatePinPadReference()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pins[1].padId = fp.pins[0].padId;
    QVERIFY(hasIssue(validateFootprint(fp), issue_code::kPinPadDuplicate));
}

void TestFootprintValidation::padSpacingTooSmall()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pads[1].pos = QPointF(-150, 120);
    QVERIFY(hasIssue(validateFootprint(fp), issue_code::kPadSpacing));
}

void TestFootprintValidation::differentLayersSkipSpacing()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pads[1].pos = fp.pads[0].pos;
    fp.pads[1].layer = kBottomCopper;
    QVERIFY(!hasIssue(validateFootprint(fp), issue_code::kPadSpacing));
}

void TestFootprintValidation::structureErrorShortCircuits()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pins[0].padId = 999;
    const QVector<Issue> issues = validateFootprint(fp);
    QCOMPARE(issues.size(), 1);
    QCOMPARE(issues[0].code, issue_code::kStructure);
}

void TestFootprintValidation::unreferencedPadIsWarning()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pins.removeLast();
    const QVector<Issue> issues = validateFootprint(fp);
    QVERIFY(hasIssue(issues, issue_code::kPadUnreferenced));
    for (const Issue& issue : issues) {
        if (issue.code == issue_code::kPadUnreferenced) {
            QVERIFY(issue.severity == IssueSeverity::Warning);
        }
    }
}

void TestFootprintValidation::duplicatePadNumber()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pads[1].number = fp.pads[0].number;
    QVERIFY(hasIssue(validateFootprint(fp), issue_code::kPadNumberDuplicate));
}

void TestFootprintValidation::holeDrillOutOfRange()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.holes.append(MountingHole{QPointF(0, 0), 3.0, false, 20.0});
    QVERIFY(hasIssue(validateFootprint(fp), issue_code::kHoleDrill));
}

void TestFootprintValidation::silkWidthTooSmall()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.silk[0]->width = 1.0;
    QVERIFY(hasIssue(validateFootprint(fp), issue_code::kSilkWidth));
}

void TestFootprintValidation::circleNotSquareIsWarning()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pads[1].size = QSizeF(63, 50);
    const QVector<Issue> issues = validateFootprint(fp);
    QVERIFY(hasIssue(issues, issue_code::kCircleNotSquare));
    for (const Issue& issue : issues) {
        if (issue.code == issue_code::kCircleNotSquare) {
            QVERIFY(issue.severity == IssueSeverity::Error);
        }
    }
}

void TestFootprintValidation::unsupportedShapeGivesWarning()
{
    Footprint fp;
    QVERIFY(loadDip8(fp));
    fp.pads[1].shape = PadShape::Oblong;
    const QVector<Issue> issues = validateFootprint(fp);
    QVERIFY(hasIssue(issues, issue_code::kPadSpacingUnchecked));
    QVERIFY(!hasIssue(issues, issue_code::kPadSpacing));
}

QTEST_APPLESS_MAIN(TestFootprintValidation)

#include "tst_footprint_validation.moc"