#include "feat_fp/footprint_templates.h"

#include "core/primitive.h"
#include "feat_fp/footprint_validation.h"

#include <QString>

#include <array>
#include <memory>
#include <utility>

namespace eda {
namespace {

constexpr double kPadDiameter = 63.0;
constexpr double kDrillDiameter = 32.0;
constexpr double kSmallPadDiameter = 71.0;
constexpr double kSmallDrillDiameter = 36.0;
constexpr double kMaskMargin = 4.0;
constexpr double kClearance = 10.0;
constexpr double kSilkWidth = 10.0;

Status addThroughHolePad(Footprint& footprint, int number, QPointF position,
                         PadShape shape, double padDiameter, double drillDiameter)
{
    Pad pad;
    pad.number = number;
    pad.shape = shape;
    pad.pos = position;
    pad.size = QSizeF(padDiameter, padDiameter);
    pad.drill = drillDiameter;
    pad.maskMargin = kMaskMargin;
    pad.clearance = kClearance;
    pad.layer = kTopCopper;

    const Result<PadId> added = footprint.addPad(std::move(pad));
    if (!added.status.ok) {
        return added.status;
    }

    footprint.pins.append(Pin{number, QString::number(number),
                              ElectricalType::Passive, *added.value});
    return Status::success();
}

void addSilkRect(Footprint& footprint, const QRectF& rect)
{
    auto item = std::make_unique<SilkRect>();
    item->rect = rect;
    item->width = kSilkWidth;
    footprint.silk.push_back(std::move(item));
}

void addSilkArc(Footprint& footprint, QPointF center, double radius,
                double startAngle, double spanAngle)
{
    auto item = std::make_unique<SilkArc>();
    item->center = center;
    item->radius = radius;
    item->startAngle = startAngle;
    item->spanAngle = spanAngle;
    item->width = kSilkWidth;
    footprint.silk.push_back(std::move(item));
}

void addSilkText(Footprint& footprint, QPointF position, const QString& text)
{
    auto item = std::make_unique<SilkText>();
    item->pos = position;
    item->text = text;
    item->height = 50.0;
    item->rotation = 0.0;
    // D4 的 silk text 记录不保存线宽，保持 Primitive 默认值即可。
    footprint.silk.push_back(std::move(item));
}

Result<Footprint> finish(Footprint footprint)
{
    const QVector<Issue> issues = validateFootprint(footprint);
    for (const Issue& issue : issues) {
        if (issue.severity == IssueSeverity::Error) {
            return {Status::failure(issue.message), std::nullopt};
        }
    }
    return {Status::success(), std::move(footprint)};
}

struct PadSpec {
    int number;
    QPointF position;
    PadShape shape;
};

Result<Footprint> makeDip(const QString& id, const QString& name,
                          const QVector<PadSpec>& specs, double bodyHeight,
                          const QString& label)
{
    Footprint footprint;
    footprint.id = id;
    footprint.name = name;
    footprint.units = QStringLiteral("mil");

    for (const PadSpec& spec : specs) {
        const Status added = addThroughHolePad(
            footprint, spec.number, spec.position, spec.shape,
            kPadDiameter, kDrillDiameter);
        if (!added.ok) {
            return {added, std::nullopt};
        }
    }

    addSilkRect(footprint, QRectF(-120.0, -bodyHeight / 2.0, 240.0, bodyHeight));
    addSilkArc(footprint, QPointF(0.0, bodyHeight / 2.0), 20.0, 180.0, 180.0);
    addSilkText(footprint, QPointF(0.0, 0.0), label);

    return finish(std::move(footprint));
}

} // namespace

Result<Footprint> makeDip8Footprint()
{
    const QVector<PadSpec> specs{
        {1, QPointF(-150, 150), PadShape::Rectangle},
        {2, QPointF(-150, 50), PadShape::Circle},
        {3, QPointF(-150, -50), PadShape::Circle},
        {4, QPointF(-150, -150), PadShape::Circle},
        {5, QPointF(150, -150), PadShape::Circle},
        {6, QPointF(150, -50), PadShape::Circle},
        {7, QPointF(150, 50), PadShape::Circle},
        {8, QPointF(150, 150), PadShape::Circle},
    };
    return makeDip(QStringLiteral("dip8_w762"),
                   QStringLiteral("DIP-8 W7.62mm"), specs, 400.0,
                   QStringLiteral("DIP-8"));
}

Result<Footprint> makeDip16Footprint()
{
    QVector<PadSpec> specs;
    specs.reserve(16);
    for (int i = 0; i < 8; ++i) {
        specs.append({i + 1, QPointF(-150, 350 - 100 * i),
                      i == 0 ? PadShape::Rectangle : PadShape::Circle});
    }
    for (int i = 0; i < 8; ++i) {
        specs.append({9 + i, QPointF(150, -350 + 100 * i),
                      PadShape::Circle});
    }
    return makeDip(QStringLiteral("dip16_w762"),
                   QStringLiteral("DIP-16 W7.62mm"), specs, 800.0,
                   QStringLiteral("DIP-16"));
}

Result<Footprint> makeRad01Footprint()
{
    Footprint footprint;
    footprint.id = QStringLiteral("rad01");
    footprint.name = QStringLiteral("RAD-0.1");
    footprint.units = QStringLiteral("mil");

    for (const PadSpec& spec : std::array<PadSpec, 2>{
             PadSpec{1, QPointF(-50, 0), PadShape::Rectangle},
             PadSpec{2, QPointF(50, 0), PadShape::Circle}}) {
        const Status added = addThroughHolePad(
            footprint, spec.number, spec.position, spec.shape,
            kSmallPadDiameter, kSmallDrillDiameter);
        if (!added.ok) {
            return {added, std::nullopt};
        }
    }

    addSilkArc(footprint, QPointF(0, 0), 80.0, 0.0, 360.0);
    addSilkText(footprint, QPointF(0, 110), QStringLiteral("RAD-0.1"));

    return finish(std::move(footprint));
}

Result<Footprint> makeAxial04Footprint()
{
    Footprint footprint;
    footprint.id = QStringLiteral("axial04");
    footprint.name = QStringLiteral("AXIAL-0.4");
    footprint.units = QStringLiteral("mil");

    for (const PadSpec& spec : std::array<PadSpec, 2>{
             PadSpec{1, QPointF(-200, 0), PadShape::Rectangle},
             PadSpec{2, QPointF(200, 0), PadShape::Circle}}) {
        const Status added = addThroughHolePad(
            footprint, spec.number, spec.position, spec.shape,
            kSmallPadDiameter, kSmallDrillDiameter);
        if (!added.ok) {
            return {added, std::nullopt};
        }
    }

    addSilkRect(footprint, QRectF(-125, -50, 250, 100));
    addSilkText(footprint, QPointF(0, 90), QStringLiteral("AXIAL-0.4"));

    return finish(std::move(footprint));
}

} // namespace eda