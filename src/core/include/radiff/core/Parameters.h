#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace radiff::core {

// ---------------------------------------------------------------------------
// Modules of the canonical signal chain (section 5 of the master spec).
// Order here IS the fixed v1 processing order.
// ---------------------------------------------------------------------------
enum class Module : std::size_t {
    Global = 0, Input, Eq, Expander, Deesser, Compressor1, Compressor2, Presence, Output, Count
};
constexpr std::size_t kNumModules = static_cast<std::size_t>(Module::Count);
[[nodiscard]] std::string_view moduleKey(Module m) noexcept;  ///< JSON key, e.g. "compressor1"
[[nodiscard]] std::string_view moduleName(Module m) noexcept; ///< display name, e.g. "Compressor 1"

enum class ParamUnit { None, Decibels, Hertz, Milliseconds, Ratio, Q, Fraction, Boolean, Enum, DbPerOctave, Octaves };

inline constexpr std::array<std::string_view, 5> kEqTypeNames{"bell", "lowShelf", "highShelf", "highPass", "lowPass"};
inline constexpr std::array<std::string_view, 2> kDetectorNames{"rms", "peak"};
inline constexpr std::array<std::string_view, 2> kThresholdModeNames{"absolute", "speechRelative"};
inline constexpr std::array<std::string_view, 0> kNoEnum{};

enum class EqBandType : int { Bell = 0, LowShelf, HighShelf, HighPass, LowPass };
enum class DetectorType : int { Rms = 0, Peak };
/// Expander threshold mode. `SpeechRelative` places the threshold `relativeThresholdDb` below the
/// measured speech level of the material (supplied by the host via Engine::setSpeechLevelDb,
/// e.g. from an offline analysis pass); without a measurement it falls back to the absolute value.
enum class ThresholdMode : int { Absolute = 0, SpeechRelative };

