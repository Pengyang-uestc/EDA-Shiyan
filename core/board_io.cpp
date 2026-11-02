#include "io.h"
#include "io_internal.h"

#include "geometry.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QStringConverter>
#include <QStringList>
#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace eda::detail {
namespace {
constexpr qint64 kMaxBoardFileBytes = 16 * 1024 * 1024;

struct ParseFailure : std::exception {
    QString message;
    explicit ParseFailure(QString text) : message(std::move(text)) {}
    const char* what() const noexcept override {
        return "EDA-BRD parse or serialization failure";
    }
};

bool fail(QString* error, const QString& message) {
    if (error)
        *error = message;
    return false;
}

[[noreturn]] void bad(const QString& message) {
    throw ParseFailure{message};
}

QString quote(const QString& value) {
    QString output = QStringLiteral("\"");
    for (QChar c : value) {
        if (c == '\\')
            output += QStringLiteral("\\\\");
        else if (c == '\"')
            output += QStringLiteral("\\\"");
        else if (c == '\n')
            output += QStringLiteral("\\n");
        else
            output += c;
    }
    return output + QChar('"');
}

QString token(const QString& value) {
    static const QRegularExpression simple(QStringLiteral("^[A-Za-z0-9_+-]+$"));
    return simple.match(value).hasMatch() ? value : quote(value);
}

QString realText(double value) {
    if (!std::isfinite(value))
        bad(QStringLiteral("数字不是有限值"));
    std::array<char, 64> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                         std::chars_format::general,
                                         std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc{})
        bad(QStringLiteral("数字无法序列化"));

    const std::string encoded(buffer.data(), converted.ptr);
    const std::size_t exponentPosition = encoded.find_first_of("eE");
    if (exponentPosition == std::string::npos)
        return QString::fromLatin1(encoded.data(), static_cast<qsizetype>(encoded.size()));

    std::string mantissa = encoded.substr(0, exponentPosition);
    const int exponent = std::stoi(encoded.substr(exponentPosition + 1));
    const bool negative = !mantissa.empty() && mantissa.front() == '-';
    if (negative)
        mantissa.erase(mantissa.begin());

    std::string digits;
    digits.reserve(mantissa.size());
    std::size_t decimalPosition = 0;
    bool hadDecimalPoint = false;
    for (char c : mantissa) {
        if (c == '.') {
            decimalPosition = digits.size();
            hadDecimalPoint = true;
        } else {
            digits.push_back(c);
        }
    }
    if (!hadDecimalPoint)
        decimalPosition = digits.size();
    const long long shiftedPosition = static_cast<long long>(decimalPosition) + exponent;

    std::string plain;
    if (negative)
        plain.push_back('-');
    if (shiftedPosition <= 0) {
        plain += "0.";
        plain.append(static_cast<std::size_t>(-shiftedPosition), '0');
        plain += digits;
    } else if (shiftedPosition >= static_cast<long long>(digits.size())) {
        plain += digits;
        plain.append(static_cast<std::size_t>(shiftedPosition - static_cast<long long>(digits.size())), '0');
    } else {
        const auto split = static_cast<std::size_t>(shiftedPosition);
        plain.append(digits.data(), split);
        plain.push_back('.');
        plain.append(digits.data() + split, digits.size() - split);
    }
    return QString::fromLatin1(plain.data(), static_cast<qsizetype>(plain.size()));
}

double storedCoordinate(double value) {
    if (!std::isfinite(value))
        bad(QStringLiteral("坐标不是有限值"));
    return std::round(value * 1000.0) / 1000.0;
}

QString coordinateText(double value) {
    if (!std::isfinite(value))
        bad(QStringLiteral("坐标不是有限值"));
    return QString::number(value, 'f', 3);
}

QString lineError(qsizetype line, const QString& message) {
    return QStringLiteral("第 ") + QString::number(line) + QStringLiteral(" 行：") + message;
}

