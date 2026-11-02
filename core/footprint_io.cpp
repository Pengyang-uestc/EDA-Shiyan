#include "io.h"
#include "io_internal.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>
#include <cmath>
#include <limits>

namespace eda::detail {
namespace {
struct ParseFailure {
    QString message;
};
bool fail(QString* e, const QString& s) {
    if (e)
        *e = s;
    return false;
}
[[noreturn]] void bad(const QString& s) {
    throw ParseFailure{s};
}

QString quote(const QString& s) {
    QString out = QStringLiteral("\"");
    for (QChar c : s) {
        if (c == '\\' || c == '"')
            out += '\\';
        if (c == '\n')
            out += QStringLiteral("\\n");
        else if (c == '\r')
            out += QStringLiteral("\\r");
        else
            out += c;
    }
    return out + '"';
}
QString token(const QString& s) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_+-]+$"));
    return re.match(s).hasMatch() ? s : quote(s);
}
QString realText(double n) {
    if (!std::isfinite(n))
        bad(QStringLiteral("数字不是有限值"));
    return QString::number(n, 'f', 3);
}
QVector<QString> lex(const QString& s) {
    QVector<QString> out;
    qsizetype i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i].isSpace())
            ++i;
        if (i == s.size())
            break;
        QString v;
        if (s[i] == '"') {
            ++i;
            bool closed = false;
            while (i < s.size()) {
                QChar c = s[i++];
                if (c == '"') {
                    closed = true;
                    break;
                }
                if (c == '\\' && i < s.size()) {
                    c = s[i++];
                    if (c == 'n')
                        v += '\n';
                    else if (c == 'r')
                        v += '\r';
                    else if (c == '\\' || c == '"')
                        v += c;
                    else
                        bad(QStringLiteral("字符串转义非法"));
                } else
                    v += c;
            }
            if (!closed || (i < s.size() && !s[i].isSpace()))
                bad(QStringLiteral("字符串未闭合或后缺空白"));
        } else {
            qsizetype b = i;
            while (i < s.size() && !s[i].isSpace())
                ++i;
            v = s.mid(b, i - b);
        }
        out.append(v);
    }
    return out;
}
void count(const QVector<QString>& v, qsizetype n) {
    if (v.size() != n)
        bad(QStringLiteral("字段数量不正确"));
}
double real(const QString& s) {
    static const QRegularExpression re(QStringLiteral("^-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?$"));
    bool ok = false;
    double n = s.toDouble(&ok);
    if (!ok || !re.match(s).hasMatch() || !std::isfinite(n))
        bad(QStringLiteral("数字格式非法"));
    return n;
}
int integer(const QString& s) {
    bool ok = false;
    int n = s.toInt(&ok);
    if (!ok)
        bad(QStringLiteral("整数格式非法"));
    return n;
}
quint64 id(const QString& s) {
    bool ok = false;
    quint64 n = s.toULongLong(&ok);
    if (!ok || n == kInvalidId)
        bad(QStringLiteral("ID 必须为正整数"));
    return n;
}
QString layerName(LayerId l) {
    switch (l) {
    case kTopCopper:
        return "top";
    case kBottomCopper:
        return "bottom";
    case kTopSilk:
        return "top_silk";
    case kBottomSilk:
        return "bottom_silk";
    case kTopMask:
        return "top_mask";
    case kBottomMask:
        return "bottom_mask";
    default:
        bad(QStringLiteral("封装层 ID 不支持"));
    }
}
LayerId layer(const QString& s) {
    if (s == "top")
        return kTopCopper;
    if (s == "bottom")
        return kBottomCopper;
    if (s == "top_silk")
        return kTopSilk;
    if (s == "bottom_silk")
        return kBottomSilk;
    if (s == "top_mask")
        return kTopMask;
    if (s == "bottom_mask")
        return kBottomMask;
    bad(QStringLiteral("未知层名称"));
}
QString shapeName(PadShape s) {
    switch (s) {
    case PadShape::Circle:
        return "circle";
    case PadShape::Rectangle:
        return "rect";
    case PadShape::Oblong:
        return "oval";
    case PadShape::RoundRect:
        return "roundrect";
    }
    bad(QStringLiteral("未知焊盘形状"));
}
PadShape shape(const QString& s) {
    if (s == "circle")
        return PadShape::Circle;
    if (s == "rect")
        return PadShape::Rectangle;
    if (s == "oval")
        return PadShape::Oblong;
    if (s == "roundrect")
        return PadShape::RoundRect;
    bad(QStringLiteral("未知焊盘形状"));
}
QString electricalName(ElectricalType e) {
    switch (e) {
    case ElectricalType::Input:
        return "input";
    case ElectricalType::Output:
        return "output";
    case ElectricalType::Bidirectional:
        return "bidir";
    case ElectricalType::Passive:
        return "passive";
    case ElectricalType::Power:
        return "power";
    case ElectricalType::NoConnect:
        return "nc";
    }
    bad(QStringLiteral("未知引脚类型"));
}
ElectricalType electrical(const QString& s) {
    if (s == "input")
        return ElectricalType::Input;
    if (s == "output")
        return ElectricalType::Output;
    if (s == "bidir")
        return ElectricalType::Bidirectional;
    if (s == "passive")
        return ElectricalType::Passive;
    if (s == "power")
        return ElectricalType::Power;
    if (s == "nc")
        return ElectricalType::NoConnect;
    bad(QStringLiteral("未知引脚类型"));
}
} // namespace

