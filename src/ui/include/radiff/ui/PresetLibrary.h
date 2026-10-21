#pragma once

#include "radiff/core/Parameters.h"
#include "radiff/presets/Preset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace radiff::ui {

/// One preset file known to the library.
struct PresetEntry {
    std::string name;            ///< display name (from the file, falling back to the file stem)
    std::filesystem::path file;
    bool factory = false;        ///< read-only factory preset (bundled) vs. user preset
};

/// Toolkit-independent preset bookkeeping for the GUI hosts (standalone app, later plug-ins):
/// where factory and user presets live, scanning, naming, dirty tracking. All file I/O goes
/// through radiff::presets so a preset saved here is byte-identical to one the CLI tool writes.
class PresetLibrary {
public:
    /// Candidate factory directories in priority order (the first that exists is used).
    void setFactoryDirectories(std::vector<std::filesystem::path> dirs);
    void setUserDirectory(std::filesystem::path dir);
    [[nodiscard]] const std::filesystem::path& userDirectory() const noexcept { return userDir_; }
    [[nodiscard]] const std::filesystem::path& factoryDirectory() const noexcept { return factoryDir_; }

    /// Re-reads both directories. Never throws; unreadable files are skipped.
    void rescan();
    [[nodiscard]] const std::vector<PresetEntry>& factory() const noexcept { return factory_; }
    [[nodiscard]] const std::vector<PresetEntry>& user() const noexcept { return user_; }
    /// factory() followed by user(), the order a menu shows them in.
    [[nodiscard]] std::vector<PresetEntry> all() const;

    /// "<name>.radvoice.json" with characters that are illegal in file names replaced.
    [[nodiscard]] static std::string fileNameFor(std::string_view presetName);
    /// Ensures the path ends with ".radvoice.json".
    [[nodiscard]] static std::filesystem::path withPresetExtension(std::filesystem::path p);
    [[nodiscard]] bool isUserFile(const std::filesystem::path& file) const;

    // --- current preset state -------------------------------------------------------------
    [[nodiscard]] const presets::Preset& current() const noexcept { return current_; }
    [[nodiscard]] presets::Preset& current() noexcept { return current_; }
    [[nodiscard]] const std::filesystem::path& currentFile() const noexcept { return currentFile_; }
    [[nodiscard]] bool currentIsFactory() const noexcept { return currentIsFactory_; }
    /// True when `live` differs from the parameters last loaded/saved (or from the defaults
    /// of a fresh preset).
    [[nodiscard]] bool isDirty(const core::ParameterSet& live) const noexcept { return live != saved_; }

    /// "New": defaults, metadata cleared, no file.
    void resetToDefaults();
    /// Loads a file into current(); on success `live` receives its parameters.
    presets::PresetResult load(const std::filesystem::path& file, core::ParameterSet& live);
    /// Saves `live` with the current metadata to `file` (creating its directory) and makes it
    /// the current file. Rescans afterwards.
    presets::PresetResult saveAs(const std::filesystem::path& file, const core::ParameterSet& live);
    /// Deletes a user preset file (never a factory one). Returns an error text or "".
    std::string deleteUserPreset(const std::filesystem::path& file);

private:
    void scanDirectory(const std::filesystem::path& dir, bool factory, std::vector<PresetEntry>& out) const;

    std::vector<std::filesystem::path> factoryCandidates_;
    std::filesystem::path factoryDir_, userDir_;
    std::vector<PresetEntry> factory_, user_;

    presets::Preset current_;
    std::filesystem::path currentFile_;
    bool currentIsFactory_ = false;
    core::ParameterSet saved_;
};

} // namespace radiff::ui
