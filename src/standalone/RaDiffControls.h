#pragma once
// Small original IGraphics controls for the RaDiff standalone app: level and gain-reduction
// meters fed from the engine's meter snapshot, a progress bar, and a path display.

#include "IControl.h"
#include "IGraphics.h"

// PNG writer for the snapshot helper (static: this header is included by one translation unit).
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace radiff::standalone {

using namespace iplug;
using namespace igraphics;

struct Theme {
    IColor window{255, 24, 27, 31};
    IColor panel{255, 36, 40, 46};
    IColor panelTitle{255, 46, 51, 58};
    IColor header{255, 30, 34, 39};
    IColor text{255, 226, 229, 233};
    IColor textDim{255, 150, 156, 164};
    IColor accent{255, 240, 160, 48};
    IColor accentDim{255, 120, 84, 30};
    IColor knob{255, 58, 64, 72};
    IColor knobRing{255, 86, 94, 104};
    IColor meterGreen{255, 86, 196, 112};
    IColor meterYellow{255, 236, 196, 64};
    IColor meterRed{255, 232, 76, 64};
    IColor meterBack{255, 20, 22, 25};
    IColor frame{255, 62, 68, 76};
    const char* font = "Roboto-Regular";
};

inline const Theme& theme() {
    static const Theme t;
    return t;
}

inline IText uiText(float size, const IColor& color, EAlign align = EAlign::Center, EVAlign valign = EVAlign::Middle) {
    return IText(size, color, theme().font, align, valign);
}

/// Vertical peak meters (up to 2 channels) in dBFS with a slow-falling peak-hold line.
class LevelMeterControl : public IControl {
public:
    LevelMeterControl(const IRECT& bounds, const char* label, int numChannels, float minDb = -60.f)
        : IControl(bounds), label_(label), channels_(std::clamp(numChannels, 1, 2)), minDb_(minDb) {
        SetTooltip("Sample peak level per block, dBFS. Red above -1 dBFS.");
    }

    /// Called from the UI thread (OnIdle) with linear peak values.
    void setLevels(const float* linear) {
        bool changed = false;
        for (int c = 0; c < channels_; ++c) {
            const float db = linear[c] > 1e-6f ? 20.f * std::log10(linear[c]) : -120.f;
            // Attack instantly, fall ~ 40 dB/s at the 20 ms idle rate.
            const float fall = 0.8f;
            const float next = std::max(db, level_[c] - fall);
            if (std::fabs(next - level_[c]) > 0.05f)
                changed = true;
            level_[c] = next;
            if (db >= hold_[c]) {
                hold_[c] = db;
                holdAge_[c] = 0;
            } else if (++holdAge_[c] > 75) { // 1.5 s
                hold_[c] = std::max(minDb_, hold_[c] - 0.5f);
            }
        }
        if (changed)
            SetDirty(false);
    }

    void Draw(IGraphics& g) override {
        const Theme& t = theme();
        const IRECT r = mRECT;
        const float labelH = 14.f;
        const IRECT labelR = r.GetFromBottom(labelH);
        const IRECT body = r.GetReducedFromBottom(labelH + 2.f);
        const float scaleW = 24.f;
        const IRECT bars = body.GetReducedFromRight(scaleW);
        const IRECT scale = body.GetFromRight(scaleW);
        const float gap = 3.f;
        const float barW = (bars.W() - gap * static_cast<float>(channels_ - 1)) / static_cast<float>(channels_);

        auto yFor = [&](float db) {
            const float f = std::clamp((db - minDb_) / (0.f - minDb_), 0.f, 1.f);
            return bars.B - f * bars.H();
        };

        for (int c = 0; c < channels_; ++c) {
            const float x0 = bars.L + static_cast<float>(c) * (barW + gap);
            const IRECT bar(x0, bars.T, x0 + barW, bars.B);
            g.FillRect(t.meterBack, bar);
            const float top = yFor(level_[c]);
            // Three-colour fill: green to -12, yellow to -1, red above.
            const float yYellow = yFor(-12.f), yRed = yFor(-1.f);
            if (top < bar.B) {
                g.FillRect(t.meterGreen, IRECT(bar.L, std::max(top, yYellow), bar.R, bar.B));
                if (top < yYellow)
                    g.FillRect(t.meterYellow, IRECT(bar.L, std::max(top, yRed), bar.R, yYellow));
                if (top < yRed)
                    g.FillRect(t.meterRed, IRECT(bar.L, top, bar.R, yRed));
            }
            if (hold_[c] > minDb_) {
                const float yh = yFor(hold_[c]);
                g.FillRect(hold_[c] >= -1.f ? t.meterRed : t.text, IRECT(bar.L, yh - 1.f, bar.R, yh + 1.f));
            }
            g.DrawRect(t.frame, bar);
        }
        // Scale ticks
        const IText tick = uiText(9.f, t.textDim, EAlign::Near);
        for (float db : {0.f, -6.f, -12.f, -24.f, -36.f, -48.f, -60.f}) {
            if (db < minDb_)
                continue;
            const float y = yFor(db);
            g.DrawLine(t.textDim, scale.L, y, scale.L + 3.f, y);
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(db));
            g.DrawText(tick, buf, IRECT(scale.L + 5.f, y - 6.f, scale.R, y + 6.f));
        }
        g.DrawText(uiText(11.f, t.text), label_.c_str(), labelR);
        // Numeric peak hold readout above the label is skipped on purpose: the bars + hold line
        // are what an operator reads; exact values live in the file panel report.
    }

private:
    std::string label_;
    int channels_;
    float minDb_;
    std::array<float, 2> level_{-120.f, -120.f};
    std::array<float, 2> hold_{-120.f, -120.f};
    std::array<int, 2> holdAge_{0, 0};
};

