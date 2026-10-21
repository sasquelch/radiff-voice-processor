#pragma once

#include "radiff/core/Parameters.h"

#include <filesystem>
#include <string>
#include <vector>

namespace radiff::presets {

/// The portable preset: sonic parameters plus descriptive metadata only.
/// It deliberately contains NO file paths, NO machine identifiers and NO binary
/// state, so the same file loads on macOS, Windows, CLI and every plug-in format.
struct Preset {
    std::string name;
    std::string description;
    std::string author;
    core::ParameterSet parameters;

    [[nodiscard]] bool operator==(const Preset&) const = default;
};

/// Current on-disk schema version. Bump only on incompatible changes; additive
/// parameters are handled by "missing key -> default".
constexpr int kPresetFormatVersion = 1;
constexpr const char* kPresetFileExtension = ".radvoice.json";

struct PresetResult {
    std::string error;                 ///< empty = success
    std::vector<std::string> warnings; ///< e.g. unknown keys ignored
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

/// Serialises a preset to its canonical JSON text (UTF-8, LF line endings, sorted keys)
/// so that identical presets produce byte-identical files on every platform.
[[nodiscard]] std::string toJson(const Preset& preset);

/// Parses JSON text. Unknown parameter keys are reported as warnings and ignored;
/// missing parameters take their defaults; out-of-range values are clamped.
PresetResult fromJson(const std::string& json, Preset& preset);

PresetResult loadPresetFile(const std::filesystem::path& file, Preset& preset);
PresetResult savePresetFile(const std::filesystem::path& file, const Preset& preset);

} // namespace radiff::presets
