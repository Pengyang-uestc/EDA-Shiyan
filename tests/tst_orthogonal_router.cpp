#include <QtTest>

#include "feat_pcb/orthogonal_router.h"

#include <cmath>
#include <limits>

using namespace eda;

namespace {

void comparePoints(const QVector<QPointF>& actual,
                   const QVector<QPointF>& expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (int i = 0; i < actual.size(); ++i) {
        QVERIFY2(std::abs(actual[i].x() - expected[i].x()) < 1e-6,
                 qPrintable(QStringLiteral("第 %1 个点 x 不符").arg(i)));
        QVERIFY2(std::abs(actual[i].y() - expected[i].y()) < 1e-6,
                 qPrintable(QStringLiteral("第 %1 个点 y 不符").arg(i)));
    }
}

} // namespace

class TestOrthogonalRouter : public QObject {
    Q_OBJECT

private slots:
    void samePoint();
    void sameX();
    void sameY();
    void approximateSameXSnaps();
    void approximateSameYSnaps();
    void horizontalThenVertical();
    void verticalThenHorizontal();
    void zHorizontal();
    void zHorizontalMidOutside();
    void zVertical();
    void zMidNaN();
    void zMidInfinity();
    void zMidAtEndpoint();
    void allSegmentsOrthogonal();
    void noAdjacentDuplicates();
    void nonFiniteCoordinateReturnsEmpty();
    void invalidEnumReturnsEmpty();
    void errorCodesAreReported();
};

void TestOrthogonalRouter::samePoint()
{
    comparePoints(orthogonalPath(QPointF(10, 20), QPointF(10, 20), OrthoMode::HV),
                  {QPointF(10, 20)});
}

void TestOrthogonalRouter::sameX()
{
    comparePoints(orthogonalPath(QPointF(10, 0), QPointF(10, 20), OrthoMode::Z_H),
                  {QPointF(10, 0), QPointF(10, 20)});
}

void TestOrthogonalRouter::sameY()
{
    comparePoints(orthogonalPath(QPointF(0, 10), QPointF(20, 10), OrthoMode::Z_V),
                  {QPointF(0, 10), QPointF(20, 10)});
}

void TestOrthogonalRouter::approximateSameXSnaps()
{
    const QVector<QPointF> points =
        orthogonalPath(QPointF(0, 0), QPointF(0.0005, 10), OrthoMode::HV);

    QCOMPARE(points.size(), 2);
    QVERIFY(std::abs(points[1].x() - points[0].x()) < 1e-12);
}

void TestOrthogonalRouter::approximateSameYSnaps()
{
    const QVector<QPointF> points =
        orthogonalPath(QPointF(0, 0), QPointF(20, 0.0005), OrthoMode::HV);
    QCOMPARE(points.size(), 2);
    QVERIFY(std::abs(points[1].y() - points[0].y()) < 1e-12);
}

void TestOrthogonalRouter::horizontalThenVertical()
{
    comparePoints(orthogonalPath(QPointF(0, 0), QPointF(20, 10), OrthoMode::HV),
                  {QPointF(0, 0), QPointF(20, 0), QPointF(20, 10)});
}

void TestOrthogonalRouter::verticalThenHorizontal()
{
    comparePoints(orthogonalPath(QPointF(0, 0), QPointF(20, 10), OrthoMode::VH),
                  {QPointF(0, 0), QPointF(0, 10), QPointF(20, 10)});
}

void TestOrthogonalRouter::zHorizontal()
{
    comparePoints(orthogonalPath(QPointF(0, 0), QPointF(20, 10),
                                 OrthoMode::Z_H, 5.0),
                  {QPointF(0, 0), QPointF(5, 0),
                   QPointF(5, 10), QPointF(20, 10)});
}

void TestOrthogonalRouter::zHorizontalMidOutside()
{
    // mid = -5 会夹到 0，退化成 VH。
    comparePoints(orthogonalPath(QPointF(0, 0), QPointF(20, 10),
                                 OrthoMode::Z_H, -5.0),
                  {QPointF(0, 0), QPointF(0, 10), QPointF(20, 10)});
}

