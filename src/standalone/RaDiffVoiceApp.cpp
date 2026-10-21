#include "RaDiffVoiceApp.h"
#include "IPlug_include_in_plug_src.h"

#include "IControls.h"
#include "RaDiffControls.h"
#include "radiff/platform/Paths.h"
#include "radiff/ui/ParamText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

using namespace iplug;
using namespace igraphics;
using namespace radiff;
using namespace radiff::standalone;
using core::Module;
using core::ParamId;
using core::ParamInfo;
using core::ParamUnit;

namespace {

constexpr int kStateMagic = 0x52445653; // "RDVS"
constexpr int kStateVersion = 1;

constexpr float kMargin = 8.f;
constexpr float kGap = 8.f;
constexpr float kHeaderH = 56.f;
constexpr float kTitleH = 24.f;
constexpr float kRightColW = 244.f;
constexpr float kFileBarH = 82.f;
constexpr float kPreviewBarH = 46.f;

std::string formatTime(double seconds) {
    const int total10 = static_cast<int>(std::lround(std::max(0.0, seconds) * 10.0));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d.%d", total10 / 600, (total10 / 10) % 60, total10 % 10);
    return buf;
}

const char* kFormatNames[] = {"Keep", "PCM 16", "PCM 24", "PCM 32", "Float 32"};

IVStyle baseStyle() {
    const Theme& t = theme();
    IVStyle s;
    s.colorSpec = IVColorSpec{t.panel, t.knob, t.accent, t.knobRing, t.panelTitle, COLOR_BLACK_DROP_SHADOW,
                              t.accent, t.textDim, t.text};
    s.labelText = uiText(11.f, t.text, EAlign::Center, EVAlign::Top);
    s.valueText = uiText(11.f, t.accent, EAlign::Center, EVAlign::Bottom);
    s.drawShadows = false;
    s.drawFrame = false;
    s.roundness = 0.25f;
    s.frameThickness = 1.f;
    return s;
}

IVStyle buttonStyle() {
    const Theme& t = theme();
    IVStyle s = baseStyle();
    s.colorSpec = IVColorSpec{t.panel, t.panelTitle, t.accent, t.frame, t.knobRing, COLOR_BLACK_DROP_SHADOW,
                              t.accent, t.textDim, t.text};
    s.labelText = uiText(11.f, t.text);
    s.valueText = uiText(11.f, t.text);
    s.drawFrame = true;
    s.roundness = 0.3f;
    return s;
}

IVStyle toggleStyle() {
    const Theme& t = theme();
    IVStyle s = baseStyle();
    s.colorSpec = IVColorSpec{t.panel, t.knob, t.accent, t.frame, t.knobRing, COLOR_BLACK_DROP_SHADOW,
                              t.accent, t.textDim, t.text};
    s.valueText = uiText(10.f, t.text);
    s.drawFrame = true;
    s.roundness = 0.3f;
    return s;
}

std::vector<ParamId> moduleParams(Module m, bool includeEnabled) {
    std::vector<ParamId> ids;
    for (const auto& info : core::paramTable()) {
        if (info.module != m || info.band >= 0)
            continue;
        if (!includeEnabled && info.key == "enabled")
            continue;
        ids.push_back(info.id);
    }
    return ids;
}

std::optional<ParamId> enableParamOf(Module m) {
    return core::paramIdFor(m, -1, "enabled");
}

double fineStep(const ParamInfo& info) {
    if (info.step > 0.f)
        return static_cast<double>(info.step);
    switch (info.unit) {
    case ParamUnit::Hertz: return info.maxValue >= 1000.f ? 1.0 : 0.1;
    case ParamUnit::Milliseconds: return info.minValue < 1.f ? 0.01 : 0.1;
    case ParamUnit::Decibels: return 0.1;
    case ParamUnit::Ratio: return 0.1;
    case ParamUnit::Q:
    case ParamUnit::Fraction: return 0.01;
    case ParamUnit::Octaves: return 0.05;
    case ParamUnit::None: return 0.1;
    case ParamUnit::DbPerOctave: return 12.0;
    case ParamUnit::Boolean:
    case ParamUnit::Enum: return 1.0;
    }
    return 0.01;
}

std::string formatDb(double v, int decimals = 1) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Construction / parameters
// ---------------------------------------------------------------------------------------------

RaDiffVoiceApp::RaDiffVoiceApp(const InstanceInfo& info) : Plugin(info, MakeConfig(kNumParams, kNumPresets)) {
    initParameters();
    initPresetLibrary();
    if (const char* snap = std::getenv("RADIFF_SCREENSHOT"); snap && *snap)
        snapshotPath_ = snap;
    if (const char* in = std::getenv("RADIFF_AUTO_PROCESS_IN"); in && *in) {
        autoIn_ = platform::pathFromUtf8(in);
        if (const char* out = std::getenv("RADIFF_AUTO_PROCESS_OUT"); out && *out)
            autoOut_ = platform::pathFromUtf8(out);
    }
    if (const char* play = std::getenv("RADIFF_AUTO_PLAY"); play && *play)
        autoPlay_ = platform::pathFromUtf8(play);

    // Start from the owner-approved factory default when it is available (round 2 of the
    // listening tests chose the Balanced/medium tier); otherwise from engine defaults.
    for (const auto& e : presets_.factory())
        if (e.name == "Broadcast Voice - Balanced") {
            loadPresetFile(e.file);
            break;
        }

#if IPLUG_EDITOR
    mMakeGraphicsFunc = [&]() {
        return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
    };
    mLayoutFunc = [&](IGraphics* g) { layout(g); };
#endif
}

RaDiffVoiceApp::~RaDiffVoiceApp() = default;

void RaDiffVoiceApp::initParameters() {
    for (const auto& info : core::paramTable()) {
        const int i = static_cast<int>(info.id);
        const std::string name(info.name);
        const std::string group(core::moduleName(info.module));
        if (info.isBoolean()) {
            GetParam(i)->InitBool(name.c_str(), info.defaultValue >= 0.5f, "", 0, group.c_str(), "off", "on");
        } else if (info.isEnum()) {
            GetParam(i)->InitEnum(name.c_str(), static_cast<int>(info.defaultValue + 0.5f),
                                  static_cast<int>(info.enumNames.size()), "", 0, group.c_str());
            for (std::size_t e = 0; e < info.enumNames.size(); ++e)
                GetParam(i)->SetDisplayText(static_cast<double>(e), std::string(info.enumNames[e]).c_str());
        } else {
            const std::string unit(core::unitSuffix(info.unit));
            const double step = fineStep(info);
            if (info.unit == ParamUnit::Hertz) {
                GetParam(i)->InitDouble(name.c_str(), info.defaultValue, info.minValue, info.maxValue, step,
                                        unit.c_str(), 0, group.c_str(), IParam::ShapeExp());
            } else if (info.unit == ParamUnit::Milliseconds && info.maxValue / std::max(info.minValue, 0.1f) > 50.f) {
                GetParam(i)->InitDouble(name.c_str(), info.defaultValue, info.minValue, info.maxValue, step,
                                        unit.c_str(), 0, group.c_str(), IParam::ShapePowCurve(2.5));
            } else {
                GetParam(i)->InitDouble(name.c_str(), info.defaultValue, info.minValue, info.maxValue, step,
                                        unit.c_str(), info.step > 0.f ? IParam::kFlagStepped : 0, group.c_str());
            }
        }
    }
}

core::ParameterSet RaDiffVoiceApp::liveParameters() const {
    core::ParameterSet p;
    for (int i = 0; i < kNumParams; ++i)
        p.set(static_cast<ParamId>(i), static_cast<float>(GetParam(i)->Value()));
    return p;
}

void RaDiffVoiceApp::applyParameterSet(const core::ParameterSet& set) {
    for (int i = 0; i < kNumParams; ++i)
        GetParam(i)->Set(static_cast<double>(set.get(static_cast<ParamId>(i))));
    OnParamReset(EParamSource::kPresetRecall);
    if (GetUI())
        SendCurrentParamValuesFromDelegate();
    eqCurveDirty_ = true;
}

// ---------------------------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------------------------

void RaDiffVoiceApp::OnReset() {
    core::EngineConfig cfg;
    cfg.sampleRate = GetSampleRate();
    cfg.maxBlockFrames = static_cast<std::size_t>(std::max(GetBlockSize(), 64));
    cfg.numChannels = 2;
    scratchFrames_ = cfg.maxBlockFrames;
    for (std::size_t c = 0; c < 2; ++c) {
        scratch_[c].assign(scratchFrames_, 0.0f);
        scratchPtrs_[c] = scratch_[c].data();
    }
    engine_.prepare(cfg);
    engine_.setParameters(liveParameters());
    // Live input: track the speech level online so speechRelative presets work without a file
    // analysis pass (first ~1 s of speech runs on the absolute threshold; see docs/DSP_DESIGN.md).
    engine_.setSpeechTracking(true, 20.0);
    reportedLatency_.store(-1, std::memory_order_relaxed);
}

void RaDiffVoiceApp::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
    const int nIn = NInChansConnected();
    const int nOut = NOutChansConnected();
    if (nFrames <= 0 || scratchFrames_ == 0) {
        for (int c = 0; c < nOut; ++c)
            std::fill(outputs[c], outputs[c] + std::max(nFrames, 0), 0.0);
        return;
    }
    // Parameter snapshot for this block (iPlug params are atomics; Engine ignores unchanged sets).
    engine_.setParameters(liveParameters());

