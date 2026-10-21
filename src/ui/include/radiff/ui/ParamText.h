#pragma once

#include "radiff/core/Parameters.h"

#include <string>

namespace radiff::ui {

/// Human-readable range for tooltips: "-80 … 0 dB", "20 … 20000 Hz", "rms | peak", "on/off".
[[nodiscard]] std::string rangeText(const core::ParamInfo& info);

/// Tooltip for a parameter control: "<display name> (<module.key>)\n<range>".
[[nodiscard]] std::string tooltipText(core::ParamId id);

/// Short control label derived from the display name with the module prefix removed
/// ("Exp Threshold" -> "Threshold", "EQ 3 Gain" -> "Gain", "Comp 1 SC HPF On" -> "SC HPF On").
[[nodiscard]] std::string shortLabel(const core::ParamInfo& info);

/// Formats a value with its unit the way the UI shows exact values ("-24.0 dB", "3.50 kHz" is NOT
/// used: frequencies stay in Hz so typed values round-trip, "2.5:1", "0.80", "on").
[[nodiscard]] std::string valueText(const core::ParamInfo& info, double value);

} // namespace radiff::ui
