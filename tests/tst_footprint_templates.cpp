#include <QtTest>

#include "core/io.h"
#include "core/validation.h"
#include "feat_fp/footprint_templates.h"
#include "feat_fp/footprint_validation.h"

using namespace eda;

namespace {

bool requireOk(const Result<Footprint>& result)
{
    return result.status.ok && result.value.has_value();
}

bool hasError(const QVector<Issue>& issues)
{
    for (const Issue& issue : issues) {
        if (issue.severity == IssueSeverity::Error) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestFootprintTemplates : public QObject {
    Q_OBJECT

private slots:
    void dip8Geometry();
    void dip16Geometry();
    void radGeometry();
    void axialGeometry();
    void allTemplatesPassManufacturingRules();
    void allTemplatesRoundTripThroughFileFormat();
    void exampleFootprintHasNoErrors();
};

void TestFootprintTemplates::dip8Geometry()
{
    const Result<Footprint> result = makeDip8Footprint();
    QVERIFY2(requireOk(result), qPrintable(result.status.message));
    const Footprint& fp = *result.value;

    QCOMPARE(fp.id, QStringLiteral("dip8_w762"));
    QCOMPARE(fp.pads.size(), 8);
    QCOMPARE(fp.pins.size(), 8);
    QCOMPARE(fp.silk.size(), std::size_t(3));
    QCOMPARE(fp.nextPadId, PadId(9));

    QCOMPARE(fp.pads[0].id, PadId(1));
    QCOMPARE(fp.pads[0].number, 1);
    QVERIFY(fp.pads[0].shape == PadShape::Rectangle);
    QCOMPARE(fp.pads[0].pos, QPointF(-150, 150));
    QCOMPARE(fp.pads[0].drill, 32.0);
    QVERIFY(fp.pads[1].shape == PadShape::Circle);
    QCOMPARE(fp.pads[7].pos, QPointF(150, 150));
    QCOMPARE(fp.pins[7].padId, fp.pads[7].id);
}

void TestFootprintTemplates::dip16Geometry()
{
    const Result<Footprint> result = makeDip16Footprint();
    QVERIFY2(requireOk(result), qPrintable(result.status.message));
    const Footprint& fp = *result.value;

    QCOMPARE(fp.pads.size(), 16);
    QCOMPARE(fp.pins.size(), 16);
    QCOMPARE(fp.nextPadId, PadId(17));
    QCOMPARE(fp.pads[0].pos, QPointF(-150, 350));
    QCOMPARE(fp.pads[7].pos, QPointF(-150, -350));
    QCOMPARE(fp.pads[8].pos, QPointF(150, -350));
    QCOMPARE(fp.pads[15].pos, QPointF(150, 350));
    QVERIFY(fp.pads[0].shape == PadShape::Rectangle);
    QVERIFY(fp.pads[15].shape == PadShape::Circle);
}

void TestFootprintTemplates::radGeometry()
{
    const Result<Footprint> result = makeRad01Footprint();
    QVERIFY2(requireOk(result), qPrintable(result.status.message));
    const Footprint& fp = *result.value;

    QCOMPARE(fp.pads.size(), 2);
    QCOMPARE(fp.nextPadId, PadId(3));
    QCOMPARE(fp.pads[0].pos, QPointF(-50, 0));
    QCOMPARE(fp.pads[1].pos, QPointF(50, 0));
    QCOMPARE(fp.pads[0].size, QSizeF(71, 71));
    QCOMPARE(fp.pads[0].drill, 36.0);
}

void TestFootprintTemplates::axialGeometry()
{
    const Result<Footprint> result = makeAxial04Footprint();
    QVERIFY2(requireOk(result), qPrintable(result.status.message));
    const Footprint& fp = *result.value;

    QCOMPARE(fp.pads.size(), 2);
    QCOMPARE(fp.nextPadId, PadId(3));
    QCOMPARE(fp.pads[0].pos, QPointF(-200, 0));
    QCOMPARE(fp.pads[1].pos, QPointF(200, 0));
    QCOMPARE(fp.silk.size(), std::size_t(2));
}

void TestFootprintTemplates::allTemplatesPassManufacturingRules()
{
    const QVector<Result<Footprint>> results{
        makeDip8Footprint(), makeDip16Footprint(),
        makeRad01Footprint(), makeAxial04Footprint()};

    for (const Result<Footprint>& result : results) {
        QVERIFY2(requireOk(result), qPrintable(result.status.message));
        const QVector<Issue> issues = validateFootprint(*result.value);
        QVERIFY2(issues.isEmpty(), qPrintable(issues.isEmpty()
                                                 ? QString()
                                                 : issues.first().message));
    }
}

void TestFootprintTemplates::allTemplatesRoundTripThroughFileFormat()
{
    const QVector<Result<Footprint>> results{
        makeDip8Footprint(), makeDip16Footprint(),
        makeRad01Footprint(), makeAxial04Footprint()};

    for (const Result<Footprint>& result : results) {
        QVERIFY2(requireOk(result), qPrintable(result.status.message));
        const Footprint& original = *result.value;

        const Result<QByteArray> encoded = encodeFootprint(original);
        QVERIFY2(encoded.status.ok, qPrintable(encoded.status.message));

        const Result<Footprint> decoded = decodeFootprint(*encoded.value);
        QVERIFY2(decoded.status.ok, qPrintable(decoded.status.message));

        QCOMPARE(decoded.value->id, original.id);
        QCOMPARE(decoded.value->name, original.name);
        QCOMPARE(decoded.value->units, original.units);
        QCOMPARE(decoded.value->nextPadId, original.nextPadId);
        QCOMPARE(decoded.value->pads.size(), original.pads.size());
        QCOMPARE(decoded.value->pins.size(), original.pins.size());
        QCOMPARE(decoded.value->silk.size(), original.silk.size());

        for (int i = 0; i < original.pads.size(); ++i) {
            const Pad& a = original.pads[i];
            const Pad& b = decoded.value->pads[i];
            QCOMPARE(b.id, a.id);
            QCOMPARE(b.number, a.number);
            QVERIFY(b.shape == a.shape);
            QCOMPARE(b.pos, a.pos);
            QCOMPARE(b.size, a.size);
            QCOMPARE(b.drill, a.drill);
            QCOMPARE(b.maskMargin, a.maskMargin);
            QCOMPARE(b.clearance, a.clearance);
            QCOMPARE(b.cornerRadius, a.cornerRadius);
            QVERIFY(b.layer == a.layer);
        }

        for (int i = 0; i < original.pins.size(); ++i) {
            const Pin& a = original.pins[i];
            const Pin& b = decoded.value->pins[i];
            QCOMPARE(b.number, a.number);
            QCOMPARE(b.name, a.name);
            QVERIFY(b.type == a.type);
            QCOMPARE(b.padId, a.padId);
        }

        for (std::size_t i = 0; i < original.silk.size(); ++i) {
            QVERIFY(decoded.value->silk[i]->kind() == original.silk[i]->kind());
            QVERIFY(decoded.value->silk[i]->layer == original.silk[i]->layer);
            QCOMPARE(decoded.value->silk[i]->width, original.silk[i]->width);
            QCOMPARE(decoded.value->silk[i]->filled, original.silk[i]->filled);
        }
    }
}

void TestFootprintTemplates::exampleFootprintHasNoErrors()
{
    const QVector<Issue> issues = validateFootprint(exampleFootprint());
    QVERIFY(!hasError(issues));
}

QTEST_APPLESS_MAIN(TestFootprintTemplates)

#include "tst_footprint_templates.moc"