    for (int pos = 0; pos < nFrames; pos += static_cast<int>(scratchFrames_)) {
        const int n = std::min(static_cast<int>(scratchFrames_), nFrames - pos);
        // Preview: the file replaces the device input (the microphone is muted meanwhile), so
        // the engine, meters, bypass and latency behave exactly as for live input.
        const bool preview = player_.isPlaying() &&
                             player_.render(scratchPtrs_.data(), 2, static_cast<std::size_t>(n), GetSampleRate());
        if (!preview) {
            for (int c = 0; c < 2; ++c) {
                const int src = nIn > 0 ? std::min(c, nIn - 1) : -1;
                float* dst = scratch_[static_cast<std::size_t>(c)].data();
                if (src < 0) {
                    std::fill(dst, dst + n, 0.0f);
                } else {
                    const sample* s = inputs[src] + pos;
                    for (int i = 0; i < n; ++i)
                        dst[i] = static_cast<float>(s[i]);
                }
            }
        }
        engine_.process(core::AudioView{scratchPtrs_.data(), 2, static_cast<std::size_t>(n)});
        for (int c = 0; c < nOut; ++c) {
            const float* s = scratch_[static_cast<std::size_t>(std::min(c, 1))].data();
            sample* d = outputs[c] + pos;
            for (int i = 0; i < n; ++i)
                d[i] = static_cast<sample>(s[i]);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Idle: meters, latency, background job, dirty state
// ---------------------------------------------------------------------------------------------

void RaDiffVoiceApp::OnIdle() {
    const int lat = static_cast<int>(engine_.latencyFrames());
    if (lat != reportedLatency_.load(std::memory_order_relaxed)) {
        reportedLatency_.store(lat, std::memory_order_relaxed);
        SetLatency(lat);
    }
    pollJob();

    IGraphics* g = GetUI();
    if (!g)
        return;

    // Automation hooks (documentation screenshots / smoke test of the file path), env-driven.
    ++snapshotTicks_;
    if (!autoIn_.empty() && !autoStarted_ && snapshotTicks_ >= 30) {
        autoStarted_ = true;
        fileIn_ = autoIn_;
        fileOut_ = autoOut_.empty() ? radiff::ui::defaultOutputPath(autoIn_) : autoOut_;
        overwrite_ = true;
        if (auto* t = g->GetControlWithTag(kTagOverwrite))
            t->SetValue(1.0);
        if (auto* t = dynamic_cast<PathTextControl*>(g->GetControlWithTag(kTagFileIn)))
            t->setPath(platform::pathToUtf8(fileIn_));
        if (auto* t = dynamic_cast<PathTextControl*>(g->GetControlWithTag(kTagFileOut)))
            t->setPath(platform::pathToUtf8(fileOut_));
        startJob(false);
    }
    const bool autoFinished = autoStarted_ && autoDoneTick_ > 0;
    if (autoFinished && snapshotPath_.empty() && snapshotTicks_ >= autoDoneTick_ + 5)
        std::exit(autoFailed_ ? 1 : 0);
    // RADIFF_AUTO_PLAY: preview a file for ~3 s through the live engine, report, (screenshot,) exit.
    if (!autoPlay_.empty() && !autoPlayStarted_ && snapshotTicks_ >= 30) {
        autoPlayStarted_ = true;
        previewLoad(autoPlay_);
        player_.seek(std::min(0.9, 10.0 / std::max(1.0, player_.durationSeconds()))); // skip a quiet intro
        player_.play();
        autoPlayDoneTick_ = snapshotTicks_ + 150; // ~3 s at the 20 ms idle rate
    }
    if (autoPlayStarted_ && !autoPlayReported_) {
        const core::Meters mm = engine_.meters();
        autoPlayMaxIn_ = std::max({autoPlayMaxIn_, mm.inputPeak[0], mm.inputPeak[1]});
        autoPlayMaxOut_ = std::max({autoPlayMaxOut_, mm.outputPeak[0], mm.outputPeak[1]});
    }
    if (autoPlayStarted_ && !autoPlayReported_ && snapshotTicks_ >= autoPlayDoneTick_) {
        autoPlayReported_ = true;
        const core::Meters mm = engine_.meters();
        auto db = [](float lin) { return lin > 1e-6f ? 20.0 * std::log10(static_cast<double>(lin)) : -120.0; };
        std::printf("RaDiff auto-play: file=%s playing=%d position=%.2fs device=%.0fHz in=%.1f/%.1f dBFS out=%.1f/%.1f dBFS "
                    "max-in=%.1f max-out=%.1f dBFS gr exp=%.1f ds=%.1f c1=%.1f c2=%.1f dB\n",
                    player_.info().name.c_str(), player_.isPlaying() ? 1 : 0, player_.positionSeconds(), GetSampleRate(),
                    db(mm.inputPeak[0]), db(mm.inputPeak[1]), db(mm.outputPeak[0]), db(mm.outputPeak[1]),
                    db(autoPlayMaxIn_), db(autoPlayMaxOut_),
                    static_cast<double>(mm.expanderReductionDb), static_cast<double>(mm.deesserReductionDb),
                    static_cast<double>(mm.compressor1ReductionDb), static_cast<double>(mm.compressor2ReductionDb));
        std::fflush(stdout);
    }
    if (autoPlayReported_ && snapshotPath_.empty() && snapshotTicks_ >= autoPlayDoneTick_ + 5)
        std::exit(0);
    if (!snapshotPath_.empty()) {
        if (auto* s = dynamic_cast<SnapshotControl*>(g->GetControlWithTag(kTagSnapshot))) {
            if (s->done()) {
                std::fprintf(stderr, "RaDiff: screenshot %s -> %s\n", s->failed() ? "FAILED" : "written", snapshotPath_.c_str());
                std::exit(s->failed() || autoFailed_ ? 1 : 0);
            }
            bool due = false;
            if (!autoIn_.empty())
                due = autoFinished && snapshotTicks_ == autoDoneTick_ + 10;
            else if (!autoPlay_.empty())
                due = autoPlayReported_ && snapshotTicks_ == autoPlayDoneTick_ + 2;
            else
                due = snapshotTicks_ == 75; // ~1.5 s: fonts loaded, first frames drawn
            if (due)
                s->request(snapshotPath_);
        }
    }
    pollPreview();

    const core::Meters m = engine_.meters();
    if (auto* c = dynamic_cast<LevelMeterControl*>(g->GetControlWithTag(kTagMeterIn)))
        c->setLevels(m.inputPeak.data());
    if (auto* c = dynamic_cast<LevelMeterControl*>(g->GetControlWithTag(kTagMeterOut)))
        c->setLevels(m.outputPeak.data());
    if (auto* c = dynamic_cast<ReductionMeterControl*>(g->GetControlWithTag(kTagGrExpander)))
        c->setReduction(m.expanderReductionDb);
    if (auto* c = dynamic_cast<ReductionMeterControl*>(g->GetControlWithTag(kTagGrComp1)))
        c->setReduction(m.compressor1ReductionDb);
    if (auto* c = dynamic_cast<ReductionMeterControl*>(g->GetControlWithTag(kTagGrDeesser)))
        c->setReduction(m.deesserReductionDb);
    if (auto* c = dynamic_cast<ReductionMeterControl*>(g->GetControlWithTag(kTagGrComp2)))
        c->setReduction(m.compressor2ReductionDb);

    if (auto* t = dynamic_cast<ITextControl*>(g->GetControlWithTag(kTagExpThresholdText))) {
        const auto& exp = engine_.expander();
        std::string s = "Threshold in use: " + formatDb(exp.effectiveThresholdDb()) + " dBFS";
        const bool relative = liveParameters().getInt(ParamId::ExpThresholdMode) == 1;
        s += relative ? (exp.speechLevelDb() ? " (speech-relative, tracked live)"
                                             : " (absolute until ~1 s of speech has been heard)")
                      : " (absolute)";
        if (std::strcmp(t->GetStr(), s.c_str()) != 0)
            t->SetStr(s.c_str());
    }
    if (auto* t = dynamic_cast<ITextControl*>(g->GetControlWithTag(kTagLatencyText))) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Latency %d smp (%.1f ms)", lat,
                      GetSampleRate() > 0 ? 1000.0 * lat / GetSampleRate() : 0.0);
        if (std::strcmp(t->GetStr(), buf) != 0)
            t->SetStr(buf);
    }
    if (eqCurveDirty_) {
        eqCurveDirty_ = false;
        if (eqCurve_) {
            eqCurve_->update(liveParameters());
            if (auto* c = g->GetControlWithTag(kTagEqCurve))
                c->SetDirty(false);
        }
    }
    const bool dirty = presets_.isDirty(liveParameters());
    if (dirty != wasDirty_) {
        wasDirty_ = dirty;
        refreshPresetHeader();
    }
}

void RaDiffVoiceApp::OnParamChangeUI(int paramIdx, EParamSource) {
    if (paramIdx >= 0 && paramIdx < kNumParams && core::paramInfo(static_cast<ParamId>(paramIdx)).module == Module::Eq)
        eqCurveDirty_ = true;
}

void RaDiffVoiceApp::OnUIOpen() {
    Plugin::OnUIOpen();
    eqCurveDirty_ = true;
    refreshPresetHeader();
}

// ---------------------------------------------------------------------------------------------
// State (standalone session / future DAW chunks): values keyed by their portable path
// ---------------------------------------------------------------------------------------------

bool RaDiffVoiceApp::SerializeState(IByteChunk& chunk) const {
    const int magic = kStateMagic, version = kStateVersion, count = kNumParams;
    chunk.Put(&magic);
    chunk.Put(&version);
    chunk.Put(&count);
    for (int i = 0; i < kNumParams; ++i) {
        const auto id = static_cast<ParamId>(i);
        chunk.PutStr(core::paramPath(id).c_str());
        const double v = GetParam(i)->Value();
        chunk.Put(&v);
    }
    const auto& p = presets_.current();
    chunk.PutStr(p.name.c_str());
    chunk.PutStr(p.description.c_str());
    chunk.PutStr(p.author.c_str());
    return true;
}

int RaDiffVoiceApp::UnserializeState(const IByteChunk& chunk, int startPos) {
    int pos = startPos;
    int magic = 0, version = 0, count = 0;
    pos = chunk.Get(&magic, pos);
    if (pos < 0 || magic != kStateMagic)
        return Plugin::UnserializeState(chunk, startPos); // older/plain iPlug layout
    pos = chunk.Get(&version, pos);
    pos = chunk.Get(&count, pos);
    if (pos < 0 || count < 0)
        return -1;
    core::ParameterSet set; // unknown paths keep defaults
    for (int i = 0; i < count && pos >= 0; ++i) {
        WDL_String path;
        pos = chunk.GetStr(path, pos);
        double v = 0.0;
        pos = chunk.Get(&v, pos);
        if (pos < 0)
            break;
        if (auto id = core::paramIdForPath(path.Get()))
            set.set(*id, static_cast<float>(v));
    }
    WDL_String name, desc, author;
    if (pos >= 0) pos = chunk.GetStr(name, pos);
    if (pos >= 0) pos = chunk.GetStr(desc, pos);
    if (pos >= 0) pos = chunk.GetStr(author, pos);
    for (int i = 0; i < kNumParams; ++i)
        GetParam(i)->Set(static_cast<double>(set.get(static_cast<ParamId>(i))));
    auto& cur = presets_.current();
    cur.name = name.Get();
    cur.description = desc.Get();
    cur.author = author.Get();
    OnParamReset(EParamSource::kPresetRecall);
    eqCurveDirty_ = true;
    return pos;
}

// ---------------------------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------------------------

void RaDiffVoiceApp::initPresetLibrary() {
    std::vector<std::filesystem::path> factory;
    const auto bundled = platform::bundledResourceDirectory();
    if (!bundled.empty())
        factory.push_back(bundled / "presets");
#ifdef RADIFF_DEV_FACTORY_PRESETS
    factory.push_back(platform::pathFromUtf8(RADIFF_DEV_FACTORY_PRESETS)); // build-tree fallback
#endif
    presets_.setFactoryDirectories(std::move(factory));
    presets_.setUserDirectory(platform::userDataDirectory() / "Presets");
    presets_.rescan();
}

void RaDiffVoiceApp::message(const char* text, const char* title) {
    lastMessage_ = text;
    if (GetUI())
        GetUI()->ShowMessageBox(text, title, kMB_OK);
}

void RaDiffVoiceApp::loadPresetFile(const std::filesystem::path& file) {
    core::ParameterSet live;
    const auto r = presets_.load(file, live);
    if (!r.ok()) {
        message(("Could not load preset:\n" + r.error).c_str());
        return;
    }
    applyParameterSet(live);
    std::string status = "Loaded " + presets_.current().name;
    if (!r.warnings.empty())
        status += " (" + std::to_string(r.warnings.size()) + " warning(s): " + r.warnings.front() + ")";
    setStatus(status);
    refreshPresetHeader();
}

void RaDiffVoiceApp::newPreset() {
    presets_.resetToDefaults();
    presets_.current().name = "Untitled";
    applyParameterSet(core::ParameterSet{});
    setStatus("New preset (engine defaults)");
    refreshPresetHeader();
}

void RaDiffVoiceApp::loadPresetDialog() {
    IGraphics* g = GetUI();
    if (!g)
        return;
    WDL_String fileName, path;
    const auto start = presets_.user();
    path.Set(platform::pathToUtf8(presets_.userDirectory()).c_str());
    g->PromptForFile(fileName, path, EFileAction::Open, "json", [this](const WDL_String& f, const WDL_String&) {
        if (f.GetLength())
            loadPresetFile(platform::pathFromUtf8(f.Get()));
    });
}

void RaDiffVoiceApp::savePreset() {
    if (presets_.currentFile().empty() || presets_.currentIsFactory()) {
        savePresetAsDialog(); // factory presets are read-only; a copy goes to the user folder
        return;
    }
    const auto r = presets_.saveAs(presets_.currentFile(), liveParameters());
    if (!r.ok()) {
        message(("Could not save preset:\n" + r.error).c_str());
        return;
    }
    setStatus("Saved " + platform::pathToUtf8(presets_.currentFile()));
    refreshPresetHeader();
}

void RaDiffVoiceApp::savePresetAsDialog() {
    IGraphics* g = GetUI();
    if (!g)
        return;
    std::error_code ec;
    std::filesystem::create_directories(presets_.userDirectory(), ec);
    WDL_String fileName, path;
    std::string suggested = presets_.current().name.empty() ? "My Voice" : presets_.current().name;
    if (presets_.currentIsFactory())
        suggested += " (copy)";
    fileName.Set(radiff::ui::PresetLibrary::fileNameFor(suggested).c_str());
    path.Set(platform::pathToUtf8(presets_.userDirectory()).c_str());
    g->PromptForFile(fileName, path, EFileAction::Save, "json", [this](const WDL_String& f, const WDL_String&) {
        if (!f.GetLength())
            return;
        auto target = radiff::ui::PresetLibrary::withPresetExtension(platform::pathFromUtf8(f.Get()));
        // The saved file's stem becomes the preset name when the user renamed it in the dialog.
        std::string stem = platform::pathToUtf8(target.filename());
        stem.erase(stem.size() - std::string(presets::kPresetFileExtension).size());
        auto& cur = presets_.current();
        if (cur.name.empty() || cur.name == "Untitled" || presets_.currentIsFactory())
            cur.name = stem;
        if (cur.author.empty())
            cur.author = "RaDiff user";
        if (presets_.currentIsFactory())
            cur.description = "User copy of a factory preset. " + cur.description;
        const auto r = presets_.saveAs(target, liveParameters());
        if (!r.ok()) {
            message(("Could not save preset:\n" + r.error).c_str());
            return;
        }
        setStatus("Saved " + platform::pathToUtf8(presets_.currentFile()));
        refreshPresetHeader();
    });
}

void RaDiffVoiceApp::deleteCurrentPreset() {
    IGraphics* g = GetUI();
    if (!g)
        return;
    if (presets_.currentFile().empty() || presets_.currentIsFactory()) {
        message("Only user presets can be deleted. Factory presets are read-only.");
        return;
    }
    const std::string q = "Delete the user preset \"" + presets_.current().name + "\"?\n" +
                          platform::pathToUtf8(presets_.currentFile());
    if (g->ShowMessageBox(q.c_str(), "Delete preset", kMB_YESNO) != kYES)
        return;
    const std::string err = presets_.deleteUserPreset(presets_.currentFile());
    if (!err.empty()) {
        message(err.c_str());
        return;
    }
    setStatus("Deleted preset");
    refreshPresetHeader();
}

void RaDiffVoiceApp::showPresetMenu(IControl* anchor) {
    IGraphics* g = GetUI();
    if (!g || !anchor)
        return;
    presets_.rescan();
    auto entries = std::make_shared<std::vector<radiff::ui::PresetEntry>>(presets_.all());
    auto* menu = new IPopupMenu("Presets");
    int idx = 0;
    bool separatorDone = false;
    for (const auto& e : *entries) {
        if (!e.factory && !separatorDone) {
            menu->AddSeparator();
            separatorDone = true;
        }
        auto* item = menu->AddItem(e.name.c_str());
        if (e.file == presets_.currentFile())
            item->SetChecked(true);
        ++idx;
    }
    if (presets_.user().empty()) {
        menu->AddSeparator();
        menu->AddItem("(no user presets yet - use Save As)", -1, IPopupMenu::Item::kDisabled);
    }
    menu->SetFunction([this, entries, menu](IPopupMenu* m) {
        if (IPopupMenu::Item* chosen = m->GetChosenItem()) {
            // Map the chosen item back to the entry by name + flag (separators shift indices).
            for (const auto& e : *entries)
                if (e.name == chosen->GetText()) {
                    loadPresetFile(e.file);
                    break;
                }
        }
        delete menu;
    });
    g->CreatePopupMenu(*anchor, *menu, anchor->GetRECT());
}

void RaDiffVoiceApp::refreshPresetHeader() {
    IGraphics* g = GetUI();
    if (!g)
        return;
    const bool dirty = presets_.isDirty(liveParameters());
    wasDirty_ = dirty;
    if (auto* b = dynamic_cast<IVButtonControl*>(g->GetControlWithTag(kTagPresetButton))) {
        std::string name = presets_.current().name.empty() ? "Untitled" : presets_.current().name;
        b->SetLabelStr((name + "  \xE2\x96\xBE").c_str());
        b->SetDirty(false);
    }
    if (auto* t = dynamic_cast<ITextControl*>(g->GetControlWithTag(kTagPresetState))) {
        std::string s;
        if (presets_.currentFile().empty())
            s = "unsaved";
        else
            s = presets_.currentIsFactory() ? "factory" : "user";
        if (dirty)
            s += " \xC2\xB7 modified";
        t->SetText(uiText(11.f, dirty ? theme().accent : theme().textDim, EAlign::Near));
        t->SetStr(s.c_str());
    }
}

// ---------------------------------------------------------------------------------------------
// File panel
// ---------------------------------------------------------------------------------------------

void RaDiffVoiceApp::setStatus(const std::string& text) {
    status_ = text;
    if (IGraphics* g = GetUI())
        if (auto* t = dynamic_cast<ITextControl*>(g->GetControlWithTag(kTagStatus)))
            t->SetStr(text.c_str());
}

void RaDiffVoiceApp::setResult(const std::string& text) {
    result_ = text;
    if (IGraphics* g = GetUI())
        if (auto* t = dynamic_cast<ITextControl*>(g->GetControlWithTag(kTagResult)))
            t->SetStr(text.c_str());
}

void RaDiffVoiceApp::chooseInputFile() {
    IGraphics* g = GetUI();
    if (!g)
        return;
    WDL_String fileName, path;
    if (!fileIn_.empty())
        path.Set(platform::pathToUtf8(fileIn_.parent_path()).c_str());
    g->PromptForFile(fileName, path, EFileAction::Open, "wav", [this](const WDL_String& f, const WDL_String&) {
        if (!f.GetLength())
            return;
        fileIn_ = platform::pathFromUtf8(f.Get());
        if (fileOut_.empty() || fileOut_ == radiff::ui::defaultOutputPath(fileIn_))
            fileOut_ = radiff::ui::defaultOutputPath(fileIn_);
        if (IGraphics* ui = GetUI()) {
            if (auto* t = dynamic_cast<PathTextControl*>(ui->GetControlWithTag(kTagFileIn)))
                t->setPath(platform::pathToUtf8(fileIn_));
            if (auto* t = dynamic_cast<PathTextControl*>(ui->GetControlWithTag(kTagFileOut)))
                t->setPath(platform::pathToUtf8(fileOut_));
        }
        setResult("");
        setStatus("Ready");
        previewLoad(fileIn_);
    });
}

void RaDiffVoiceApp::chooseOutputFile() {
    IGraphics* g = GetUI();
    if (!g)
        return;
    WDL_String fileName, path;
    const auto def = fileOut_.empty() ? (fileIn_.empty() ? std::filesystem::path{} : radiff::ui::defaultOutputPath(fileIn_)) : fileOut_;
    if (!def.empty()) {
        fileName.Set(platform::pathToUtf8(def.filename()).c_str());
        path.Set(platform::pathToUtf8(def.parent_path()).c_str());
    }
    g->PromptForFile(fileName, path, EFileAction::Save, "wav", [this](const WDL_String& f, const WDL_String&) {
        if (!f.GetLength())
            return;
        fileOut_ = platform::pathFromUtf8(f.Get());
        if (fileOut_.extension() != ".wav")
            fileOut_ += ".wav";
        if (IGraphics* ui = GetUI())
            if (auto* t = dynamic_cast<PathTextControl*>(ui->GetControlWithTag(kTagFileOut)))
                t->setPath(platform::pathToUtf8(fileOut_));
    });
}

void RaDiffVoiceApp::startJob(bool analyzeOnly) {
    if (job_.busy()) {
        setStatus("A job is already running");
        return;
    }
    if (fileIn_.empty()) {
        message("Choose an input WAV file first.");
        return;
    }
    if (!analyzeOnly && fileOut_.empty())
        fileOut_ = radiff::ui::defaultOutputPath(fileIn_);
    app::FileJob job;
    job.input = fileIn_;
    job.output = analyzeOnly ? std::filesystem::path{} : fileOut_;
    job.parameters = liveParameters();
    job.overwrite = overwrite_;
    job.analyzeOnly = analyzeOnly;
    switch (outputFormat_) {
    case 1: job.outputFormat = audioio::SampleFormat::Pcm16; break;
    case 2: job.outputFormat = audioio::SampleFormat::Pcm24; break;
    case 3: job.outputFormat = audioio::SampleFormat::Pcm32; break;
    case 4: job.outputFormat = audioio::SampleFormat::Float32; break;
    default: break;
    }
    jobWasAnalyzeOnly_ = analyzeOnly;
    if (!job_.start(std::move(job))) {
        setStatus("Could not start the job");
        return;
    }
    setResult("");
    setStatus(analyzeOnly ? "Analyzing\xE2\x80\xA6" : "Processing\xE2\x80\xA6");
    if (IGraphics* g = GetUI()) {
        if (auto* b = g->GetControlWithTag(kTagProcessButton)) b->SetDisabled(true);
        if (auto* b = g->GetControlWithTag(kTagAnalyzeButton)) b->SetDisabled(true);
    }
}

void RaDiffVoiceApp::pollJob() {
    IGraphics* g = GetUI();
    if (g)
        if (auto* p = dynamic_cast<ProgressBarControl*>(g->GetControlWithTag(kTagProgress)))
            p->setProgress(job_.progress(), job_.busy());
    auto report = job_.takeResult();
    if (!report)
        return;
    if (g) {
        if (auto* b = g->GetControlWithTag(kTagProcessButton)) b->SetDisabled(false);
        if (auto* b = g->GetControlWithTag(kTagAnalyzeButton)) b->SetDisabled(false);
    }
    const auto& r = *report;
    if (autoStarted_ && autoDoneTick_ == 0) {
        autoDoneTick_ = snapshotTicks_;
        autoFailed_ = r.exitCode != app::ExitCode::Success;
        std::fprintf(stderr, "RaDiff: auto job exit %d: %s\n", static_cast<int>(r.exitCode), r.message.c_str());
    }
    if (r.exitCode != app::ExitCode::Success) {
        setStatus("Failed (exit code " + std::to_string(static_cast<int>(r.exitCode)) + ")");
        setResult(r.message);
        return;
    }
    setStatus(jobWasAnalyzeOnly_ ? "Analysis done" : "Done (output written and verified)");
    const auto& a = r.analysis;
    char buf[512];
    if (a.hasSignal) {
        std::snprintf(buf, sizeof(buf),
                      "%.1f s, %u ch, %.0f Hz  \xC2\xB7  peak %+.1f dBFS  \xC2\xB7  speech %.1f dBFS  \xC2\xB7  noise floor %.1f dBFS  \xC2\xB7  "
                      "expander threshold %.1f dBFS (%s)%s",
                      a.durationSeconds, static_cast<unsigned>(r.inputInfo.numChannels), r.inputInfo.sampleRate, a.peakDbfs,
                      a.speechLevelDbfs, a.noiseFloorDbfs, r.expanderThresholdDbfs,
                      r.expanderThresholdIsRelative ? "speech-relative" : "absolute",
                      jobWasAnalyzeOnly_ ? "" : (std::string("  \xC2\xB7  output peak ") +
                                                 formatDb(r.peakOut > 0 ? 20.0 * std::log10(r.peakOut) : -144.0) + " dBFS").c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f s of silence (no signal); nothing to measure", a.durationSeconds);
    }
    setResult(buf);
}

// ---------------------------------------------------------------------------------------------
// Preview playback
// ---------------------------------------------------------------------------------------------

void RaDiffVoiceApp::previewLoad(const std::filesystem::path& file) {
    const std::string err = player_.load(file);
    if (!err.empty()) {
        message(("Could not load the preview file:\n" + err).c_str());
        return;
    }
    const auto info = player_.info();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s  \xC2\xB7  %.0f Hz  \xC2\xB7  %s", info.name.c_str(), info.sampleRate,
                  info.numChannels == 1 ? "mono" : "stereo");
    previewFileText_ = buf;
    if (IGraphics* g = GetUI())
        if (auto* t = dynamic_cast<ITextControl*>(g->GetControlWithTag(kTagPreviewInfo)))
            t->SetStr(previewFileText_.c_str());
    pollPreview();
}

void RaDiffVoiceApp::previewLoadDialog() {
    IGraphics* g = GetUI();
    if (!g)
        return;
    WDL_String fileName, path;
    if (!fileIn_.empty())
        path.Set(platform::pathToUtf8(fileIn_.parent_path()).c_str());
    g->PromptForFile(fileName, path, EFileAction::Open, "wav", [this](const WDL_String& f, const WDL_String&) {
        if (f.GetLength())
            previewLoad(platform::pathFromUtf8(f.Get()));
    });
}

void RaDiffVoiceApp::pollPreview() {
    IGraphics* g = GetUI();
    if (!g)
        return;
    const bool playing = player_.isPlaying();
    const bool hasFile = player_.hasFile();
    const double dur = player_.durationSeconds();
    const double pos = player_.positionSeconds();
    if (player_.takeEnded())
        setStatus("Preview reached the end of the file");
    if (auto* b = dynamic_cast<IVButtonControl*>(g->GetControlWithTag(kTagPreviewPlay))) {
        const char* want = playing ? "Pause" : "Play";
        if (std::strcmp(b->GetLabelStr(), want) != 0) {
            b->SetLabelStr(want);
            b->SetDirty(false);
        }
    }
    if (auto* bar = dynamic_cast<TransportBarControl*>(g->GetControlWithTag(kTagPreviewPosition)))
        bar->setPosition(dur > 0.0 ? pos / dur : 0.0, hasFile);
    if (auto* t = dynamic_cast<ITextControl*>(g->GetControlWithTag(kTagPreviewTime))) {
        const std::string txt = hasFile ? formatTime(pos) + " / " + formatTime(dur) : "-:--.- / -:--.-";
        if (std::strcmp(t->GetStr(), txt.c_str()) != 0)
            t->SetStr(txt.c_str());
    }
    if (auto* t = dynamic_cast<ITextControl*>(g->GetControlWithTag(kTagPreviewInfo))) {
        const char* want = hasFile ? previewFileText_.c_str() : "no file loaded";
        if (std::strcmp(t->GetStr(), want) != 0)
            t->SetStr(want);
    }
    if (playing != previewWasPlaying_) {
        previewWasPlaying_ = playing;
        if (auto* t = dynamic_cast<ITextControl*>(g->GetControlWithTag(kTagSource))) {
            t->SetText(uiText(11.f, playing ? theme().accent : theme().textDim, EAlign::Far));
            t->SetStr(playing ? "SOURCE: FILE PREVIEW" : "SOURCE: LIVE INPUT");
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------

void RaDiffVoiceApp::attachPanel(IGraphics* g, const IRECT& bounds, const char* title, std::optional<ParamId> enableParam) {
    g->AttachControl(new PanelControl(bounds, theme().panel, 5.f, title, kTitleH));
    if (enableParam) {
        const IRECT tb = bounds.GetFromTop(kTitleH).GetFromRight(58.f).GetPadded(-3.f);
        auto* t = new IVToggleControl(tb, static_cast<int>(*enableParam), "", toggleStyle().WithShowLabel(false), "OFF", "ON");
        t->SetTooltip(radiff::ui::tooltipText(*enableParam).c_str());
        g->AttachControl(t);
    }
}

IControl* RaDiffVoiceApp::makeParamControl(const IRECT& cell, ParamId id, const char* label) {
    const auto& info = core::paramInfo(id);
    const int idx = static_cast<int>(id);
    IControl* c = nullptr;
    if (info.isBoolean()) {
        const IRECT r = cell.GetCentredInside(std::min(cell.W() - 6.f, 84.f), 44.f);
        c = new IVToggleControl(r, idx, label, toggleStyle(), "OFF", "ON");
    } else if (info.isEnum()) {
        std::vector<const char*> opts;
        for (auto n : info.enumNames)
            opts.push_back(n.data()); // registry names are string literals
        const IRECT r = cell.GetCentredInside(std::min(cell.W() - 4.f, 110.f), 44.f);
        std::size_t longest = 0;
        for (auto n : info.enumNames)
            longest = std::max(longest, n.size());
        if (opts.size() <= 2 && longest <= 5)
            c = new IVTabSwitchControl(r, idx, opts, label, toggleStyle());
        else
            c = new IVMenuButtonControl(r, idx, label, buttonStyle().WithValueText(uiText(10.f, theme().text)));
    } else {
        c = new IVKnobControl(cell.GetPadded(-2.f), idx, label, baseStyle(), true);
    }
    c->SetTooltip(radiff::ui::tooltipText(id).c_str());
    return c;
}

void RaDiffVoiceApp::layoutModulePanel(IGraphics* g, const IRECT& bounds, Module m, int columns, float cellHeight) {
    attachPanel(g, bounds, std::string(core::moduleName(m)).c_str(), enableParamOf(m));
    const auto ids = moduleParams(m, false);
    const IRECT inner = bounds.GetReducedFromTop(kTitleH + 4.f).GetHPadded(-4.f);
    const float cellW = inner.W() / static_cast<float>(columns);
    int i = 0;
    for (ParamId id : ids) {
        const int row = i / columns, col = i % columns;
        const IRECT cell(inner.L + static_cast<float>(col) * cellW, inner.T + static_cast<float>(row) * cellHeight,
                         inner.L + static_cast<float>(col + 1) * cellW, inner.T + static_cast<float>(row + 1) * cellHeight);
        const std::string label = radiff::ui::shortLabel(core::paramInfo(id));
        g->AttachControl(makeParamControl(cell, id, label.c_str()));
        ++i;
    }
    if (m == Module::Expander) {
        // Resolved threshold read-out under the grid.
        const int rows = (static_cast<int>(ids.size()) + columns - 1) / columns;
        const IRECT tr(inner.L + 4.f, inner.T + static_cast<float>(rows) * cellHeight + 2.f, inner.R, bounds.B - 4.f);
        auto* t = new ITextControl(tr, "", uiText(10.f, theme().textDim, EAlign::Near));
        t->SetTooltip("The threshold the expander is actually using right now. speechRelative needs a speech-level measurement (file mode provides it via the analysis pass).");
        g->AttachControl(t, kTagExpThresholdText);
    }
}

void RaDiffVoiceApp::layoutEqPanel(IGraphics* g, const IRECT& bounds) {
    attachPanel(g, bounds, "Equalizer", enableParamOf(Module::Eq));
    const Theme& t = theme();
    const IRECT inner = bounds.GetReducedFromTop(kTitleH + 6.f).GetHPadded(-8.f).GetReducedFromBottom(6.f);
    const float curveH = 112.f;
    const IRECT curveR = inner.GetFromTop(curveH);

    // Response curve: 20 Hz .. 20 kHz log, -18 .. +18 dB.
    if (!eqCurve_) {
        eqCurve_ = std::make_unique<core::modules::Equalizer>();
        eqCurve_->prepare(GetSampleRate() > 0 ? GetSampleRate() : 48000.0, 1);
    }
    eqCurve_->update(liveParameters());
    auto* plot = new IVPlotControl(
        curveR,
        {{t.accent, [this](double x) {
              if (!eqCurve_)
                  return 0.0;
              const double hz = 20.0 * std::pow(1000.0, x);
              return std::clamp(eqCurve_->magnitudeDb(hz) / 18.0, -1.0, 1.0);
          }}},
        192, "", baseStyle().WithDrawFrame(true).WithColor(kBG, t.meterBack).WithColor(kFR, t.frame), -1.f, 1.f, false);
    plot->SetTooltip("Combined magnitude response of the enabled bands, 20 Hz - 20 kHz (log), \xC2\xB1" "18 dB.");
    g->AttachControl(plot, kTagEqCurve);
    // Axis captions
    g->AttachControl(new ITextControl(curveR.GetFromTop(12.f).GetFromLeft(50.f).GetHShifted(3.f), "+18 dB", uiText(9.f, t.textDim, EAlign::Near)));
    g->AttachControl(new ITextControl(curveR.GetFromBottom(12.f).GetFromLeft(50.f).GetHShifted(3.f), "-18 dB", uiText(9.f, t.textDim, EAlign::Near)));
    g->AttachControl(new ITextControl(curveR.GetFromBottom(12.f).GetFromRight(120.f).GetHShifted(-3.f), "20 Hz  \xC2\xB7  1 kHz  \xC2\xB7  20 kHz", uiText(9.f, t.textDim, EAlign::Far)));

    const IRECT rowsR = inner.GetReducedFromTop(curveH + 4.f);
    const float headH = 14.f;
    const IRECT head = rowsR.GetFromTop(headH);
    const IRECT bands = rowsR.GetReducedFromTop(headH);
    const float rowH = bands.H() / 5.f;
    // Columns: band | on | type | freq | gain | q
    const float wBand = 22.f, wOn = 54.f, wType = 96.f;
    const float wRest = (bands.W() - wBand - wOn - wType) / 3.f;
    auto col = [&](const IRECT& r, int i) {
        float x = r.L;
        const float ws[] = {wBand, wOn, wType, wRest, wRest, wRest};
        for (int k = 0; k < i; ++k)
            x += ws[k];
        return IRECT(x, r.T, x + ws[i], r.B);
    };
    const IText ht = uiText(10.f, t.textDim);
    g->AttachControl(new ITextControl(col(head, 1), "On", ht));
    g->AttachControl(new ITextControl(col(head, 2), "Type", ht));
    g->AttachControl(new ITextControl(col(head, 3), "Frequency (Hz)", ht));
    g->AttachControl(new ITextControl(col(head, 4), "Gain (dB)", ht));
    g->AttachControl(new ITextControl(col(head, 5), "Q", ht));

    IVStyle nb = baseStyle();
    nb.valueText = uiText(11.f, t.text);
    nb.labelText = uiText(10.f, t.textDim);
    nb.colorSpec = IVColorSpec{t.meterBack, t.knob, t.accent, t.frame, t.panelTitle, COLOR_BLACK_DROP_SHADOW, t.accent, t.textDim, t.text};
    for (int b = 0; b < core::kNumEqBands; ++b) {
        const IRECT row(bands.L, bands.T + static_cast<float>(b) * rowH, bands.R, bands.T + static_cast<float>(b + 1) * rowH);
        char num[4];
        std::snprintf(num, sizeof(num), "%d", b + 1);
        g->AttachControl(new ITextControl(col(row, 0), num, uiText(12.f, t.text)));
        const auto pOn = core::ParameterSet::eqParam(b, "enabled");
        const auto pType = core::ParameterSet::eqParam(b, "type");
        const auto pHz = core::ParameterSet::eqParam(b, "frequencyHz");
        const auto pGain = core::ParameterSet::eqParam(b, "gainDb");
        const auto pQ = core::ParameterSet::eqParam(b, "q");
        auto* on = new IVToggleControl(col(row, 1).GetCentredInside(46.f, 22.f), static_cast<int>(pOn), "", toggleStyle().WithShowLabel(false), "OFF", "ON");
        on->SetTooltip(radiff::ui::tooltipText(pOn).c_str());
        g->AttachControl(on);
        auto* type = new IVMenuButtonControl(col(row, 2).GetCentredInside(90.f, 22.f), static_cast<int>(pType), "", buttonStyle().WithShowLabel(false));
        type->SetTooltip(radiff::ui::tooltipText(pType).c_str());
        g->AttachControl(type);
        auto numberBox = [&](const IRECT& r, ParamId id, const char* fmt) {
            auto* c = new IVNumberBoxControl(r.GetCentredInside(std::min(r.W() - 8.f, 96.f), 22.f), static_cast<int>(id), nullptr, "",
                                             nb.WithShowLabel(false), false, 50.0, 1.0, 100.0, fmt, false);
            c->SetTooltip((radiff::ui::tooltipText(id) + "\nDrag up/down, double-click to type").c_str());
            g->AttachControl(c);
        };
        numberBox(col(row, 3), pHz, "%0.0f");
        numberBox(col(row, 4), pGain, "%+0.1f");
        numberBox(col(row, 5), pQ, "%0.2f");
    }
}

void RaDiffVoiceApp::layoutHeader(IGraphics* g, const IRECT& bounds) {
    const Theme& t = theme();
    g->AttachControl(new PanelControl(bounds, t.header, 5.f));
    IRECT r = bounds.GetPadded(-10.f, -8.f, -10.f, -8.f);
    // Title
    g->AttachControl(new ITextControl(r.GetFromLeft(230.f), "RaDiff Voice Processor", uiText(17.f, t.text, EAlign::Near)));
    g->AttachControl(new ITextControl(r.GetFromLeft(230.f).GetFromBottom(12.f), "broadcast voice channel strip  \xC2\xB7  v" PLUG_VERSION_STR,
                                      uiText(9.f, t.textDim, EAlign::Near, EVAlign::Bottom)));
    float x = r.L + 244.f;
    auto button = [&](float w, const char* label, IActionFunction fn, int tag = kNoTag, const char* tip = nullptr) {
        const IRECT br(x, r.T + 6.f, x + w, r.B - 6.f);
        auto* b = new IVButtonControl(br, std::move(fn), label, buttonStyle(), true, false);
        if (tip)
            b->SetTooltip(tip);
        g->AttachControl(b, tag);
        x += w + 6.f;
        return b;
    };
    g->AttachControl(new ITextControl(IRECT(x, r.T, x + 52.f, r.B), "PRESET", uiText(10.f, t.textDim, EAlign::Near)));
    x += 52.f;
    button(250.f, "Untitled", [this](IControl* c) { showPresetMenu(c); }, kTagPresetButton, "Choose a factory or user preset");
    auto* state = new ITextControl(IRECT(x, r.T, x + 120.f, r.B), "", uiText(11.f, t.textDim, EAlign::Near));
    g->AttachControl(state, kTagPresetState);
    x += 124.f;
    button(56.f, "New", [this](IControl*) { newPreset(); }, kNoTag, "Reset every parameter to the engine defaults");
    button(66.f, "Load\xE2\x80\xA6", [this](IControl*) { loadPresetDialog(); }, kNoTag, "Load a .radvoice.json preset file");
    button(56.f, "Save", [this](IControl*) { savePreset(); }, kNoTag, "Save to the current user preset file (factory presets ask for a new name)");
    button(76.f, "Save As\xE2\x80\xA6", [this](IControl*) { savePresetAsDialog(); }, kNoTag, "Save as a new .radvoice.json file (user preset folder by default)");
    button(64.f, "Delete", [this](IControl*) { deleteCurrentPreset(); }, kNoTag, "Delete the current user preset file");

    // Right side: bypass + latency
    const IRECT right = r.GetFromRight(300.f);
    auto* bypass = new IVToggleControl(right.GetFromRight(110.f).GetPadded(0.f, -4.f, 0.f, -4.f), static_cast<int>(ParamId::Bypass), "",
                                       toggleStyle().WithShowLabel(false).WithColor(kPR, t.meterRed), "PROCESS", "BYPASSED");
    bypass->SetTooltip("Full-chain bypass (time-aligned with the processed path)");
    g->AttachControl(bypass);
    g->AttachControl(new ITextControl(right.GetReducedFromRight(118.f), "", uiText(10.f, t.textDim, EAlign::Far)), kTagLatencyText);
}

void RaDiffVoiceApp::layoutMeters(IGraphics* g, const IRECT& bounds) {
    attachPanel(g, bounds, "Meters", std::nullopt);
    const IRECT inner = bounds.GetReducedFromTop(kTitleH + 8.f).GetHPadded(-8.f).GetReducedFromBottom(6.f);
    float x = inner.L;
    auto place = [&](float w) {
        const IRECT r(x, inner.T, x + w, inner.B);
        x += w + 2.f;
        return r;
    };
    g->AttachControl(new LevelMeterControl(place(52.f), "IN", 2), kTagMeterIn);
    g->AttachControl(new LevelMeterControl(place(52.f), "OUT", 2), kTagMeterOut);
    g->AttachControl(new ReductionMeterControl(place(32.f), "EXP"), kTagGrExpander);
    g->AttachControl(new ReductionMeterControl(place(32.f), "DS"), kTagGrDeesser);
    g->AttachControl(new ReductionMeterControl(place(32.f), "C1"), kTagGrComp1);
    g->AttachControl(new ReductionMeterControl(place(32.f), "C2"), kTagGrComp2);
}

void RaDiffVoiceApp::layoutFileBar(IGraphics* g, const IRECT& bounds) {
    const Theme& t = theme();
    g->AttachControl(new PanelControl(bounds, t.panel, 5.f));
    const IRECT inner = bounds.GetPadded(-10.f, -6.f, -10.f, -6.f);
    const IRECT row1 = inner.GetFromTop(30.f);
    const IRECT row2 = inner.GetFromBottom(30.f);
    float x = row1.L;
    auto label = [&](const IRECT& row, float w, const char* s) {
        g->AttachControl(new ITextControl(IRECT(x, row.T, x + w, row.B), s, uiText(11.f, t.textDim, EAlign::Near)));
        x += w;
    };
    auto pathBox = [&](const IRECT& row, float w, const char* placeholder, int tag, IActionFunction browse) {
        const IRECT r(x, row.T + 3.f, x + w, row.B - 3.f);
        g->AttachControl(new PanelControl(r, t.meterBack, 3.f));
        g->AttachControl(new PathTextControl(r.GetHPadded(-6.f), placeholder), tag);
        x += w + 2.f;
        auto* b = new IVButtonControl(IRECT(x, row.T + 3.f, x + 28.f, row.B - 3.f), std::move(browse), "\xE2\x80\xA6", buttonStyle(), true, false);
        g->AttachControl(b);
        x += 28.f + 10.f;
    };
    label(row1, 44.f, "Input");
    pathBox(row1, 380.f, "choose a WAV file", kTagFileIn, [this](IControl*) { chooseInputFile(); });
    label(row1, 50.f, "Output");
    pathBox(row1, 380.f, "<input>-processed.wav", kTagFileOut, [this](IControl*) { chooseOutputFile(); });
    label(row1, 50.f, "Format");
    {
        std::vector<const char*> opts(std::begin(kFormatNames), std::end(kFormatNames));
        auto* fmt = new IVTabSwitchControl(IRECT(x, row1.T + 3.f, x + 250.f, row1.B - 3.f),
                                           [this](IControl* c) { outputFormat_ = static_cast<int>(std::lround(c->GetValue() * 4.0)); },
                                           opts, "", toggleStyle().WithShowLabel(false));
        fmt->SetTooltip("Output encoding. Keep = same as the input file.");
        g->AttachControl(fmt);
        x += 250.f + 10.f;
    }
    {
        auto* ow = new IVToggleControl(IRECT(x, row1.T + 3.f, x + 104.f, row1.B - 3.f),
                                       [this](IControl* c) { overwrite_ = c->GetValue() > 0.5; }, "", toggleStyle().WithShowLabel(false),
                                       "OVERWRITE OFF", "OVERWRITE ON", false);
        ow->SetTooltip("Allow replacing an existing output file. The input file is never overwritten.");
        g->AttachControl(ow, kTagOverwrite);
    }

    x = row2.L;
    auto button = [&](float w, const char* s, IActionFunction fn, int tag, const char* tip) {
        auto* b = new IVButtonControl(IRECT(x, row2.T + 3.f, x + w, row2.B - 3.f), std::move(fn), s, buttonStyle(), true, false);
        b->SetTooltip(tip);
        g->AttachControl(b, tag);
        x += w + 8.f;
    };
    button(90.f, "Analyze", [this](IControl*) { startJob(true); }, kTagAnalyzeButton, "Measure the input (speech level, noise floor, peak, resolved expander threshold). Writes nothing.");
    button(90.f, "Process", [this](IControl*) { startJob(false); }, kTagProcessButton, "Render the input through the current settings to the output file (transactional: temp file, verify, atomic replace).");
    g->AttachControl(new ProgressBarControl(IRECT(x, row2.T + 6.f, x + 200.f, row2.B - 6.f)), kTagProgress);
    x += 200.f + 10.f;
    g->AttachControl(new ITextControl(IRECT(x, row2.T, x + 200.f, row2.B), status_.c_str(), uiText(11.f, t.text, EAlign::Near)), kTagStatus);
    x += 206.f;
    g->AttachControl(new ITextControl(IRECT(x, row2.T, row2.R, row2.B), result_.c_str(), uiText(11.f, t.textDim, EAlign::Near)), kTagResult);
}

void RaDiffVoiceApp::layoutPreviewBar(IGraphics* g, const IRECT& bounds) {
    const Theme& t = theme();
    g->AttachControl(new PanelControl(bounds, t.panel, 5.f));
    const IRECT row = bounds.GetPadded(-10.f, -8.f, -10.f, -8.f);
    float x = row.L;
    g->AttachControl(new ITextControl(IRECT(x, row.T, x + 64.f, row.B), "PREVIEW", uiText(11.f, t.textDim, EAlign::Near)));
    x += 64.f;
    auto button = [&](float w, const char* s, IActionFunction fn, int tag, const char* tip) {
        auto* b = new IVButtonControl(IRECT(x, row.T, x + w, row.B), std::move(fn), s, buttonStyle(), true, false);
        b->SetTooltip(tip);
        g->AttachControl(b, tag);
        x += w + 6.f;
        return b;
    };
    button(70.f, "Load\xE2\x80\xA6", [this](IControl*) { previewLoadDialog(); }, kNoTag,
           "Load a WAV file for preview (choosing an Input file below loads it here too)");
    button(70.f, "Play", [this](IControl*) {
        if (!player_.hasFile()) {
            if (!fileIn_.empty())
                previewLoad(fileIn_);
            else
                previewLoadDialog();
        }
        if (player_.hasFile())
            player_.togglePlay();
        pollPreview();
    }, kTagPreviewPlay, "Play / pause the loaded file through the live engine (replaces the microphone while playing)");
    button(60.f, "Stop", [this](IControl*) { player_.stop(); pollPreview(); }, kNoTag, "Stop and rewind");
    {
        auto* loop = new IVToggleControl(IRECT(x, row.T, x + 70.f, row.B), [this](IControl* c) { player_.setLooping(c->GetValue() > 0.5); },
                                         "", toggleStyle().WithShowLabel(false), "LOOP OFF", "LOOP ON", false);
        loop->SetTooltip("Repeat the file until Stop");
        g->AttachControl(loop, kTagPreviewLoop);
        x += 70.f + 10.f;
    }
    const float timeW = 118.f, infoW = 300.f, sourceW = 170.f;
    const IRECT barR(x, row.T, row.R - timeW - infoW - sourceW - 3 * 10.f, row.B);
    g->AttachControl(new TransportBarControl(barR, [this](double f) { player_.seek(f); pollPreview(); }), kTagPreviewPosition);
    x = barR.R + 10.f;
    g->AttachControl(new ITextControl(IRECT(x, row.T, x + timeW, row.B), "-:--.- / -:--.-", uiText(11.f, t.text, EAlign::Near)), kTagPreviewTime);
    x += timeW + 10.f;
    g->AttachControl(new ITextControl(IRECT(x, row.T, x + infoW, row.B), "no file loaded", uiText(11.f, t.textDim, EAlign::Near)), kTagPreviewInfo);
    x += infoW + 10.f;
    g->AttachControl(new ITextControl(IRECT(x, row.T, row.R, row.B), "SOURCE: LIVE INPUT", uiText(11.f, t.textDim, EAlign::Far)), kTagSource);
}

void RaDiffVoiceApp::layout(IGraphics* g) {
    if (g->NControls() > 0)
        g->RemoveAllControls();
    const Theme& t = theme();
    g->AttachCornerResizer(EUIResizerMode::Scale, false);
    g->AttachPanelBackground(t.window);
    g->LoadFont(t.font, ROBOTO_FN);
    g->EnableTooltips(true);
    g->AttachTextEntryControl();
    g->AttachPopupMenuControl(uiText(12.f, t.text));
    g->AttachBubbleControl(uiText(11.f, t.text));

    const IRECT b = g->GetBounds();
    const IRECT header(kMargin, kMargin, b.R - kMargin, kMargin + kHeaderH);
    const float bodyTop = header.B + kGap;
    const IRECT fileBar(kMargin, b.B - kMargin - kFileBarH, b.R - kMargin, b.B - kMargin);
    const IRECT previewBar(kMargin, fileBar.T - kGap - kPreviewBarH, b.R - kMargin, fileBar.T - kGap);
    const float bodyBottom = previewBar.T - kGap;
    const IRECT rightCol(b.R - kMargin - kRightColW, bodyTop, b.R - kMargin, bodyBottom);
    const IRECT body(kMargin, bodyTop, rightCol.L - kGap, bodyBottom);

    layoutHeader(g, header);

    // Row A: Input | Equalizer | Expander
    const float rowAH = 360.f;
    const IRECT rowA = body.GetFromTop(rowAH);
    const IRECT rowB = body.GetReducedFromTop(rowAH + kGap);
    float x = rowA.L;
    auto slot = [&](const IRECT& row, float w) {
        const IRECT r(x, row.T, x + w, row.B);
        x += w + kGap;
        return r;
    };
    const float wInput = 160.f, wEq = 520.f;
    const float wExp = rowA.W() - wInput - wEq - 2 * kGap;
    layoutModulePanel(g, slot(rowA, wInput), Module::Input, 2, 78.f);
    layoutEqPanel(g, slot(rowA, wEq));
    layoutModulePanel(g, slot(rowA, wExp), Module::Expander, 5, 78.f);

    // Row B: De-esser | Compressor 1 | Compressor 2
    x = rowB.L;
    const float wDeess = 276.f;
    const float wComp = (rowB.W() - wDeess - 2 * kGap) / 2.f;
    layoutModulePanel(g, slot(rowB, wDeess), Module::Deesser, 3, 78.f);
    layoutModulePanel(g, slot(rowB, wComp), Module::Compressor1, 4, 78.f);
    layoutModulePanel(g, slot(rowB, wComp), Module::Compressor2, 4, 78.f);

    // Right column: Meters | Output | Enhancer
    const IRECT meters = rightCol.GetFromTop(280.f);
    const IRECT output(rightCol.L, meters.B + kGap, rightCol.R, meters.B + kGap + 100.f);
    const IRECT enhancer(rightCol.L, output.B + kGap, rightCol.R, rightCol.B);
    layoutMeters(g, meters);
    layoutModulePanel(g, output, Module::Output, 1, 72.f);
    layoutModulePanel(g, enhancer, Module::Presence, 2, (enhancer.H() - kTitleH - 8.f) / 3.f);

    layoutPreviewBar(g, previewBar);
    layoutFileBar(g, fileBar);
    g->AttachControl(new SnapshotControl(b), kTagSnapshot); // last: draws nothing unless asked

    // Restore dynamic texts after a (re)build of the UI.
    if (auto* p = dynamic_cast<PathTextControl*>(g->GetControlWithTag(kTagFileIn)))
        p->setPath(fileIn_.empty() ? "" : platform::pathToUtf8(fileIn_));
    if (auto* p = dynamic_cast<PathTextControl*>(g->GetControlWithTag(kTagFileOut)))
        p->setPath(fileOut_.empty() ? "" : platform::pathToUtf8(fileOut_));
    eqCurveDirty_ = true;
    refreshPresetHeader();
    previewWasPlaying_ = !player_.isPlaying(); // force the SOURCE text to refresh
    pollPreview();
}