// ---------------------------------------------------------------------------
// The parameter list. X(Id, Module, BandIndex, jsonKey, displayName, unit, min, max, default, step, enumNames)
// The enum AND the table are generated from this list, so they can never drift.
// APPEND ONLY: the enum value is the stable automation/state index for every host.
// ---------------------------------------------------------------------------
#define RADIFF_EQ_BAND(X, N, IDX, DEF_TYPE, DEF_HZ, DEF_Q)                                                           \
    X(EqBand##N##Enabled, Eq, IDX, "enabled", "EQ " #N " On", Boolean, 0, 1, 0, 1, kNoEnum)                           \
    X(EqBand##N##Type, Eq, IDX, "type", "EQ " #N " Type", Enum, 0, 4, DEF_TYPE, 1, kEqTypeNames)                      \
    X(EqBand##N##FrequencyHz, Eq, IDX, "frequencyHz", "EQ " #N " Freq", Hertz, 20, 20000, DEF_HZ, 0, kNoEnum)         \
    X(EqBand##N##GainDb, Eq, IDX, "gainDb", "EQ " #N " Gain", Decibels, -18, 18, 0, 0, kNoEnum)                       \
    X(EqBand##N##Q, Eq, IDX, "q", "EQ " #N " Q", Q, 0.2, 12, DEF_Q, 0, kNoEnum)

#define RADIFF_COMPRESSOR(X, P, M, LABEL, DEF_EN, DEF_THR, DEF_RATIO, DEF_KNEE, DEF_ATT, DEF_REL, DEF_DET, DEF_RMS)    \
    X(P##Enabled, M, -1, "enabled", LABEL " On", Boolean, 0, 1, DEF_EN, 1, kNoEnum)                                   \
    X(P##ThresholdDbfs, M, -1, "thresholdDbfs", LABEL " Threshold", Decibels, -60, 0, DEF_THR, 0, kNoEnum)            \
    X(P##Ratio, M, -1, "ratio", LABEL " Ratio", Ratio, 1, 20, DEF_RATIO, 0, kNoEnum)                                  \
    X(P##KneeDb, M, -1, "kneeDb", LABEL " Knee", Decibels, 0, 18, DEF_KNEE, 0, kNoEnum)                               \
    X(P##AttackMs, M, -1, "attackMs", LABEL " Attack", Milliseconds, 0.1, 100, DEF_ATT, 0, kNoEnum)                   \
    X(P##ReleaseMs, M, -1, "releaseMs", LABEL " Release", Milliseconds, 10, 2000, DEF_REL, 0, kNoEnum)                \
    X(P##MakeupGainDb, M, -1, "makeupGainDb", LABEL " Makeup", Decibels, -12, 24, 0, 0, kNoEnum)                      \
    X(P##Detector, M, -1, "detector", LABEL " Detector", Enum, 0, 1, DEF_DET, 1, kDetectorNames)                      \
    X(P##RmsWindowMs, M, -1, "rmsWindowMs", LABEL " RMS Window", Milliseconds, 1, 100, DEF_RMS, 0, kNoEnum)           \
    X(P##SidechainHighPassEnabled, M, -1, "sidechainHighPassEnabled", LABEL " SC HPF On", Boolean, 0, 1, 0, 1, kNoEnum) \
    X(P##SidechainHighPassHz, M, -1, "sidechainHighPassHz", LABEL " SC HPF", Hertz, 20, 2000, 100, 0, kNoEnum)        \
    X(P##AutoRelease, M, -1, "autoRelease", LABEL " Auto Release", Boolean, 0, 1, 0, 1, kNoEnum)                      \
    X(P##Wet, M, -1, "wet", LABEL " Wet", Fraction, 0, 1, 1, 0, kNoEnum)                                              \
    X(P##StereoLink, M, -1, "stereoLink", LABEL " Stereo Link", Boolean, 0, 1, 1, 1, kNoEnum)

#define RADIFF_PARAM_LIST(X)                                                                                           \
    X(Bypass, Global, -1, "bypass", "Bypass", Boolean, 0, 1, 0, 1, kNoEnum)                                            \
    X(InputGainDb, Input, -1, "gainDb", "Input Gain", Decibels, -24, 24, 0, 0, kNoEnum)                                \
    X(InputDcBlockEnabled, Input, -1, "dcBlockEnabled", "DC Block", Boolean, 0, 1, 1, 1, kNoEnum)                      \
    X(InputHighPassEnabled, Input, -1, "highPassEnabled", "HPF On", Boolean, 0, 1, 1, 1, kNoEnum)                      \
    X(InputHighPassHz, Input, -1, "highPassHz", "HPF Freq", Hertz, 20, 250, 80, 0, kNoEnum)                            \
    X(InputHighPassSlopeDbOct, Input, -1, "highPassSlopeDbOct", "HPF Slope", DbPerOctave, 12, 24, 12, 12, kNoEnum)     \
    X(EqEnabled, Eq, -1, "enabled", "EQ On", Boolean, 0, 1, 1, 1, kNoEnum)                                             \
    RADIFF_EQ_BAND(X, 1, 0, 1, 120, 0.7)                                                                               \
    RADIFF_EQ_BAND(X, 2, 1, 0, 300, 1.0)                                                                               \
    RADIFF_EQ_BAND(X, 3, 2, 0, 1200, 1.0)                                                                              \
    RADIFF_EQ_BAND(X, 4, 3, 0, 3500, 1.0)                                                                              \
    RADIFF_EQ_BAND(X, 5, 4, 2, 10000, 0.7)                                                                             \
    X(ExpEnabled, Expander, -1, "enabled", "Expander On", Boolean, 0, 1, 1, 1, kNoEnum)                                \
    X(ExpThresholdDbfs, Expander, -1, "thresholdDbfs", "Exp Threshold", Decibels, -80, 0, -48, 0, kNoEnum)             \
    X(ExpRatio, Expander, -1, "ratio", "Exp Ratio", Ratio, 1, 8, 2, 0, kNoEnum)                                        \
    X(ExpKneeDb, Expander, -1, "kneeDb", "Exp Knee", Decibels, 0, 18, 6, 0, kNoEnum)                                   \
    X(ExpAttackMs, Expander, -1, "attackMs", "Exp Attack", Milliseconds, 0.1, 100, 5, 0, kNoEnum)                      \
    X(ExpHoldMs, Expander, -1, "holdMs", "Exp Hold", Milliseconds, 0, 1000, 120, 0, kNoEnum)                           \
    X(ExpReleaseMs, Expander, -1, "releaseMs", "Exp Release", Milliseconds, 10, 2000, 250, 0, kNoEnum)                 \
    X(ExpHysteresisDb, Expander, -1, "hysteresisDb", "Exp Hysteresis", Decibels, 0, 12, 4, 0, kNoEnum)                 \
    X(ExpDetector, Expander, -1, "detector", "Exp Detector", Enum, 0, 1, 0, 1, kDetectorNames)                         \
    X(ExpRmsWindowMs, Expander, -1, "rmsWindowMs", "Exp RMS Window", Milliseconds, 1, 100, 10, 0, kNoEnum)             \
    X(ExpSidechainHighPassEnabled, Expander, -1, "sidechainHighPassEnabled", "Exp SC HPF On", Boolean, 0, 1, 0, 1, kNoEnum) \
    X(ExpSidechainHighPassHz, Expander, -1, "sidechainHighPassHz", "Exp SC HPF", Hertz, 20, 2000, 100, 0, kNoEnum)     \
    X(ExpSidechainLowPassEnabled, Expander, -1, "sidechainLowPassEnabled", "Exp SC LPF On", Boolean, 0, 1, 0, 1, kNoEnum) \
    X(ExpSidechainLowPassHz, Expander, -1, "sidechainLowPassHz", "Exp SC LPF", Hertz, 1000, 20000, 8000, 0, kNoEnum)   \
    X(ExpLookaheadMs, Expander, -1, "lookaheadMs", "Exp Lookahead", Milliseconds, 0, 20, 0, 0, kNoEnum)                \
    X(ExpMaxAttenuationDb, Expander, -1, "maxAttenuationDb", "Exp Max Atten", Decibels, 0, 60, 10, 0, kNoEnum)         \
    X(ExpWet, Expander, -1, "wet", "Exp Wet", Fraction, 0, 1, 1, 0, kNoEnum)                                           \
    X(ExpStereoLink, Expander, -1, "stereoLink", "Exp Stereo Link", Boolean, 0, 1, 1, 1, kNoEnum)                      \
    RADIFF_COMPRESSOR(X, Comp1, Compressor1, "Comp 1", 1, -24, 2.5, 6, 10, 150, 0, 10)                                  \
    RADIFF_COMPRESSOR(X, Comp2, Compressor2, "Comp 2", 0, -18, 4, 3, 3, 80, 1, 5)                                       \
    X(OutputGainDb, Output, -1, "gainDb", "Output Gain", Decibels, -24, 24, 0, 0, kNoEnum)                              \
    /* --- appended after v0.1.0 (append-only: enum index is the stable ID) --- */                                      \
    X(ExpThresholdMode, Expander, -1, "thresholdMode", "Exp Threshold Mode", Enum, 0, 1, 0, 1, kThresholdModeNames)     \
    X(ExpRelativeThresholdDb, Expander, -1, "relativeThresholdDb", "Exp Relative Threshold", Decibels, -40, 0, -15, 0, kNoEnum) \
    X(ExpTransientRejectMs, Expander, -1, "transientRejectMs", "Exp Transient Reject", Milliseconds, 0, 20, 0, 0, kNoEnum) \
    /* --- Phase 6 (dbx-286-class coverage): de-esser, enhancer, compressor auto makeup --- */                           \
    X(DeessEnabled, Deesser, -1, "enabled", "De-ess On", Boolean, 0, 1, 0, 1, kNoEnum)                                   \
    X(DeessFrequencyHz, Deesser, -1, "frequencyHz", "De-ess Freq", Hertz, 800, 12000, 6000, 0, kNoEnum)                  \
    X(DeessBandwidthOct, Deesser, -1, "bandwidthOctaves", "De-ess Width", Octaves, 0.25, 3, 1, 0, kNoEnum)               \
    X(DeessThresholdDbfs, Deesser, -1, "thresholdDbfs", "De-ess Threshold", Decibels, -60, 0, -30, 0, kNoEnum)           \
    X(DeessMaxReductionDb, Deesser, -1, "maxReductionDb", "De-ess Max Reduction", Decibels, 0, 12, 6, 0, kNoEnum)        \
    X(DeessAttackMs, Deesser, -1, "attackMs", "De-ess Attack", Milliseconds, 0.1, 50, 1, 0, kNoEnum)                     \
    X(DeessReleaseMs, Deesser, -1, "releaseMs", "De-ess Release", Milliseconds, 10, 500, 60, 0, kNoEnum)                 \
    X(DeessListen, Deesser, -1, "listen", "De-ess Listen", Boolean, 0, 1, 0, 1, kNoEnum)                                 \
    X(DeessStereoLink, Deesser, -1, "stereoLink", "De-ess Stereo Link", Boolean, 0, 1, 1, 1, kNoEnum)                    \
    X(PresEnabled, Presence, -1, "enabled", "Enhancer On", Boolean, 0, 1, 0, 1, kNoEnum)                                 \
    X(PresLfDetail, Presence, -1, "lfDetail", "LF Detail", None, 0, 10, 0, 0, kNoEnum)                                   \
    X(PresLfFrequencyHz, Presence, -1, "lfFrequencyHz", "LF Detail Freq", Hertz, 40, 300, 100, 0, kNoEnum)               \
    X(PresHfDetail, Presence, -1, "hfDetail", "HF Detail", None, 0, 10, 0, 0, kNoEnum)                                   \
    X(PresHfFrequencyHz, Presence, -1, "hfFrequencyHz", "HF Detail Freq", Hertz, 1500, 12000, 3500, 0, kNoEnum)          \
    X(PresHfDynamic, Presence, -1, "hfDynamic", "HF Detail Dynamic", Fraction, 0, 1, 0.5, 0, kNoEnum)                    \
    X(PresMakeupGainDb, Presence, -1, "makeupGainDb", "Enhancer Makeup", Decibels, -12, 12, 0, 0, kNoEnum)               \
    X(Comp1AutoMakeup, Compressor1, -1, "autoMakeup", "Comp 1 Auto Makeup", Boolean, 0, 1, 0, 1, kNoEnum)                \
    X(Comp2AutoMakeup, Compressor2, -1, "autoMakeup", "Comp 2 Auto Makeup", Boolean, 0, 1, 0, 1, kNoEnum)

enum class ParamId : std::size_t {
#define RADIFF_PARAM_ENUM(Id, ...) Id,
    RADIFF_PARAM_LIST(RADIFF_PARAM_ENUM)
#undef RADIFF_PARAM_ENUM
        Count
};
constexpr std::size_t kNumParams = static_cast<std::size_t>(ParamId::Count);
constexpr int kNumEqBands = 5;

/// Static description of one parameter. The same table drives the preset format,
/// the CLI overrides, the plug-in parameter lists and the UI.
struct ParamInfo {
    ParamId id;
    Module module;
    int band;              ///< EQ band index (0-based) or -1
    std::string_view key;  ///< JSON key inside the module object
    std::string_view name; ///< display name
    ParamUnit unit;
    float minValue;
    float maxValue;
    float defaultValue;
    float step; ///< 0 = continuous
    std::span<const std::string_view> enumNames;

    [[nodiscard]] bool isBoolean() const noexcept { return unit == ParamUnit::Boolean; }
    [[nodiscard]] bool isEnum() const noexcept { return unit == ParamUnit::Enum; }
};

[[nodiscard]] const std::array<ParamInfo, kNumParams>& paramTable() noexcept;
[[nodiscard]] const ParamInfo& paramInfo(ParamId id) noexcept;

/// Fully-qualified portable key, e.g. "expander.thresholdDbfs" or "eq.band3.gainDb".
/// Used by the CLI (--set) and by tests; the JSON file uses the nested structure.
[[nodiscard]] std::string paramPath(ParamId id);
[[nodiscard]] std::optional<ParamId> paramIdForPath(std::string_view path) noexcept;

/// Looks up a parameter by module (+ band) and JSON key.
[[nodiscard]] std::optional<ParamId> paramIdFor(Module m, int band, std::string_view key) noexcept;
[[nodiscard]] std::optional<Module> moduleForKey(std::string_view key) noexcept;

/// Parses a user-facing textual value: numbers, "true"/"false"/"on"/"off", or enum names.
[[nodiscard]] std::optional<float> parseParamValue(ParamId id, std::string_view text) noexcept;
/// Enum name for a value (empty if not an enum parameter).
[[nodiscard]] std::string_view enumName(ParamId id, float value) noexcept;
/// Unit suffix for displays ("dB", "Hz", "ms", ":1", "oct", ...; empty for none/bool/enum).
[[nodiscard]] std::string_view unitSuffix(ParamUnit unit) noexcept;
/// False for operator-only states that must never be written to a preset or restored as an
/// on-air default (the de-esser's detection monitor). Serialisers write such parameters as
/// their default value and loaders reset them.
[[nodiscard]] bool isPersistent(ParamId id) noexcept;

/// Plain value set for all parameters. Trivially copyable so hosts can hand a complete
/// snapshot to the engine without locking.
class ParameterSet {
public:
    ParameterSet(); // initialised to defaults

    [[nodiscard]] float get(ParamId id) const noexcept { return values_[static_cast<std::size_t>(id)]; }
    [[nodiscard]] bool getBool(ParamId id) const noexcept { return get(id) >= 0.5f; }
    [[nodiscard]] int getInt(ParamId id) const noexcept { return static_cast<int>(get(id) + 0.5f); }

    /// Sets a value, snapped to the step (if any) and clamped to the documented range.
    void set(ParamId id, float value) noexcept;
    void set(ParamId id, bool value) noexcept { set(id, value ? 1.0f : 0.0f); }

    /// EQ band accessors (band 0..4).
    [[nodiscard]] static ParamId eqParam(int band, std::string_view key) noexcept;

    [[nodiscard]] bool operator==(const ParameterSet&) const noexcept = default;

private:
    std::array<float, kNumParams> values_{};
};

} // namespace radiff::core