QVector<QString> lex(const QString& line, QVector<bool>* quotedFlags = nullptr) {
    QVector<QString> values;
    if (quotedFlags)
        quotedFlags->clear();
    qsizetype i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i].isSpace())
            ++i;
        if (i == line.size())
            break;

        QString value;
        bool wasQuoted = false;
        if (line[i] == QChar('"')) {
            wasQuoted = true;
            ++i;
            bool closed = false;
            while (i < line.size()) {
                const QChar c = line[i++];
                if (c == QChar('"')) {
                    closed = true;
                    break;
                }
                if (c != QChar('\\')) {
                    value += c;
                    continue;
                }
                if (i == line.size())
                    bad(QStringLiteral("字符串未闭合"));
                const QChar escaped = line[i++];
                if (escaped == QChar('n'))
                    value += QChar('\n');
                else if (escaped == QChar('\\') || escaped == QChar('"'))
                    value += escaped;
                else {
                    value += QChar('\\');
                    value += escaped;
                }
            }
            if (!closed || (i < line.size() && !line[i].isSpace()))
                bad(QStringLiteral("字符串未闭合或后缺空白"));
        } else {
            const qsizetype begin = i;
            while (i < line.size() && !line[i].isSpace()) {
                if (line[i] == QChar('"'))
                    bad(QStringLiteral("未引用文本中不能包含双引号"));
                ++i;
            }
            value = line.mid(begin, i - begin);
        }
        values.append(value);
        if (quotedFlags)
            quotedFlags->append(wasQuoted);
    }
    return values;
}

bool simpleIdentifier(const QString& value) {
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9_+-]+$"));
    return identifier.match(value).hasMatch();
}

void requireQuotedIdentifier(const QString& value, bool quoted, const QString& field) {
    if (!quoted && !simpleIdentifier(value))
        bad(field + QStringLiteral(" 含非标识符字符时必须加引号"));
}

void count(const QVector<QString>& values, qsizetype expected) {
    if (values.size() != expected)
        bad(QStringLiteral("字段数量不正确"));
}

double real(const QString& value) {
    static const QRegularExpression decimal(QStringLiteral("^-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?$"));
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !decimal.match(value).hasMatch() || !std::isfinite(number))
        bad(QStringLiteral("数字格式非法"));
    return number;
}

int integer(const QString& value) {
    static const QRegularExpression decimalInteger(QStringLiteral("^-?(?:0|[1-9][0-9]*)$"));
    bool ok = false;
    const int number = value.toInt(&ok);
    if (!ok || !decimalInteger.match(value).hasMatch())
        bad(QStringLiteral("整数格式非法"));
    return number;
}

quint64 positiveId(const QString& value) {
    static const QRegularExpression decimalId(QStringLiteral("^[0-9]+$"));
    bool ok = false;
    const quint64 id = value.toULongLong(&ok);
    if (!ok || id == kInvalidId || !decimalId.match(value).hasMatch())
        bad(QStringLiteral("ID 必须为正整数"));
    return id;
}

int pointCount(const QVector<QString>& values, qsizetype countIndex, int minimum, qsizetype baseFields) {
    if (countIndex >= values.size())
        bad(QStringLiteral("缺少点数"));
    const int number = integer(values[countIndex]);
    if (number < minimum || values.size() < baseFields ||
        static_cast<qsizetype>(number) != (values.size() - baseFields) / 2 ||
        (values.size() - baseFields) % 2 != 0)
        bad(QStringLiteral("点数与坐标字段数量不匹配"));
    return number;
}

QString typeName(BoardLayerType type) {
    switch (type) {
    case BoardLayerType::Copper:
        return QStringLiteral("copper");
    case BoardLayerType::Silk:
        return QStringLiteral("silk");
    case BoardLayerType::Mask:
        return QStringLiteral("mask");
    case BoardLayerType::Outline:
        return QStringLiteral("outline");
    }
    bad(QStringLiteral("未知层类型"));
}

BoardLayerType layerType(const QString& value) {
    if (value == QStringLiteral("copper"))
        return BoardLayerType::Copper;
    if (value == QStringLiteral("silk"))
        return BoardLayerType::Silk;
    if (value == QStringLiteral("mask"))
        return BoardLayerType::Mask;
    if (value == QStringLiteral("outline"))
        return BoardLayerType::Outline;
    bad(QStringLiteral("未知层类型"));
}

QString sideName(BoardSide side) {
    switch (side) {
    case BoardSide::Top:
        return QStringLiteral("top");
    case BoardSide::Bottom:
        return QStringLiteral("bottom");
    }
    bad(QStringLiteral("未知板面"));
}

BoardSide side(const QString& value) {
    if (value == QStringLiteral("top"))
        return BoardSide::Top;
    if (value == QStringLiteral("bottom"))
        return BoardSide::Bottom;
    bad(QStringLiteral("未知板面"));
}

QString netToken(const QString& name) {
    return name.isEmpty() ? QStringLiteral("-") : token(name);
}

void appendRecord(QString& output, const QStringList& fields) {
    output += fields.join(QChar(' '));
    output += QChar('\n');
}

