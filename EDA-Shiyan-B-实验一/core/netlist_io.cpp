#include "io.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QStringList>

namespace eda {
namespace {
constexpr qint64 kMaxNetlistBytes = 16 * 1024 * 1024;

bool isIdentifier(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("\\A[^\\s\\p{Cc}\"]+\\z"),
                                            QRegularExpression::UseUnicodePropertiesOption);
    return pattern.match(text).hasMatch();
}

QString pinText(const PinRef& pin) {
    return pin.refdes + QLatin1Char('.') + QString::number(pin.pinNumber);
}

Status lineError(qsizetype line, const QString& message) {
    return Status::failure(QStringLiteral("第 %1 行：").arg(line) + message);
}
} // namespace

Result<Netlist> decodeNetlist(const QByteArray& data) {
    if (data.size() > kMaxNetlistBytes)
        return {Status::failure(QStringLiteral("网表超过 16 MiB 上限")), std::nullopt};
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder(data);
    if (decoder.hasError())
        return {Status::failure(QStringLiteral("网表不是有效的 UTF-8")), std::nullopt};

    static const QRegularExpression whitespace(QStringLiteral("\\s+"),
                                               QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression pinNumberPattern(QStringLiteral("\\A[0-9]+\\z"));
    Netlist netlist;
    QSet<QString> refdeses;
    QSet<QString> netNames;
    QSet<QString> assignedPins;
    QVector<qsizetype> netLines;
    const auto lines = QStringView{text}.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const auto fields = line.toString().split(whitespace);
        if (fields.front() == QStringLiteral("COMP")) {
            if (fields.size() != 3 || !isIdentifier(fields[1]) || !isIdentifier(fields[2]))
                return {lineError(i + 1, QStringLiteral("应为 COMP refdes footprintId")), std::nullopt};
            if (refdeses.contains(fields[1]))
                return {lineError(i + 1, QStringLiteral("元件位号重复：") + fields[1]), std::nullopt};
            refdeses.insert(fields[1]);
            netlist.components.append({fields[1], fields[2]});
        } else if (fields.front() == QStringLiteral("NET")) {
            if (fields.size() < 3 || !isIdentifier(fields[1]) || fields[1] == QStringLiteral("-"))
                return {lineError(i + 1, QStringLiteral("应为 NET 名称 refdes.引脚号 ...")), std::nullopt};
            if (netNames.contains(fields[1]))
                return {lineError(i + 1, QStringLiteral("网络名重复：") + fields[1]), std::nullopt};
            netNames.insert(fields[1]);
            Net net;
            net.name = fields[1];
            for (qsizetype j = 2; j < fields.size(); ++j) {
                const auto separator = fields[j].lastIndexOf(QLatin1Char('.'));
                const auto refdes = fields[j].left(separator);
                const auto numberText = fields[j].mid(separator + 1);
                if (separator < 1 || !isIdentifier(refdes) || !pinNumberPattern.match(numberText).hasMatch())
                    return {lineError(i + 1, QStringLiteral("引脚引用非法：") + fields[j]), std::nullopt};
                bool ok = false;
                const int number = numberText.toInt(&ok);
                if (!ok || number <= 0)
                    return {lineError(i + 1, QStringLiteral("引脚引用非法：") + fields[j]), std::nullopt};
                PinRef pin{refdes, number};
                const QString key = pinText(pin);
                if (assignedPins.contains(key))
                    return {lineError(i + 1, QStringLiteral("引脚已分配给网络：") + key), std::nullopt};
                assignedPins.insert(key);
                net.pins.append(std::move(pin));
            }
            netlist.nets.append(std::move(net));
            netLines.append(i + 1);
        } else {
            return {lineError(i + 1, QStringLiteral("未知网表记录：") + fields.front()), std::nullopt};
        }
    }
    for (qsizetype i = 0; i < netlist.nets.size(); ++i) {
        for (const auto& pin : netlist.nets[i].pins) {
            if (!refdeses.contains(pin.refdes))
                return {lineError(netLines[i], QStringLiteral("未声明的元件：") + pin.refdes), std::nullopt};
        }
    }
    if (netlist.components.isEmpty())
        return {Status::failure(QStringLiteral("网表缺少 COMP 记录")), std::nullopt};
    return {Status::success(), std::move(netlist)};
}

Result<Netlist> loadNetlist(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {Status::failure(file.errorString()), std::nullopt};
    if (file.size() > kMaxNetlistBytes)
        return {Status::failure(QStringLiteral("网表超过 16 MiB 上限")), std::nullopt};
    const QByteArray data = file.read(kMaxNetlistBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return {Status::failure(file.errorString()), std::nullopt};
    return decodeNetlist(data);
}

Result<QVector<ResolvedNetPin>> resolveNetlistPins(const Netlist& netlist,
                                                 const FootprintResolver& resolveFootprint) {
    if (!resolveFootprint)
        return {Status::failure(QStringLiteral("未提供封装查找器")), std::nullopt};
    QHash<QString, QString> components;
    QHash<QString, Footprint> footprints;
    for (const auto& component : netlist.components) {
        if (component.refdes.isEmpty() || component.footprintId.isEmpty() || components.contains(component.refdes))
            return {Status::failure(QStringLiteral("网表元件位号重复或缺少标识")), std::nullopt};
        components.insert(component.refdes, component.footprintId);
        if (footprints.contains(component.footprintId))
            continue;
        auto result = resolveFootprint(component.footprintId);
        if (!result.status.ok || !result.value)
            return {Status::failure(QStringLiteral("无法加载封装：") + component.footprintId +
                                    QStringLiteral("。") + result.status.message), std::nullopt};
        if (result.value->id != component.footprintId)
            return {Status::failure(QStringLiteral("封装查找器返回了错误的 ID：") + component.footprintId),
                    std::nullopt};
        const Status structure = validateStructure(*result.value);
        if (!structure.ok)
            return {structure, std::nullopt};
        footprints.insert(component.footprintId, std::move(*result.value));
    }
    QVector<ResolvedNetPin> pins;
    for (const auto& net : netlist.nets) {
        for (const auto& pin : net.pins) {
            const auto component = components.constFind(pin.refdes);
            if (component == components.cend())
                return {Status::failure(QStringLiteral("未声明的元件：") + pin.refdes), std::nullopt};
            const auto footprint = footprints.constFind(component.value());
            const auto* pad = footprint->findPadForPin(pin.pinNumber);
            if (!pad)
                return {Status::failure(QStringLiteral("找不到对应焊盘：") + pinText(pin)), std::nullopt};
            pins.append({net.name, pin, component.value(), pad->id});
        }
    }
    return {Status::success(), std::move(pins)};
}
} // namespace eda
