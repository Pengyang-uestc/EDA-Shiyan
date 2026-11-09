#pragma once
#include "primitive.h"
#include "status.h"
#include <vector>
namespace eda {
struct Pad {
    PadId id = 0;
    int number = 1;
    PadShape shape = PadShape::Circle;
    QPointF pos; // center, mil, Y up
    QSizeF size{60, 60};
    double drill = 0, maskMargin = 4, clearance = 10, cornerRadius = 0; // lengths in mil
    LayerId layer = kTopCopper;
};
struct Pin {
    int number = 1;
    QString name;
    ElectricalType type = ElectricalType::Passive;
    PadId padId = 0;
};
struct MountingHole {
    QPointF pos;
    double drill = 100;
    bool plated = false;
    double keepout = 20; // mil, radial distance outward from hole edge
};
struct Footprint {
    QString id, name, units = QStringLiteral("mil");
    PadId nextPadId = 1; // monotonically increasing during this document lifetime
    QVector<Pad> pads;
    QVector<Pin> pins;
    QVector<MountingHole> holes;
    std::vector<PrimitivePtr> silk;
    Footprint() = default;
    Footprint(const Footprint&);
    Footprint& operator=(const Footprint&);
    Footprint(Footprint&&) noexcept = default;
    Footprint& operator=(Footprint&&) noexcept = default;
    // UI 线程；footprint-local/mil。只读借用，找不到返回 nullptr；容器修改后指针可能失效。
    const Pad* findPad(PadId id) const;
    // Same borrowing rules as findPad(); resolves through the Pin's stable PadId.
    const Pad* findPadForPin(int pinNumber) const;
    // UI 线程；传入值的所有权不保留，调用者无需提供 ID。用户编辑时由 B 的命令调用。
    // 成功修改模型并返回稳定 ID；失败返回中文 Status，模型不变。
    [[nodiscard]] Result<PadId> addPad(Pad pad); // failure leaves model unchanged
    // UI 线程；用户编辑时由命令调用；有引用或 ID 不存在则失败，不修改模型。
    [[nodiscard]] Status removePad(PadId id); // refuses deletion while a Pin references it
};
// UI 线程；mil/footprint-local；只读、非空引用，不保留所有权；返回中文结构错误。
[[nodiscard]] Status validateStructure(const Footprint&);
Footprint exampleFootprint(); // all primitive types; integration fixture, not a manufacturing template
} // namespace eda
