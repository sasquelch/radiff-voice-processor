#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace radiff::core {

/// Identifiers for every user-facing DSP parameter.
///
/// PHASE 2 NOTE: the full parameter set (gate, de-esser, EQ, compression, limiting,
/// loudness, etc.) is defined by the master specification and will be added here.
/// The ordering of this enum is the stable parameter index used by the plug-in
/// formats, so new parameters are appended, never inserted.
enum class ParamId : std::size_t {
    Bypass = 0,
    InputGainDb,
    OutputGainDb,

    Count // keep last
};

constexpr std::size_t kNumParams = static_cast<std::size_t>(ParamId::Count);

enum class ParamUnit { None, Decibels, Hertz, Milliseconds, Percent, Ratio, Boolean };

/// Static description of one parameter. The same table drives the preset format,
/// the CLI overrides, the plug-in parameter lists and the UI.
struct ParamInfo {
    ParamId id;
    std::string_view key;  ///< stable, portable key used in .radvoice.json presets
    std::string_view name; ///< display name
    ParamUnit unit;
    float minValue;
    float maxValue;
    float defaultValue;
    float step; ///< 0 = continuous
};

[[nodiscard]] const std::array<ParamInfo, kNumParams>& paramTable() noexcept;
[[nodiscard]] const ParamInfo& paramInfo(ParamId id) noexcept;
[[nodiscard]] std::optional<ParamId> paramIdForKey(std::string_view key) noexcept;

/// Plain value set for all parameters. Trivially copyable so hosts can hand a
/// complete snapshot to the engine without locking.
class ParameterSet {
public:
    ParameterSet(); // initialised to defaults

    [[nodiscard]] float get(ParamId id) const noexcept { return values_[static_cast<std::size_t>(id)]; }

    /// Sets a value, clamped to the parameter's documented range.
    void set(ParamId id, float value) noexcept;

    [[nodiscard]] bool operator==(const ParameterSet&) const noexcept = default;

private:
    std::array<float, kNumParams> values_{};
};

} // namespace radiff::core