QByteArray encodeFootprintImpl(const Footprint& f) {
    QString s = QStringLiteral("EDA-FP 1\nunits mil\nid %1\nname %2\n").arg(token(f.id), quote(f.name));
    for (const auto& p : f.pads) {
        const QStringList fields{QString::number(p.id),     QString::number(p.number), shapeName(p.shape),
                                 realText(p.pos.x()),       realText(p.pos.y()),       realText(p.size.width()),
                                 realText(p.size.height()), realText(p.drill),         realText(p.maskMargin),
                                 realText(p.clearance),     realText(p.cornerRadius),  layerName(p.layer)};
        s += QStringLiteral("pad ") + fields.join(' ') + '\n';
    }
    for (const auto& p : f.pins)
        s += QStringLiteral("pin %1 %2 %3 %4\n")
                 .arg(QString::number(p.number), quote(p.name), electricalName(p.type), QString::number(p.padId));
    for (const auto& h : f.holes)
        s += QStringLiteral("hole %1 %2 %3 %4 %5\n")
                 .arg(realText(h.pos.x()), realText(h.pos.y()), realText(h.drill),
                      h.plated ? QStringLiteral("1") : QStringLiteral("0"), realText(h.keepout));
    for (const auto& p : f.silk)
        switch (p->kind()) {
        case PrimitiveKind::Line: {
            const auto& v = dynamic_cast<const SilkLine&>(*p);
            s += QStringLiteral("silk line %1 %2 %3 %4 %5 %6\n")
                     .arg(layerName(v.layer), realText(v.width), realText(v.a.x()), realText(v.a.y()),
                          realText(v.b.x()), realText(v.b.y()));
            break;
        }
        case PrimitiveKind::Arc: {
            const auto& v = dynamic_cast<const SilkArc&>(*p);
            s += QStringLiteral("silk arc %1 %2 %3 %4 %5 %6 %7\n")
                     .arg(layerName(v.layer), realText(v.width), realText(v.center.x()), realText(v.center.y()),
                          realText(v.radius), realText(v.startAngle), realText(v.spanAngle));
            break;
        }
        case PrimitiveKind::Rect: {
            const auto& v = dynamic_cast<const SilkRect&>(*p);
            s += QStringLiteral("silk rect %1 %2 %3 %4 %5 %6 %7\n")
                     .arg(layerName(v.layer), realText(v.width), realText(v.rect.x()), realText(v.rect.y()),
                          realText(v.rect.width()), realText(v.rect.height()),
                          v.filled ? QStringLiteral("1") : QStringLiteral("0"));
            break;
        }
        case PrimitiveKind::Polygon: {
            const auto& v = dynamic_cast<const SilkPolygon&>(*p);
            QString row =
                QStringLiteral("silk poly %1 %2 %3 %4")
                    .arg(layerName(v.layer), realText(v.width), v.filled ? QStringLiteral("1") : QStringLiteral("0"),
                         QString::number(v.points.size()));
            for (auto pt : v.points)
                row += QStringLiteral(" %1 %2").arg(realText(pt.x()), realText(pt.y()));
            s += row + '\n';
            break;
        }
        case PrimitiveKind::Text: {
            const auto& v = dynamic_cast<const SilkText&>(*p);
            s += QStringLiteral("silk text %1 %2 %3 %4 %5 %6\n")
                     .arg(layerName(v.layer), realText(v.pos.x()), realText(v.pos.y()), realText(v.height),
                          realText(v.rotation), quote(v.text));
            break;
        }
        }
    return (s + QStringLiteral("end\n")).toUtf8();
}

