#include "feat_fp/footprint_validation.h"

#include "core/status.h"
#include "core/manufacturing_rules.h"

#include <QSet>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace eda {
namespace {

Issue makeIssue(IssueSeverity severity, const QString& code,
                const QString& message, PadId padId = kInvalidId,
                PadId relatedPadId = kInvalidId, int pinNumber = -1)
{
    Issue issue;
    issue.severity = severity;
    issue.code = code;
    issue.message = message;
    issue.padId = padId;
    issue.relatedPadId = relatedPadId;
    issue.pinNumber = pinNumber;
    return issue;
}

QRectF padBounds(const Pad& pad)
{
    return QRectF(pad.pos.x() - pad.size.width() / 2.0,
                  pad.pos.y() - pad.size.height() / 2.0,
                  pad.size.width(),
                  pad.size.height());
}

double pointRectDistance(const QPointF& point, const QRectF& rect)
{
    const double dx = std::max({rect.left() - point.x(), 0.0,
                                point.x() - rect.right()});
    const double dy = std::max({rect.top() - point.y(), 0.0,
                                point.y() - rect.bottom()});
    return std::hypot(dx, dy);
}

double rectGap(const QRectF& a, const QRectF& b)
{
    const double dx = std::max({a.left() - b.right(), 0.0,
                                b.left() - a.right()});
    const double dy = std::max({a.top() - b.bottom(), 0.0,
                                b.top() - a.bottom()});
    return std::hypot(dx, dy);
}

bool exactSpacingShape(PadShape shape)
{
    return shape == PadShape::Circle || shape == PadShape::Rectangle;
}

// 只对圆形/矩形调用；圆形精确，矩形用轴对齐包围盒精确。
double padGap(const Pad& a, const Pad& b)
{
    const bool aCircle = a.shape == PadShape::Circle;
    const bool bCircle = b.shape == PadShape::Circle;
    const double aRadius = std::min(a.size.width(), a.size.height()) / 2.0;
    const double bRadius = std::min(b.size.width(), b.size.height()) / 2.0;

    if (aCircle && bCircle) {
        return std::hypot(a.pos.x() - b.pos.x(), a.pos.y() - b.pos.y())
               - aRadius - bRadius;
    }
    if (aCircle) {
        return pointRectDistance(a.pos, padBounds(b)) - aRadius;
    }
    if (bCircle) {
        return pointRectDistance(b.pos, padBounds(a)) - bRadius;
    }
    return rectGap(padBounds(a), padBounds(b));
}

} // namespace

QVector<Issue> validateFootprint(const Footprint& footprint)
{
    QVector<Issue> issues;

    const Status structure = validateStructure(footprint);
    if (!structure.ok) {
        issues.append(makeIssue(IssueSeverity::Error,
                                issue_code::kStructure, structure.message));
        return issues;
    }

    QSet<int> padNumbers;
    QSet<PadId> referencedPads;

    for (const Pad& pad : footprint.pads) {
        if (padNumbers.contains(pad.number)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kPadNumberDuplicate,
                QStringLiteral("焊盘编号 %1 重复").arg(pad.number), pad.id));
        } else {
            padNumbers.insert(pad.number);
        }

        if (pad.shape == PadShape::Circle
            && std::abs(pad.size.width() - pad.size.height()) > kEpsilonMil) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kCircleNotSquare,
                QStringLiteral("圆形焊盘 %1 的宽高不相等").arg(pad.number),
                pad.id));
        }

        if (pad.drill > 0.0) {
            if (!validation::drillOk(pad.drill)) {
                issues.append(makeIssue(
                    IssueSeverity::Error, issue_code::kDrill,
                    QStringLiteral("焊盘钻孔 %1 mil 超出 0.2–6.0 mm 范围")
                        .arg(pad.drill, 0, 'f', 3),
                    pad.id));
            }
            if (!validation::annularRingOk(pad.size.width(), pad.size.height(),
                                           pad.drill)) {
                issues.append(makeIssue(
                    IssueSeverity::Error, issue_code::kAnnularRing,
                    QStringLiteral("焊盘环宽不足：短边 %1 mil，钻孔 %2 mil")
                        .arg(std::min(pad.size.width(), pad.size.height()), 0, 'f', 3)
                        .arg(pad.drill, 0, 'f', 3),
                    pad.id));
            }
        }

        if (!validation::maskMarginOk(pad.maskMargin)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kMaskMargin,
                QStringLiteral("阻焊开窗 %1 mil 超出 0–0.2 mm 范围")
                    .arg(pad.maskMargin, 0, 'f', 3),
                pad.id));
        }

        if (!validation::clearanceOk(pad.clearance)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kClearance,
                QStringLiteral("安全间距 %1 mil 小于 0.2 mm")
                    .arg(pad.clearance, 0, 'f', 3),
                pad.id));
        }
    }

    for (const Pin& pin : footprint.pins) {
        if (referencedPads.contains(pin.padId)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kPinPadDuplicate,
                QStringLiteral("焊盘被多个引脚引用：引脚 %1").arg(pin.number),
                pin.padId, kInvalidId, pin.number));
        } else {
            referencedPads.insert(pin.padId);
        }
    }

    for (int i = 0; i < footprint.pads.size(); ++i) {
        for (int j = i + 1; j < footprint.pads.size(); ++j) {
            const Pad& a = footprint.pads[i];
            const Pad& b = footprint.pads[j];
            if (a.layer != b.layer) {
                continue;
            }

            if (!exactSpacingShape(a.shape) || !exactSpacingShape(b.shape)) {
                issues.append(makeIssue(
                    IssueSeverity::Warning, issue_code::kPadSpacingUnchecked,
                    QStringLiteral("焊盘 %1 与 %2 的形状暂不做精确间距检查")
                        .arg(a.number)
                        .arg(b.number),
                    a.id, b.id));
                continue;
            }

            const double required = std::max(a.clearance, b.clearance);
            const double gap = padGap(a, b);
            if (gap + kEpsilonMil < required) {
                issues.append(makeIssue(
                    IssueSeverity::Error, issue_code::kPadSpacing,
                    QStringLiteral("焊盘 %1 与 %2 边缘间距 %3 mil，小于 %4 mil")
                        .arg(a.number)
                        .arg(b.number)
                        .arg(gap, 0, 'f', 3)
                        .arg(required, 0, 'f', 3),
                    a.id, b.id));
            }
        }
    }

    for (const MountingHole& hole : footprint.holes) {
        if (!validation::drillOk(hole.drill)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kHoleDrill,
                QStringLiteral("安装孔孔径 %1 mil 超出 0.2–6.0 mm 范围")
                    .arg(hole.drill, 0, 'f', 3)));
        }
    }

    for (const auto& primitive : footprint.silk) {
        if (primitive && !validation::silkWidthOk(primitive->width)) {
            issues.append(makeIssue(
                IssueSeverity::Error, issue_code::kSilkWidth,
                QStringLiteral("丝印线宽 %1 mil 小于 6 mil")
                    .arg(primitive->width, 0, 'f', 3)));
        }
    }

    for (const Pad& pad : footprint.pads) {
        if (!referencedPads.contains(pad.id)) {
            issues.append(makeIssue(
                IssueSeverity::Warning, issue_code::kPadUnreferenced,
                QStringLiteral("焊盘 %1 没有被任何引脚引用").arg(pad.number),
                pad.id));
        }
    }

    return issues;
}

} // namespace eda