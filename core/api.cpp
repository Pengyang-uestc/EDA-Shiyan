#include "io.h"
#include "io_internal.h"
#include "library.h"
#include <exception>
namespace eda {
namespace {
Status status(bool ok, const QString& message) {
    return ok ? Status::success() : Status::failure(QStringLiteral("操作失败：") + message);
}
template <class T, class F> Result<T> result(F action) {
    T value;
    QString error;
    try {
        if (!action(value, error))
            return {status(false, error), std::nullopt};
        return {Status::success(), std::move(value)};
    } catch (const std::exception&) {
        return {Status::failure(QStringLiteral("操作失败：内存不足或模型数据异常")), std::nullopt};
    }
}
template <class F> Status operation(F action) {
    QString error;
    try {
        const bool ok = action(error);
        return status(ok, error);
    } catch (const std::exception&) {
        return Status::failure(QStringLiteral("操作失败：内存不足或模型数据异常"));
    }
}
} // namespace
Status validateStructure(const Footprint& f) {
    return operation([&](QString& e) { return detail::validateStructureImpl(f, &e); });
}
Result<QByteArray> encodeFootprint(const Footprint& f) {
    return result<QByteArray>([&](QByteArray& out, QString& e) {
        if (!detail::validateStructureImpl(f, &e))
            return false;
        out = detail::encodeFootprintImpl(f);
        return true;
    });
}
Result<Footprint> decodeFootprint(const QByteArray& data) {
    return result<Footprint>([&](Footprint& f, QString& e) { return detail::decodeFootprintImpl(data, f, &e); });
}
Result<Footprint> loadFootprint(const QString& path) {
    return result<Footprint>([&](Footprint& f, QString& e) { return detail::loadFootprintImpl(path, f, &e); });
}
Status saveFootprint(const QString& path, const Footprint& f) {
    return operation([&](QString& e) { return detail::saveFootprintImpl(path, f, &e); });
}
Status validateBoard(const Board& board, const FootprintResolver& resolveFootprint) {
    const Status structure = validateBoardStructure(board);
    if (!structure)
        return structure;
    return operation([&](QString& e) {
        return detail::validateBoardReferencesImpl(board, resolveFootprint, &e);
    });
}
Result<QByteArray> encodeBoard(const Board& board, const FootprintResolver& resolveFootprint) {
    return result<QByteArray>([&](QByteArray& bytes, QString& e) {
        const Status status = validateBoard(board, resolveFootprint);
        if (!status) {
            e = status.message;
            return false;
        }
        return detail::encodeBoardImpl(board, bytes, &e);
    });
}
Result<Board> decodeBoard(const QByteArray& data, const FootprintResolver& resolveFootprint) {
    return result<Board>([&](Board& board, QString& e) {
        return detail::decodeBoardImpl(data, board, resolveFootprint, &e);
    });
}
Status saveBoard(const QString& path, const Board& board, const FootprintResolver& resolveFootprint) {
    return operation([&](QString& e) {
        return detail::saveBoardImpl(path, board, resolveFootprint, &e);
    });
}
Result<Board> loadBoard(const QString& path, const FootprintResolver& resolveFootprint) {
    return result<Board>([&](Board& board, QString& e) {
        return detail::loadBoardImpl(path, board, resolveFootprint, &e);
    });
}
Result<QVector<LibraryEntry>> FootprintLibrary::list() const {
    return result<QVector<LibraryEntry>>([&](QVector<LibraryEntry>& v, QString& e) { return listImpl(v, &e); });
}
Result<Footprint> FootprintLibrary::load(const QString& id) const {
    return result<Footprint>([&](Footprint& v, QString& e) { return loadImpl(id, v, &e); });
}
Status FootprintLibrary::save(const Footprint& f) {
    return operation([&](QString& e) { return saveImpl(f, &e); });
}
Status FootprintLibrary::remove(const QString& id) {
    return operation([&](QString& e) { return removeImpl(id, &e); });
}
Status FootprintLibrary::rebuildIndex() const {
    return operation([&](QString& e) { return rebuildIndexImpl(&e); });
}
} // namespace eda
