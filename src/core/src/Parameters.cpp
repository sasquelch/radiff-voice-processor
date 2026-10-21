#include "radiff/core/Parameters.h"

#include <algorithm>

namespace radiff::core {

namespace {

constexpr std::array<ParamInfo, kNumParams> kTable{{
    {ParamId::Bypass, "bypass", "Bypass", ParamUnit::Boolean, 0.0f, 1.0f, 0.0f, 1.0f},
    {ParamId::InputGainDb, "input_gain_db", "Input Gain", ParamUnit::Decibels, -24.0f, 24.0f, 0.0f, 0.0f},
    {ParamId::OutputGainDb, "output_gain_db", "Output Gain", ParamUnit::Decibels, -24.0f, 24.0f, 0.0f, 0.0f},
}};

// Compile-time guard: the table must be in enum order so lookups are O(1).
constexpr bool tableIsOrdered() {
    for (std::size_t i = 0; i < kTable.size(); ++i)
        if (static_cast<std::size_t>(kTable[i].id) != i)
            return false;
    return true;
}
static_assert(tableIsOrdered(), "paramTable() must list parameters in ParamId order");

} // namespace

const std::array<ParamInfo, kNumParams>& paramTable() noexcept { return kTable; }

const ParamInfo& paramInfo(ParamId id) noexcept { return kTable[static_cast<std::size_t>(id)]; }

std::optional<ParamId> paramIdForKey(std::string_view key) noexcept {
    for (const auto& p : kTable)
        if (p.key == key)
            return p.id;
    return std::nullopt;
}

ParameterSet::ParameterSet() {
    for (const auto& p : kTable)
        values_[static_cast<std::size_t>(p.id)] = p.defaultValue;
}

void ParameterSet::set(ParamId id, float value) noexcept {
    const auto& info = paramInfo(id);
    if (info.step > 0.0f)
        value = std::round(value / info.step) * info.step;
    values_[static_cast<std::size_t>(id)] = std::clamp(value, info.minValue, info.maxValue);
}

} // namespace radiff::core