QString pointText(QPointF point) {
    return coordinateText(point.x()) + QChar(' ') + coordinateText(point.y());
}

QColor color(const QString& value) {
    static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (!hex.match(value).hasMatch())
        bad(QStringLiteral("颜色必须为 #RRGGBB"));
    const QColor result(value);
    if (!result.isValid())
        bad(QStringLiteral("颜色非法"));
    return result;
}

PinRef pinRef(const QString& value, bool quoted) {
    const qsizetype separator = value.lastIndexOf(QChar('.'));
    if (separator <= 0 || separator + 1 >= value.size())
        bad(QStringLiteral("网络引脚必须为 refdes.pinNumber"));
    const QString refdes = value.left(separator);
    requireQuotedIdentifier(refdes, quoted, QStringLiteral("refdes"));
    const int pinNumber = integer(value.mid(separator + 1));
    if (pinNumber <= 0)
        bad(QStringLiteral("网络引脚编号必须为正整数"));
    return {value.left(separator), pinNumber};
}

void appendPointList(QString& row, const QVector<QPointF>& points) {
    row += QChar(' ');
    row += QString::number(points.size());
    for (QPointF point : points) {
        row += QChar(' ');
        row += pointText(point);
    }
}

void appendPolygonList(QString& row, const QPolygonF& points) {
    row += QChar(' ');
    row += QString::number(points.size());
    for (QPointF point : points) {
        row += QChar(' ');
        row += pointText(point);
    }
}

template <class Container, class Id> Id nextAfterMaximum(const Container& items) {
    Id maximum = 0;
    for (const auto& item : items)
        maximum = qMax(maximum, static_cast<Id>(item.id));
    if (maximum == std::numeric_limits<Id>::max())
        return kInvalidId;
    return static_cast<Id>(maximum + 1);
}

} // namespace

bool validateBoardReferencesImpl(const Board& board, const FootprintResolver& resolveFootprint, QString* error) {
    if (error)
        error->clear();
    if (!resolveFootprint && !board.instances.isEmpty())
        return fail(error, QStringLiteral("未提供封装查找器"));

    QHash<QString, const Instance*> instances;
    QHash<QString, Footprint> footprints;
    for (const auto& instance : board.instances)
        instances.insert(instance.refdes, &instance);

    QSet<QString> attemptedFootprints;
    QStringList missingFootprints;
    for (const auto& instance : board.instances) {
        if (!attemptedFootprints.contains(instance.footprintId)) {
            attemptedFootprints.insert(instance.footprintId);
            Result<Footprint> resolved = resolveFootprint(instance.footprintId);
            if (!resolved.status.ok || !resolved.value) {
                QString item = instance.footprintId;
                if (!resolved.status.message.isEmpty())
                    item += QStringLiteral("（") + resolved.status.message + QStringLiteral("）");
                missingFootprints.append(item);
                continue;
            }
            Footprint footprint = std::move(*resolved.value);
            if (footprint.id != instance.footprintId)
                return fail(error, QStringLiteral("封装查找器返回了错误的封装 ID：") + instance.footprintId);
            const Status status = validateStructure(footprint);
            if (!status)
                return fail(error, QStringLiteral("封装结构非法：") + status.message);
            footprints.insert(instance.footprintId, std::move(footprint));
        }
    }
    if (!missingFootprints.isEmpty())
        return fail(error, QStringLiteral("找不到以下封装：") + missingFootprints.join(QStringLiteral("；")));

    for (const auto& net : board.nets) {
        for (const auto& pin : net.pins) {
            const Instance* instance = instances.value(pin.refdes, nullptr);
            if (!instance)
                return fail(error, QStringLiteral("net 引用了不存在的 refdes：") + pin.refdes);
            const auto footprint = footprints.constFind(instance->footprintId);
            if (footprint == footprints.cend())
                return fail(error, QStringLiteral("找不到封装：") + instance->footprintId);
            bool found = false;
            for (const auto& footprintPin : footprint->pins) {
                if (footprintPin.number == pin.pinNumber) {
                    found = true;
                    break;
                }
            }
            if (!found)
                return fail(error, QStringLiteral("net 引用了不存在的封装引脚：") + pin.refdes + QChar('.') +
                                         QString::number(pin.pinNumber));
        }
    }
    return true;
}