void TestOrthogonalRouter::zVertical()
{
    comparePoints(orthogonalPath(QPointF(0, 0), QPointF(20, 10),
                                 OrthoMode::Z_V, 4.0),
                  {QPointF(0, 0), QPointF(0, 4),
                   QPointF(20, 4), QPointF(20, 10)});
}

void TestOrthogonalRouter::zMidNaN()
{
    comparePoints(orthogonalPath(QPointF(0, 0), QPointF(20, 10),
                                 OrthoMode::Z_H,
                                 std::numeric_limits<double>::quiet_NaN()),
                  {QPointF(0, 0), QPointF(10, 0),
                   QPointF(10, 10), QPointF(20, 10)});
}

void TestOrthogonalRouter::zMidInfinity()
{
    comparePoints(orthogonalPath(QPointF(0, 0), QPointF(20, 10),
                                 OrthoMode::Z_H,
                                 std::numeric_limits<double>::infinity()),
                  {QPointF(0, 0), QPointF(10, 0),
                   QPointF(10, 10), QPointF(20, 10)});
}

void TestOrthogonalRouter::zMidAtEndpoint()
{
    comparePoints(orthogonalPath(QPointF(0, 0), QPointF(20, 10),
                                 OrthoMode::Z_H, 0.0),
                  {QPointF(0, 0), QPointF(0, 10), QPointF(20, 10)});
}

void TestOrthogonalRouter::allSegmentsOrthogonal()
{
    const QVector<QPointF> points =
        orthogonalPath(QPointF(0, 0), QPointF(20, 10), OrthoMode::Z_H, 7.0);

    for (int i = 1; i < points.size(); ++i) {
        const double dx = std::abs(points[i].x() - points[i - 1].x());
        const double dy = std::abs(points[i].y() - points[i - 1].y());
        QVERIFY(dx < 1e-12 || dy < 1e-12);
    }
}

void TestOrthogonalRouter::noAdjacentDuplicates()
{
    const QVector<QPointF> points =
        orthogonalPath(QPointF(0, 0), QPointF(20, 10), OrthoMode::Z_H, 0.0);

    for (int i = 1; i < points.size(); ++i) {
        const bool same = std::abs(points[i].x() - points[i - 1].x()) < 1e-12
                          && std::abs(points[i].y() - points[i - 1].y()) < 1e-12;
        QVERIFY(!same);
    }
}

void TestOrthogonalRouter::nonFiniteCoordinateReturnsEmpty()
{
    QVERIFY(orthogonalPath(QPointF(std::numeric_limits<double>::quiet_NaN(), 0),
                           QPointF(20, 10), OrthoMode::HV).isEmpty());
    QVERIFY(orthogonalPath(QPointF(0, 0),
                           QPointF(20, std::numeric_limits<double>::infinity()),
                           OrthoMode::HV).isEmpty());
}

void TestOrthogonalRouter::invalidEnumReturnsEmpty()
{
    QVERIFY(orthogonalPath(QPointF(0, 0), QPointF(20, 10),
                           static_cast<OrthoMode>(42)).isEmpty());
}

void TestOrthogonalRouter::errorCodesAreReported()
{
    RouteError error = RouteError::None;
    QVERIFY(orthogonalPath(QPointF(std::numeric_limits<double>::quiet_NaN(), 0),
                           QPointF(20, 10), OrthoMode::HV, std::nullopt,
                           &error).isEmpty());
    QVERIFY(error == RouteError::InvalidCoordinate);

    error = RouteError::None;
    QVERIFY(orthogonalPath(QPointF(0, 0), QPointF(20, 10),
                           static_cast<OrthoMode>(42), std::nullopt,
                           &error).isEmpty());
    QVERIFY(error == RouteError::InvalidMode);
}

QTEST_APPLESS_MAIN(TestOrthogonalRouter)

#include "tst_orthogonal_router.moc"