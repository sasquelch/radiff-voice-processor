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

/// On-disk schema version (structure of the JSON). Bump only on incompatible changes
/// and add a migration in fromJson(); additive parameters are "missing key -> default".
constexpr int kPresetSchemaVersion = 1;
constexpr const char* kPresetFileExtension = ".radvoice.json";

struct PresetResult {
    std::string error;                 ///< empty = success
    std::vector<std::string> warnings; ///< e.g. unknown keys ignored
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

/// Serialises a preset to its canonical JSON text (UTF-8, LF, fixed key order, 2-space
/// indent) so identical presets produce byte-identical files on every platform.
/// Layout (see docs/PRESET-FORMAT.md):
///   schemaVersion, engineVersion, algorithmVersion, name, description, author,
///   signalOrder[], bypass, modules{input, eq{bands[]}, expander, compressor1, compressor2},
///   output{gainDb}
[[nodiscard]] std::string toJson(const Preset& preset);

/// Parses JSON text. Unknown keys/modules are reported as warnings and ignored (forward
/// compatibility); missing parameters take defaults; out-of-range values are clamped;
/// an unsupported schema version or signal order is an error.
PresetResult fromJson(const std::string& json, Preset& preset);

PresetResult loadPresetFile(const std::filesystem::path& file, Preset& preset);
PresetResult savePresetFile(const std::filesystem::path& file, const Preset& preset);

/// Canonical v1 signal order as written to `signalOrder`.
[[nodiscard]] const std::vector<std::string>& canonicalSignalOrder();

} // namespace radiff::presets
