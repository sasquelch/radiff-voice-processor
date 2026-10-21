#pragma once

namespace radiff::app {

/// Process exit codes. Identical on macOS and Windows and part of the CLI contract
/// relied on by batch integrations (PlayoutONE WebVT wrapper, scripts).
enum class ExitCode : int {
    Success = 0,
    UsageError = 2,        ///< bad or missing arguments
    InputError = 3,        ///< input file missing, unreadable or unsupported
    PresetError = 4,       ///< preset missing or invalid
    ProcessingError = 5,   ///< DSP failure (NaN/inf detected, unsupported configuration)
    OutputError = 6,       ///< could not write or publish the output (I/O)
    ValidationError = 7,   ///< output written but failed verification (structure, length, silence)
    Cancelled = 8,         ///< cancelled by the user or a timeout
    InternalError = 70,    ///< unexpected exception
};

[[nodiscard]] constexpr int toInt(ExitCode c) noexcept { return static_cast<int>(c); }
[[nodiscard]] const char* describe(ExitCode c) noexcept;

} // namespace radiff::app