bool encodeBoardImpl(const Board& board, QByteArray& bytes, QString* error) {
    Board stored = board;
    for (auto& point : stored.outline)
        point = {storedCoordinate(point.x()), storedCoordinate(point.y())};
    for (auto& instance : stored.instances) {
        instance.pos = {storedCoordinate(instance.pos.x()), storedCoordinate(instance.pos.y())};
        instance.rotationDeg = storedCoordinate(instance.rotationDeg);
    }
    for (auto& track : stored.tracks) {
        for (auto& point : track.points)
            point = {storedCoordinate(point.x()), storedCoordinate(point.y())};
    }
    for (auto& via : stored.vias)
        via.pos = {storedCoordinate(via.pos.x()), storedCoordinate(via.pos.y())};
    for (auto& pour : stored.pours) {
        for (auto& point : pour.points)
            point = {storedCoordinate(point.x()), storedCoordinate(point.y())};
    }
    const Status storedStatus = validateBoardStructure(stored);
    if (!storedStatus)
        return fail(error, QStringLiteral("坐标和角度按三位小数保存后模型非法：") + storedStatus.message);

    QString output = QStringLiteral("EDA-BRD 1\nunits mil\n");
    for (const auto& group : stored.groups) {
        appendRecord(output, {QStringLiteral("group"), QString::number(group.id), token(group.name),
                              QString::number(group.stackOrder)});
    }
    for (const auto& layer : stored.layers) {
        appendRecord(output, {QStringLiteral("layer"), QString::number(layer.id),
                              QString::number(layer.groupId), token(layer.name), typeName(layer.type),
                              layer.color.name(QColor::HexRgb), QString::number(layer.stackOrder)});
    }
    QString outline = QStringLiteral("outline");
    appendPolygonList(outline, stored.outline);
    output += outline + QChar('\n');

    for (const auto& instance : stored.instances) {
        appendRecord(output, {QStringLiteral("instance"), token(instance.refdes), token(instance.footprintId),
                              coordinateText(instance.pos.x()), coordinateText(instance.pos.y()),
                              coordinateText(instance.rotationDeg), sideName(instance.side)});
    }
    for (const auto& track : stored.tracks) {
        QString row = QStringList{QStringLiteral("track"), QString::number(track.id),
                                  QString::number(track.layerId), realText(track.width),
                                  netToken(track.net)}
                          .join(QChar(' '));
        appendPointList(row, track.points);
        output += row + QChar('\n');
    }
    for (const auto& via : stored.vias) {
        appendRecord(output, {QStringLiteral("via"), QString::number(via.id),
                              coordinateText(via.pos.x()), coordinateText(via.pos.y()), realText(via.drill),
                              realText(via.diameter), QString::number(via.fromLayer),
                              QString::number(via.toLayer), netToken(via.net)});
    }
    for (const auto& pour : stored.pours) {
        QString row = QStringList{QStringLiteral("pour"), QString::number(pour.id),
                                  QString::number(pour.layerId), netToken(pour.net), realText(pour.clearance),
                                  QString::number(pour.priority)}
                          .join(QChar(' '));
        appendPolygonList(row, pour.points);
        output += row + QChar('\n');
    }
    for (const auto& net : stored.nets) {
        QString row = QStringList{QStringLiteral("net"), token(net.name), QString::number(net.pins.size())}
                          .join(QChar(' '));
        for (const auto& pin : net.pins) {
            const QString reference = pin.refdes + QChar('.') + QString::number(pin.pinNumber);
            row += QChar(' ');
            row += token(reference);
        }
        output += row + QChar('\n');
    }
    output += QStringLiteral("end\n");
    bytes = output.toUtf8();
    return true;
}

