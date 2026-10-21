#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "radiff/core/Engine.h"
#include "radiff/core/modules/Equalizer.h"
#include "radiff/ui/JobRunner.h"
#include "radiff/ui/PresetLibrary.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// iPlug2 wants these two symbols from config / the plug-in header.
const int kNumPresets = 1;                                        // iPlug's built-in preset slots (unused)
const int kNumParams = static_cast<int>(radiff::core::kNumParams); // 1:1 with core::ParamId

/// Control tags for controls the plug-in updates from OnIdle (meters, texts, file panel).
enum EControlTags {
    kTagPresetButton = 0,
    kTagPresetState,
    kTagMeterIn,
    kTagMeterOut,
    kTagGrExpander,
    kTagGrComp1,
    kTagGrComp2,
    kTagExpThresholdText,
    kTagEqCurve,
    kTagFileIn,
    kTagFileOut,
    kTagProgress,
    kTagResult,
    kTagStatus,
    kTagProcessButton,
    kTagAnalyzeButton,
    kTagLatencyText,
    kTagSnapshot,
    kTagOverwrite,
    kNumTags
};

/// The standalone application: iPlug2 host glue around the shared radiff::core::Engine.
/// Parameter index == core::ParamId, so every parameter the engine knows gets a control and
/// the on-disk .radvoice.json format stays the single source of truth for presets.
class RaDiffVoiceApp final : public iplug::Plugin {
public:
    explicit RaDiffVoiceApp(const iplug::InstanceInfo& info);
    ~RaDiffVoiceApp() override;

    void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
    void OnReset() override;
    void OnIdle() override;
    void OnParamChangeUI(int paramIdx, iplug::EParamSource source) override;
    void OnUIOpen() override;
    bool SerializeState(iplug::IByteChunk& chunk) const override;
    int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;

private:
    // --- parameters -----------------------------------------------------------------------
    void initParameters();
    [[nodiscard]] radiff::core::ParameterSet liveParameters() const;
    void applyParameterSet(const radiff::core::ParameterSet& set);

    // --- presets --------------------------------------------------------------------------
    void initPresetLibrary();
    void showPresetMenu(iplug::igraphics::IControl* anchor);
    void loadPresetFile(const std::filesystem::path& file);
    void newPreset();
    void loadPresetDialog();
    void savePreset();
    void savePresetAsDialog();
    void deleteCurrentPreset();
    void refreshPresetHeader();
    void message(const char* text, const char* title = "RaDiff Voice Processor");

    // --- file panel -----------------------------------------------------------------------
    void chooseInputFile();
    void chooseOutputFile();
    void startJob(bool analyzeOnly);
    void pollJob();
    void setStatus(const std::string& text);
    void setResult(const std::string& text);

    // --- layout ---------------------------------------------------------------------------
    void layout(iplug::igraphics::IGraphics* g);
    void layoutModulePanel(iplug::igraphics::IGraphics* g, const iplug::igraphics::IRECT& bounds, radiff::core::Module m,
                           int columns, float cellHeight);
    void layoutEqPanel(iplug::igraphics::IGraphics* g, const iplug::igraphics::IRECT& bounds);
    void layoutHeader(iplug::igraphics::IGraphics* g, const iplug::igraphics::IRECT& bounds);
    void layoutMeters(iplug::igraphics::IGraphics* g, const iplug::igraphics::IRECT& bounds);
    void layoutFileBar(iplug::igraphics::IGraphics* g, const iplug::igraphics::IRECT& bounds);
    iplug::igraphics::IControl* makeParamControl(const iplug::igraphics::IRECT& cell, radiff::core::ParamId id,
                                                 const char* label);
    void attachPanel(iplug::igraphics::IGraphics* g, const iplug::igraphics::IRECT& bounds, const char* title,
                     std::optional<radiff::core::ParamId> enableParam);

    // --- audio ----------------------------------------------------------------------------
    radiff::core::Engine engine_;
    std::array<std::vector<float>, 2> scratch_{};
    std::array<float*, 2> scratchPtrs_{};
    std::size_t scratchFrames_ = 0;
    std::atomic<int> reportedLatency_{-1};

    // --- UI state -------------------------------------------------------------------------
    radiff::ui::PresetLibrary presets_;
    radiff::ui::JobRunner job_;
    std::unique_ptr<radiff::core::modules::Equalizer> eqCurve_; ///< UI-side copy for the response plot
    bool eqCurveDirty_ = true;
    bool wasDirty_ = false;
    std::filesystem::path fileIn_, fileOut_;
    int outputFormat_ = 0; ///< 0 keep, 1 pcm16, 2 pcm24, 3 pcm32, 4 float32
    bool overwrite_ = false;
    bool jobWasAnalyzeOnly_ = false;
    std::string status_, result_;
    std::string lastMessage_;
    std::string snapshotPath_; ///< RADIFF_SCREENSHOT=<png>: render once, write, exit (docs/tests)
    int snapshotTicks_ = 0;
    std::filesystem::path autoIn_, autoOut_; ///< RADIFF_AUTO_PROCESS_IN/OUT: run one file job through the GUI path
    bool autoStarted_ = false, autoFailed_ = false;
    int autoDoneTick_ = 0;
};