/// Downward gain-reduction meter (0 dB at the top), positive dB input.
class ReductionMeterControl : public IControl {
public:
    ReductionMeterControl(const IRECT& bounds, const char* label, float rangeDb = 30.f)
        : IControl(bounds), label_(label), rangeDb_(rangeDb) {
        SetTooltip("Gain reduction of this stage during the last block, dB.");
    }

    void setReduction(float db) {
        const float fall = 0.6f;
        const float next = db > value_ ? db : std::max(db, value_ - fall);
        if (std::fabs(next - value_) > 0.05f) {
            value_ = next;
            SetDirty(false);
        }
    }

    void Draw(IGraphics& g) override {
        const Theme& t = theme();
        const float labelH = 14.f;
        const IRECT labelR = mRECT.GetFromBottom(labelH);
        const IRECT body = mRECT.GetReducedFromBottom(labelH + 2.f);
        const float scaleW = 24.f;
        const IRECT bar = body.GetReducedFromRight(scaleW);
        const IRECT scale = body.GetFromRight(scaleW);
        g.FillRect(t.meterBack, bar);
        const float f = std::clamp(value_ / rangeDb_, 0.f, 1.f);
        if (f > 0.f)
            g.FillRect(t.accent, IRECT(bar.L, bar.T, bar.R, bar.T + f * bar.H()));
        g.DrawRect(t.frame, bar);
        const IText tick = uiText(9.f, t.textDim, EAlign::Near);
        for (float db : {0.f, 6.f, 12.f, 20.f, 30.f}) {
            if (db > rangeDb_)
                continue;
            const float y = bar.T + (db / rangeDb_) * bar.H();
            g.DrawLine(t.textDim, scale.L, y, scale.L + 3.f, y);
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(db));
            g.DrawText(tick, buf, IRECT(scale.L + 5.f, y - 6.f, scale.R, y + 6.f));
        }
        char val[16];
        std::snprintf(val, sizeof(val), "%s", label_.c_str());
        g.DrawText(uiText(11.f, t.text), val, labelR);
    }

private:
    std::string label_;
    float rangeDb_;
    float value_ = 0.f;
};

/// Horizontal progress bar 0..1 with an optional caption.
class ProgressBarControl : public IControl {
public:
    explicit ProgressBarControl(const IRECT& bounds) : IControl(bounds) {}
    void setProgress(double f, bool active) {
        const float v = static_cast<float>(std::clamp(f, 0.0, 1.0));
        if (std::fabs(v - value_) > 0.002f || active != active_) {
            value_ = v;
            active_ = active;
            SetDirty(false);
        }
    }
    void Draw(IGraphics& g) override {
        const Theme& t = theme();
        g.FillRect(t.meterBack, mRECT);
        if (active_ || value_ >= 1.f)
            g.FillRect(active_ ? t.accent : t.accentDim, IRECT(mRECT.L, mRECT.T, mRECT.L + value_ * mRECT.W(), mRECT.B));
        g.DrawRect(t.frame, mRECT);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d %%", static_cast<int>(std::lround(value_ * 100.f)));
        g.DrawText(uiText(10.f, t.text), active_ || value_ > 0.f ? buf : "", mRECT);
    }

private:
    float value_ = 0.f;
    bool active_ = false;
};