bool decodeBoardImpl(const QByteArray& data, Board& output, const FootprintResolver& resolveFootprint,
                     QString* error) {
    if (error)
        error->clear();
    if (data.size() > kMaxBoardFileBytes)
        return fail(error, QStringLiteral("板文件超过 16 MiB 上限"));
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString source = decoder(data);
    if (decoder.hasError())
        return fail(error, QStringLiteral("文件不是有效 UTF-8"));
    const QStringList lines = source.split(QChar('\n'));
    qsizetype first = 0;
    while (first < lines.size() && (lines[first].trimmed().isEmpty() || lines[first].trimmed().startsWith('#')))
        ++first;
    QVector<QString> header;
    QVector<bool> headerQuoted;
    try {
        if (first < lines.size())
            header = lex(lines[first], &headerQuoted);
    } catch (const ParseFailure& failure) {
        return fail(error, lineError(first + 1, failure.message));
    }
    if (first == lines.size() || header != QVector<QString>{QStringLiteral("EDA-BRD"), QStringLiteral("1")} ||
        headerQuoted != QVector<bool>{false, false})
        return fail(error, QStringLiteral("缺少 EDA-BRD 1 文件头"));

    Board board;
    bool hasUnits = false;
    bool hasOutline = false;
    bool ended = false;
    qsizetype endLine = first + 1;
    QSet<LayerGroupId> groupIds;
    QSet<LayerId> layerIds;
    QSet<QString> refdeses;
    QSet<TrackId> trackIds;
    QSet<ViaId> viaIds;
    QSet<PourId> pourIds;
    QSet<QString> netNames;
    qsizetype lineNo = first + 1;
    try {
        for (qsizetype i = first + 1; i < lines.size(); ++i) {
            lineNo = i + 1;
            const QString text = lines[i].trimmed();
            if (text.isEmpty() || text.startsWith('#'))
                continue;
            QVector<bool> quoted;
            const QVector<QString> values = lex(lines[i], &quoted);
            if (ended)
                bad(QStringLiteral("end 后仍有数据"));
            if (values.isEmpty())
                continue;
            const QString& tag = values[0];

            if (tag == QStringLiteral("units")) {
                count(values, 2);
                if (hasUnits || values[1] != QStringLiteral("mil"))
                    bad(QStringLiteral("units 重复或不是 mil"));
                hasUnits = true;
            } else if (tag == QStringLiteral("group")) {
                count(values, 4);
                LayerGroup group{integer(values[1]), values[2], integer(values[3])};
                if (groupIds.contains(group.id))
                    bad(QStringLiteral("groupId 重复"));
                groupIds.insert(group.id);
                board.groups.append(group);
            } else if (tag == QStringLiteral("layer")) {
                count(values, 7);
                BoardLayer layer;
                layer.id = integer(values[1]);
                layer.groupId = integer(values[2]);
                layer.name = values[3];
                layer.type = layerType(values[4]);
                layer.color = color(values[5]);
                layer.stackOrder = integer(values[6]);
                if (layerIds.contains(layer.id))
                    bad(QStringLiteral("layerId 重复"));
                layerIds.insert(layer.id);
                board.layers.append(layer);
            } else if (tag == QStringLiteral("outline")) {
                if (hasOutline)
                    bad(QStringLiteral("outline 重复"));
                const int number = pointCount(values, 1, 3, 2);
                for (int j = 0; j < number; ++j)
                    board.outline.append({real(values[2 + 2 * j]), real(values[3 + 2 * j])});
                hasOutline = true;
            } else if (tag == QStringLiteral("instance")) {
                count(values, 7);
                Instance instance;
                instance.refdes = values[1];
                instance.footprintId = values[2];
                requireQuotedIdentifier(instance.refdes, quoted[1], QStringLiteral("refdes"));
                requireQuotedIdentifier(instance.footprintId, quoted[2], QStringLiteral("footprintId"));
                instance.pos = {real(values[3]), real(values[4])};
                instance.rotationDeg = real(values[5]);
                instance.side = side(values[6]);
                if (refdeses.contains(instance.refdes))
                    bad(QStringLiteral("refdes 重复"));
                refdeses.insert(instance.refdes);
                board.instances.append(instance);
            } else if (tag == QStringLiteral("track")) {
                if (values.size() < 10)
                    bad(QStringLiteral("track 字段不足"));
                const int number = pointCount(values, 5, 2, 6);
                Track track;
                track.id = positiveId(values[1]);
                track.layerId = integer(values[2]);
                track.width = real(values[3]);
                if (values[4] != QStringLiteral("-"))
                    requireQuotedIdentifier(values[4], quoted[4], QStringLiteral("net"));
                track.net = values[4] == QStringLiteral("-") ? QString() : values[4];
                if (trackIds.contains(track.id))
                    bad(QStringLiteral("trackId 重复"));
                trackIds.insert(track.id);
                for (int j = 0; j < number; ++j)
                    track.points.append({real(values[6 + 2 * j]), real(values[7 + 2 * j])});
                board.tracks.append(track);
            } else if (tag == QStringLiteral("via")) {
                count(values, 9);
                Via via;
                via.id = positiveId(values[1]);
                via.pos = {real(values[2]), real(values[3])};
                via.drill = real(values[4]);
                via.diameter = real(values[5]);
                via.fromLayer = integer(values[6]);
                via.toLayer = integer(values[7]);
                if (values[8] != QStringLiteral("-"))
                    requireQuotedIdentifier(values[8], quoted[8], QStringLiteral("net"));
                via.net = values[8] == QStringLiteral("-") ? QString() : values[8];
                if (viaIds.contains(via.id))
                    bad(QStringLiteral("viaId 重复"));
                viaIds.insert(via.id);
                board.vias.append(via);
            } else if (tag == QStringLiteral("pour")) {
                if (values.size() < 13)
                    bad(QStringLiteral("pour 字段不足"));
                const int number = pointCount(values, 6, 3, 7);
                CopperPour pour;
                pour.id = positiveId(values[1]);
                pour.layerId = integer(values[2]);
                if (values[3] != QStringLiteral("-"))
                    requireQuotedIdentifier(values[3], quoted[3], QStringLiteral("net"));
                pour.net = values[3] == QStringLiteral("-") ? QString() : values[3];
                pour.clearance = real(values[4]);
                pour.priority = integer(values[5]);
                if (pourIds.contains(pour.id))
                    bad(QStringLiteral("pourId 重复"));
                pourIds.insert(pour.id);
                for (int j = 0; j < number; ++j)
                    pour.points.append({real(values[7 + 2 * j]), real(values[8 + 2 * j])});
                board.pours.append(pour);
            } else if (tag == QStringLiteral("net")) {
                if (values.size() < 3)
                    bad(QStringLiteral("net 字段不足"));
                const int number = integer(values[2]);
                if (number < 0 || values.size() != 3 + static_cast<qsizetype>(number))
                    bad(QStringLiteral("net 引脚数量与字段不匹配"));
                Net net;
                net.name = values[1];
                requireQuotedIdentifier(net.name, quoted[1], QStringLiteral("net"));
                if (netNames.contains(net.name))
                    bad(QStringLiteral("net 名称重复"));
                netNames.insert(net.name);
                for (int j = 0; j < number; ++j)
                    net.pins.append(pinRef(values[3 + j], quoted[3 + j]));
                board.nets.append(net);
            } else if (tag == QStringLiteral("end")) {
                count(values, 1);
                ended = true;
                endLine = lineNo;
            } else {
                bad(QStringLiteral("未知记录：") + tag);
            }
        }
    } catch (const ParseFailure& failure) {
        return fail(error, lineError(lineNo, failure.message));
    }

    if (!ended)
        return fail(error, lineError(lines.size(), QStringLiteral("缺少 end")));
    if (!hasUnits || !hasOutline)
        return fail(error, lineError(endLine, QStringLiteral("缺少 units 或 outline")));

    const double area = signedArea(board.outline);
    if (std::isfinite(area) && area < 0.0)
        std::reverse(board.outline.begin(), board.outline.end());
    const Status structure = validateBoardStructure(board);
    if (!structure)
        return fail(error, lineError(endLine, structure.message));
    if (!validateBoardReferencesImpl(board, resolveFootprint, error)) {
        if (error)
            *error = lineError(endLine, *error);
        return false;
    }

    board.nextTrackId = nextAfterMaximum<QVector<Track>, TrackId>(board.tracks);
    board.nextViaId = nextAfterMaximum<QVector<Via>, ViaId>(board.vias);
    board.nextPourId = nextAfterMaximum<QVector<CopperPour>, PourId>(board.pours);
    output = std::move(board);
    return true;
}

bool saveBoardImpl(const QString& path, const Board& board, const FootprintResolver& resolveFootprint,
                   QString* error) {
    const Status structure = validateBoardStructure(board);
    if (!structure)
        return fail(error, structure.message);
    if (!validateBoardReferencesImpl(board, resolveFootprint, error))
        return false;

    QByteArray data;
    if (!encodeBoardImpl(board, data, error))
        return false;
    if (data.size() > kMaxBoardFileBytes)
        return fail(error, QStringLiteral("板文件超过 16 MiB 上限"));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, file.errorString());
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return fail(error, QStringLiteral("文件写入失败：") + file.errorString());
    }
    if (!file.commit())
        return fail(error, file.errorString());
    return true;
}

bool loadBoardImpl(const QString& path, Board& output, const FootprintResolver& resolveFootprint,
                   QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, file.errorString());
    if (file.size() > kMaxBoardFileBytes)
        return fail(error, QStringLiteral("板文件超过 16 MiB 上限"));
    const QByteArray data = file.read(kMaxBoardFileBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return fail(error, file.errorString());
    return decodeBoardImpl(data, output, resolveFootprint, error);
}

} // namespace eda::detail
