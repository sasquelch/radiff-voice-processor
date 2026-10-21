#include "radiff/ui/PresetLibrary.h"
#include "radiff/platform/Paths.h"

#include <algorithm>
#include <system_error>

namespace radiff::ui {

namespace {
bool hasPresetExtension(const std::filesystem::path& p) {
    const std::string name = platform::pathToUtf8(p.filename());
    const std::string ext = presets::kPresetFileExtension;
    return name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0;
}
} // namespace

void PresetLibrary::setFactoryDirectories(std::vector<std::filesystem::path> dirs) {
    factoryCandidates_ = std::move(dirs);
}

void PresetLibrary::setUserDirectory(std::filesystem::path dir) { userDir_ = std::move(dir); }

void PresetLibrary::scanDirectory(const std::filesystem::path& dir, bool factory, std::vector<PresetEntry>& out) const {
    out.clear();
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec))
        return;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file(ec) || !hasPresetExtension(entry.path()))
            continue;
        presets::Preset p;
        const auto r = presets::loadPresetFile(entry.path(), p);
        if (!r.ok())
            continue;
        PresetEntry e;
        e.file = entry.path();
        e.factory = factory;
        e.name = p.name;
        if (e.name.empty()) {
            std::string stem = platform::pathToUtf8(entry.path().filename());
            stem.erase(stem.size() - std::string(presets::kPresetFileExtension).size());
            e.name = stem;
        }
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const PresetEntry& a, const PresetEntry& b) { return a.name < b.name; });
}

void PresetLibrary::rescan() {
    factoryDir_.clear();
    std::error_code ec;
    for (const auto& c : factoryCandidates_)
        if (!c.empty() && std::filesystem::is_directory(c, ec)) {
            factoryDir_ = c;
            break;
        }
    scanDirectory(factoryDir_, true, factory_);
    scanDirectory(userDir_, false, user_);
}

std::vector<PresetEntry> PresetLibrary::all() const {
    std::vector<PresetEntry> v = factory_;
    v.insert(v.end(), user_.begin(), user_.end());
    return v;
}

std::string PresetLibrary::fileNameFor(std::string_view presetName) {
    std::string s;
    for (char c : presetName) {
        const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                         c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20;
        s += bad ? '_' : c;
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.'))
        s.pop_back();
    if (s.empty())
        s = "Untitled";
    return s + presets::kPresetFileExtension;
}

std::filesystem::path PresetLibrary::withPresetExtension(std::filesystem::path p) {
    if (hasPresetExtension(p))
        return p;
    std::string name = platform::pathToUtf8(p.filename());
    // "Name.json" -> "Name.radvoice.json"; "Name" -> "Name.radvoice.json"
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0)
        name.erase(name.size() - 5);
    p.replace_filename(platform::pathFromUtf8(name + presets::kPresetFileExtension));
    return p;
}

bool PresetLibrary::isUserFile(const std::filesystem::path& file) const {
    if (userDir_.empty())
        return false;
    std::error_code ec;
    const auto a = std::filesystem::weakly_canonical(file.parent_path(), ec);
    if (ec)
        return false;
    const auto b = std::filesystem::weakly_canonical(userDir_, ec);
    return !ec && a == b;
}

void PresetLibrary::resetToDefaults() {
    current_ = presets::Preset{};
    currentFile_.clear();
    currentIsFactory_ = false;
    saved_ = core::ParameterSet{};
}

presets::PresetResult PresetLibrary::load(const std::filesystem::path& file, core::ParameterSet& live) {
    presets::Preset p;
    auto r = presets::loadPresetFile(file, p);
    if (!r.ok())
        return r;
    current_ = p;
    currentFile_ = file;
    currentIsFactory_ = !isUserFile(file);
    saved_ = p.parameters;
    live = p.parameters;
    return r;
}

presets::PresetResult PresetLibrary::saveAs(const std::filesystem::path& fileIn, const core::ParameterSet& live) {
    const auto file = withPresetExtension(fileIn);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    current_.parameters = live;
    if (current_.name.empty()) {
        std::string stem = platform::pathToUtf8(file.filename());
        stem.erase(stem.size() - std::string(presets::kPresetFileExtension).size());
        current_.name = stem;
    }
    auto r = presets::savePresetFile(file, current_);
    if (!r.ok())
        return r;
    currentFile_ = file;
    currentIsFactory_ = !isUserFile(file);
    saved_ = live;
    rescan();
    return r;
}

std::string PresetLibrary::deleteUserPreset(const std::filesystem::path& file) {
    if (!isUserFile(file))
        return "Only presets in the user preset folder can be deleted.";
    std::error_code ec;
    if (!std::filesystem::remove(file, ec) || ec)
        return "Could not delete " + platform::pathToUtf8(file) + (ec ? ": " + ec.message() : std::string{});
    if (file == currentFile_) {
        currentFile_.clear();
        currentIsFactory_ = false;
    }
    rescan();
    return {};
}

} // namespace radiff::ui