/// Single-line text that elides long paths from the left ("…/Desktop/file.wav").
class PathTextControl : public ITextControl {
public:
    PathTextControl(const IRECT& bounds, const char* placeholder)
        : ITextControl(bounds, placeholder, uiText(11.f, theme().textDim, EAlign::Near)), placeholder_(placeholder) {}

    void setPath(const std::string& utf8) {
        path_ = utf8;
        if (path_.empty()) {
            SetText(uiText(11.f, theme().textDim, EAlign::Near));
            SetStr(placeholder_.c_str());
        } else {
            SetText(uiText(11.f, theme().text, EAlign::Near));
            SetStr(elided().c_str());
        }
        SetDirty(false);
    }
    const std::string& path() const { return path_; }
    void OnResize() override { if (!path_.empty()) SetStr(elided().c_str()); }

private:
    std::string elided() const {
        // ~6.2 px per character at 11 px Roboto; keep it simple and generous.
        const std::size_t maxChars = static_cast<std::size_t>(std::max(8.f, mRECT.W() / 6.2f));
        if (path_.size() <= maxChars)
            return path_;
        return "\xE2\x80\xA6" + path_.substr(path_.size() - (maxChars - 1));
    }
    std::string path_;
    std::string placeholder_;
};

/// Debug/documentation helper: when given a path it renders every other control into an
/// offscreen layer during the next draw and writes it as PNG (no screen-recording permission
/// needed, works for the Metal backend). Triggered by the RADIFF_SCREENSHOT environment variable.
class SnapshotControl : public IControl {
public:
    explicit SnapshotControl(const IRECT& bounds) : IControl(bounds) { mIgnoreMouse = true; }
    void request(const std::string& pngPath) { path_ = pngPath; SetDirty(false); }
    bool done() const { return done_; }
    bool failed() const { return failed_; }

    void Draw(IGraphics& g) override {
        if (path_.empty())
            return;
        const IRECT b = g.GetBounds();
        g.StartLayer(this, b);
        for (int i = 0; i < g.NControls(); ++i) {
            IControl* c = g.GetControl(i);
            if (c != this && !c->IsHidden())
                c->Draw(g);
        }
        ILayerPtr layer = g.EndLayer();
        RawBitmapData data;
        g.GetLayerBitmapData(layer, data);
        const APIBitmap* bmp = layer->GetAPIBitmap();
        const int w = bmp->GetWidth(), h = bmp->GetHeight();
        failed_ = !(data.GetSize() >= w * h * 4 && stbi_write_png(path_.c_str(), w, h, 4, data.Get(), w * 4));
        done_ = true;
        path_.clear();
    }

private:
    std::string path_;
    bool done_ = false, failed_ = false;
};

/// Filled rectangle used as a panel/header background (non-interactive).
class PanelControl : public IControl {
public:
    PanelControl(const IRECT& bounds, const IColor& color, float roundness = 4.f, const char* title = nullptr,
                 float titleHeight = 0.f)
        : IControl(bounds), color_(color), round_(roundness), title_(title ? title : ""), titleH_(titleHeight) {
        mIgnoreMouse = true;
    }
    void Draw(IGraphics& g) override {
        const Theme& t = theme();
        g.FillRoundRect(color_, mRECT, round_);
        if (titleH_ > 0.f) {
            const IRECT bar = mRECT.GetFromTop(titleH_);
            g.FillRoundRect(t.panelTitle, bar, round_);
            g.FillRect(t.panelTitle, bar.GetFromBottom(round_));
            if (!title_.empty())
                g.DrawText(uiText(12.f, t.text, EAlign::Near), title_.c_str(), bar.GetReducedFromLeft(10.f));
        }
    }

private:
    IColor color_;
    float round_;
    std::string title_;
    float titleH_;
};

} // namespace radiff::standalone