bool decodeFootprintImpl(const QByteArray& data, Footprint& output, QString* error) {
    if (error)
        error->clear();
    if (data.size() > 16 * 1024 * 1024)
        return fail(error, QStringLiteral("封装文件超过 16 MiB 上限"));
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString source = decoder(data);
    if (decoder.hasError())
        return fail(error, QStringLiteral("文件不是有效 UTF-8"));
    auto lines = source.split('\n');
    qsizetype first = 0;
    while (first < lines.size() && (lines[first].trimmed().isEmpty() || lines[first].trimmed().startsWith('#')))
        ++first;
    if (first == lines.size() || lex(lines[first]) != QVector<QString>{QStringLiteral("EDA-FP"), QStringLiteral("1")})
        return fail(error, QStringLiteral("缺少 EDA-FP 1 文件头"));
    Footprint f;
    bool units = false, hasId = false, hasName = false, ended = false;
    QSet<PadId> padIds;
    QSet<int> pinIds;
    qsizetype lineNo = first + 1;
    try {
        for (qsizetype i = first + 1; i < lines.size(); ++i) {
            lineNo = i + 1;
            auto text = lines[i].trimmed();
            if (text.isEmpty() || text.startsWith('#'))
                continue;
            auto v = lex(lines[i]);
            if (v.isEmpty())
                continue;
            if (ended)
                bad(QStringLiteral("end 后仍有数据"));
            const auto& tag = v[0];
            if (tag == "units") {
                count(v, 2);
                if (units || v[1] != "mil")
                    bad(QStringLiteral("units 重复或不是 mil"));
                units = true;
            } else if (tag == "id") {
                count(v, 2);
                if (hasId)
                    bad(QStringLiteral("id 重复"));
                f.id = v[1];
                hasId = true;
            } else if (tag == "name") {
                count(v, 2);
                if (hasName)
                    bad(QStringLiteral("name 重复"));
                f.name = v[1];
                hasName = true;
            } else if (tag == "pad") {
                count(v, 13);
                Pad p;
                p.id = id(v[1]);
                p.number = integer(v[2]);
                p.shape = shape(v[3]);
                p.pos = {real(v[4]), real(v[5])};
                p.size = {real(v[6]), real(v[7])};
                p.drill = real(v[8]);
                p.maskMargin = real(v[9]);
                p.clearance = real(v[10]);
                p.cornerRadius = real(v[11]);
                p.layer = layer(v[12]);
                if (padIds.contains(p.id))
                    bad(QStringLiteral("padId 重复"));
                padIds.insert(p.id);
                f.pads.append(p);
            } else if (tag == "pin") {
                count(v, 5);
                Pin p{integer(v[1]), v[2], electrical(v[3]), id(v[4])};
                if (pinIds.contains(p.number))
                    bad(QStringLiteral("引脚编号重复"));
                pinIds.insert(p.number);
                f.pins.append(p);
            } else if (tag == "hole") {
                count(v, 6);
                if (v[4] != "0" && v[4] != "1")
                    bad(QStringLiteral("plated 必须为 0 或 1"));
                f.holes.append({{real(v[1]), real(v[2])}, real(v[3]), v[4] == "1", real(v[5])});
            } else if (tag == "silk") {
                if (v.size() < 3)
                    bad(QStringLiteral("silk 字段不足"));
                PrimitivePtr p;
                const auto& kind = v[1];
                if (kind == "line") {
                    count(v, 8);
                    auto q = std::make_unique<SilkLine>();
                    q->layer = layer(v[2]);
                    q->width = real(v[3]);
                    q->a = {real(v[4]), real(v[5])};
                    q->b = {real(v[6]), real(v[7])};
                    p = std::move(q);
                } else if (kind == "arc") {
                    count(v, 9);
                    auto q = std::make_unique<SilkArc>();
                    q->layer = layer(v[2]);
                    q->width = real(v[3]);
                    q->center = {real(v[4]), real(v[5])};
                    q->radius = real(v[6]);
                    q->startAngle = real(v[7]);
                    q->spanAngle = real(v[8]);
                    p = std::move(q);
                } else if (kind == "rect") {
                    count(v, 9);
                    if (v[8] != "0" && v[8] != "1")
                        bad(QStringLiteral("filled 必须为 0 或 1"));
                    auto q = std::make_unique<SilkRect>();
                    q->layer = layer(v[2]);
                    q->width = real(v[3]);
                    q->rect = QRectF(real(v[4]), real(v[5]), real(v[6]), real(v[7]));
                    q->filled = v[8] == "1";
                    p = std::move(q);
                } else if (kind == "poly") {
                    if (v.size() < 6)
                        bad(QStringLiteral("多边形字段不足"));
                    int n = integer(v[5]);
                    if (n < 3 || v.size() != 6 + 2 * n)
                        bad(QStringLiteral("多边形点数不匹配"));
                    if (v[4] != "0" && v[4] != "1")
                        bad(QStringLiteral("filled 必须为 0 或 1"));
                    auto q = std::make_unique<SilkPolygon>();
                    q->layer = layer(v[2]);
                    q->width = real(v[3]);
                    q->filled = v[4] == "1";
                    for (int j = 0; j < n; ++j)
                        q->points.append({real(v[6 + 2 * j]), real(v[7 + 2 * j])});
                    p = std::move(q);
                } else if (kind == "text") {
                    count(v, 8);
                    auto q = std::make_unique<SilkText>();
                    q->layer = layer(v[2]);
                    q->pos = {real(v[3]), real(v[4])};
                    q->height = real(v[5]);
                    q->rotation = real(v[6]);
                    q->text = v[7];
                    p = std::move(q);
                } else
                    bad(QStringLiteral("未知丝印类型"));
                f.silk.push_back(std::move(p));
            } else if (tag == "end") {
                count(v, 1);
                ended = true;
            } else
                bad(QStringLiteral("未知记录：") + tag);
        }
    } catch (const ParseFailure& e) {
        return fail(error, QStringLiteral("第 %1 行：%2").arg(lineNo).arg(e.message));
    }
    if (!units || !hasId || !hasName || !ended)
        return fail(error, QStringLiteral("缺少 units、id、name 或 end"));
    PadId maxId = 0;
    for (const auto& p : f.pads)
        maxId = qMax(maxId, p.id);
    if (maxId == std::numeric_limits<PadId>::max())
        return fail(error, QStringLiteral("PadId 已耗尽"));
    f.nextPadId = maxId + 1;
    if (!validateStructureImpl(f, error))
        return false;
    output = std::move(f);
    return true;
}

bool saveFootprintImpl(const QString& path, const Footprint& f, QString* error) {
    if (!validateStructureImpl(f, error))
        return false;
    QByteArray data;
    try {
        data = encodeFootprintImpl(f);
    } catch (const ParseFailure& e) {
        return fail(error, e.message);
    }
    if (data.size() > 16 * 1024 * 1024)
        return fail(error, QStringLiteral("封装文件超过 16 MiB 上限"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, file.errorString());
    {
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << QString::fromUtf8(data);
        stream.flush();
        if (stream.status() != QTextStream::Ok) {
            file.cancelWriting();
            return fail(error, QStringLiteral("文件写入失败"));
        }
    }
    if (!file.commit())
        return fail(error, file.errorString());
    return true;
}
bool loadFootprintImpl(const QString& path, Footprint& output, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, file.errorString());
    if (file.size() > 16 * 1024 * 1024)
        return fail(error, QStringLiteral("封装文件超过 16 MiB 上限"));
    auto data = file.read(16 * 1024 * 1024 + 1);
    if (file.error() != QFileDevice::NoError)
        return fail(error, file.errorString());
    return decodeFootprintImpl(data, output, error);
}
} // namespace eda::detail
