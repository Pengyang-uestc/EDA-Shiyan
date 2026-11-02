#pragma once
#include <QString>
#include <optional>
#include <utility>
namespace eda {
struct Status {
    bool ok = false;
    QString message;
    explicit operator bool() const {
        return ok;
    }
    static Status success() {
        return {true, {}};
    }
    static Status failure(QString message) {
        return {false, std::move(message)};
    }
};
template <class T> struct Result {
    Status status;
    std::optional<T> value;
};
} // namespace eda
