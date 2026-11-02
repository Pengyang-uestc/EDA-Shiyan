#include "footprint.h"
#include "io_internal.h"
#include <QSet>
#include <cmath>
#include <limits>
namespace eda {
Footprint::Footprint(const Footprint& v)
    : id(v.id), name(v.name), units(v.units), nextPadId(v.nextPadId), pads(v.pads), pins(v.pins), holes(v.holes) {
    for (const auto& p : v.silk)
        silk.push_back(p ? p->clone() : nullptr);
}
Footprint& Footprint::operator=(const Footprint& v) {
    if (this != &v) {
        Footprint copy(v);
        *this = std::move(copy);
    }
    return *this;
}
const Pad* Footprint::findPad(PadId value) const {
    for (const auto& p : pads)
        if (p.id == value)
            return &p;
    return nullptr;
}
Result<PadId> Footprint::addPad(Pad p) {
    if (!nextPadId || nextPadId == std::numeric_limits<PadId>::max() || findPad(nextPadId))
        return {Status::failure(QStringLiteral("焊盘 ID 已耗尽或分配器状态错误")), std::nullopt};
    p.id = nextPadId;
    pads.append(p);
    ++nextPadId;
    return {Status::success(), p.id};
}
Status Footprint::removePad(PadId value) {
    for (const auto& p : pins)
        if (p.padId == value)
            return Status::failure(QStringLiteral("焊盘仍被引脚引用"));
    for (qsizetype i = 0; i < pads.size(); ++i)
        if (pads[i].id == value) {
            pads.removeAt(i);
            return Status::success();
        }
    return Status::failure(QStringLiteral("未找到指定焊盘"));
}
bool detail::validateStructureImpl(const Footprint& f, QString* error) {
    if (error)
        error->clear();
    auto fail = [&](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    auto nonneg = [](double n) { return std::isfinite(n) && n >= 0; };
    auto positive = [](double n) { return std::isfinite(n) && n > 0; };
    auto point = [](QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); };
    if (f.id.isEmpty() || f.name.isEmpty() || f.units != "mil")
        return fail(QStringLiteral("封装 ID 和名称不能为空，单位必须为 mil"));
    QSet<PadId> ids;
    QSet<int> pins;
    if (!f.nextPadId)
        return fail(QStringLiteral("nextPadId 必须为正整数"));
    for (const auto& p : f.pads) {
        if (!p.id || ids.contains(p.id) || p.id >= f.nextPadId)
            return fail(QStringLiteral("焊盘 ID 重复、非法或 nextPadId 不大于已有 ID"));
        ids.insert(p.id);
        if (!point(p.pos) || !positive(p.size.width()) || !positive(p.size.height()) || !nonneg(p.drill) ||
            !nonneg(p.maskMargin) || !nonneg(p.clearance) || p.layer < 0 || int(p.shape) < 0 || int(p.shape) > 3 ||
            p.number < 1 || !nonneg(p.cornerRadius) ||
            (p.shape == PadShape::RoundRect
                 ? (p.cornerRadius <= 0 || p.cornerRadius > qMin(p.size.width(), p.size.height()) / 2)
                 : p.cornerRadius != 0))
            return fail(QStringLiteral("焊盘坐标、尺寸、编号或枚举值非法"));
    }
    for (const auto& p : f.pins) {
        if (p.number < 1 || pins.contains(p.number) || !ids.contains(p.padId) || int(p.type) < 0 || int(p.type) > 5)
            return fail(QStringLiteral("引脚编号重复、类型非法或引用的焊盘不存在"));
        pins.insert(p.number);
    }
    for (const auto& h : f.holes)
        if (!point(h.pos) || !positive(h.drill) || !nonneg(h.keepout))
            return fail(QStringLiteral("安装孔坐标、孔径或禁布参数非法"));
    for (const auto& p : f.silk) {
        if (!p || p->layer < 0 || !positive(p->width))
            return fail(QStringLiteral("丝印图元为空或样式参数非法"));
        switch (p->kind()) {
        case PrimitiveKind::Line: {
            const auto* v = dynamic_cast<const SilkLine*>(p.get());
            if (!v || !point(v->a) || !point(v->b))
                return fail(QStringLiteral("线段坐标非法"));
            break;
        }
        case PrimitiveKind::Arc: {
            const auto* v = dynamic_cast<const SilkArc*>(p.get());
            if (!v || !point(v->center) || !positive(v->radius) || !std::isfinite(v->startAngle) ||
                !std::isfinite(v->spanAngle))
                return fail(QStringLiteral("圆弧参数非法"));
            break;
        }
        case PrimitiveKind::Rect: {
            const auto* v = dynamic_cast<const SilkRect*>(p.get());
            if (!v || !point(v->rect.topLeft()) || !positive(v->rect.width()) || !positive(v->rect.height()))
                return fail(QStringLiteral("矩形参数非法"));
            break;
        }
        case PrimitiveKind::Polygon: {
            const auto* v = dynamic_cast<const SilkPolygon*>(p.get());
            if (!v || v->points.size() < 3)
                return fail(QStringLiteral("多边形至少需要三个顶点"));
            for (auto pt : v->points)
                if (!point(pt))
                    return fail(QStringLiteral("多边形顶点非法"));
            break;
        }
        case PrimitiveKind::Text: {
            const auto* v = dynamic_cast<const SilkText*>(p.get());
            if (!v || !point(v->pos) || !positive(v->height) || !std::isfinite(v->rotation))
                return fail(QStringLiteral("文本参数非法"));
            break;
        }
        default:
            return fail(QStringLiteral("未知丝印图元类型"));
        }
    }
    return true;
}
Footprint exampleFootprint() {
    Footprint f;
    f.id = "example";
    f.name = QStringLiteral("集成测试封装");
    for (int i = 0; i < 4; ++i) {
        Pad p;
        p.number = i + 1;
        p.shape = PadShape(i);
        if (p.shape == PadShape::RoundRect)
            p.cornerRadius = 10;
        p.pos = QPointF(i * 100, 0);
        p.drill = 40;
        const auto added = f.addPad(p);
        Q_ASSERT(added.status.ok);
        const auto id = *added.value;
        f.pins.append(Pin{i + 1, QString::number(i + 1), ElectricalType::Passive, id});
    }
    f.holes.append(MountingHole{QPointF(400, 200), 100, true, 20});
    auto l = std::make_unique<SilkLine>();
    l->a = {0, 100};
    l->b = {300, 100};
    f.silk.push_back(std::move(l));
    auto a = std::make_unique<SilkArc>();
    a->center = {150, 150};
    f.silk.push_back(std::move(a));
    auto r = std::make_unique<SilkRect>();
    r->rect = QRectF(-50, -50, 400, 250);
    f.silk.push_back(std::move(r));
    auto p = std::make_unique<SilkPolygon>();
    p->points << QPointF(0, 0) << QPointF(20, 0) << QPointF(10, 20);
    p->filled = true;
    f.silk.push_back(std::move(p));
    auto t = std::make_unique<SilkText>();
    t->pos = {0, 250};
    t->text = QStringLiteral("中文 U1");
    f.silk.push_back(std::move(t));
    return f;
}
} // namespace eda
