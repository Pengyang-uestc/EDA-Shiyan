#pragma once
namespace eda {
constexpr double kMilPerMm = 39.37007874015748;
constexpr double kEpsilonMil = 1e-3;
constexpr double milToMm(double mil) {
    return mil / kMilPerMm;
}
constexpr double mmToMil(double mm) {
    return mm * kMilPerMm;
}
} // namespace eda
