#pragma once
#include "types.h"
#include <memory>
namespace eda {
enum class PrimitiveKind { Line, Arc, Rect, Polygon, Text };
struct Primitive {
    virtual ~Primitive() = default;
    LayerId layer = kTopSilk;
    double width = 8; // mil
    bool filled = false;
    virtual PrimitiveKind kind() const = 0;
    virtual std::unique_ptr<Primitive> clone() const = 0;
};
template <class T, PrimitiveKind K> struct PrimitiveValue : Primitive {
    PrimitiveKind kind() const override {
        return K;
    }
    std::unique_ptr<Primitive> clone() const override {
        return std::make_unique<T>(static_cast<const T&>(*this));
    }
};
struct SilkLine : PrimitiveValue<SilkLine, PrimitiveKind::Line> {
    QPointF a, b;
};
struct SilkArc : PrimitiveValue<SilkArc, PrimitiveKind::Arc> {
    QPointF center;
    double radius = 50, startAngle = 0, spanAngle = 90; // degrees, CCW in model
};
struct SilkRect : PrimitiveValue<SilkRect, PrimitiveKind::Rect> {
    QRectF rect;
};
struct SilkPolygon : PrimitiveValue<SilkPolygon, PrimitiveKind::Polygon> {
    QPolygonF points;
};
struct SilkText : PrimitiveValue<SilkText, PrimitiveKind::Text> {
    QPointF pos;
    QString text;
    double height = 50, rotation = 0;
};
using PrimitivePtr = std::unique_ptr<Primitive>;
} // namespace eda
