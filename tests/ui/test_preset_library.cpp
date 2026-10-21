#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "radiff/presets/Preset.h"
#include "radiff/ui/PresetLibrary.h"

#include <algorithm>
#include <filesystem>

using namespace radiff;
using core::ParamId;
namespace fs = std::filesystem;

TEST_CASE("preset library: scan factory/user folders, load, dirty tracking, save as, delete, new") {
    const fs::path tmp = fs::path(RADIFF_TEST_TMP_DIR) / "ui-presets";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    ui::PresetLibrary lib;
    lib.setFactoryDirectories({tmp / "does-not-exist", fs::path(RADIFF_FACTORY_PRESET_DIR)});
    lib.setUserDirectory(tmp / "user"); // created on first save
    lib.rescan();
    CHECK(lib.factoryDirectory() == fs::path(RADIFF_FACTORY_PRESET_DIR));
    CHECK(lib.factory().size() == 10);
    CHECK(lib.user().empty());
    CHECK(lib.all().size() == 10);

    const auto& f = lib.factory();
    auto it = std::find_if(f.begin(), f.end(), [](const ui::PresetEntry& e) { return e.name == "Broadcast Voice - Balanced"; });
    REQUIRE(it != f.end());
    CHECK(it->factory);

    core::ParameterSet live;
    auto r = lib.load(it->file, live);
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(lib.currentIsFactory());
    CHECK(lib.currentFile() == it->file);
    CHECK_FALSE(lib.isDirty(live));
    CHECK(live.get(ParamId::ExpRelativeThresholdDb) == -12.0f);

    live.set(ParamId::OutputGainDb, -3.0f);
    CHECK(lib.isDirty(live));

    // Save As into the (not yet existing) user folder; illegal name characters are replaced.
    const std::string fileName = ui::PresetLibrary::fileNameFor("My Voice: Test/1");
    CHECK(fileName.ends_with(".radvoice.json"));
    CHECK(fileName.find('/') == std::string::npos);
    CHECK(fileName.find(':') == std::string::npos);
    lib.current().name = "My Voice";
    const fs::path userFile = lib.userDirectory() / fileName;
    auto s = lib.saveAs(userFile, live);
    REQUIRE_MESSAGE(s.ok(), s.error);
    CHECK(fs::exists(userFile));
    CHECK_FALSE(lib.isDirty(live));
    CHECK_FALSE(lib.currentIsFactory());
    CHECK(lib.isUserFile(userFile));
    CHECK(lib.user().size() == 1);
    CHECK(lib.all().size() == 11);

    // The file is a normal preset the CLI/presets library reads back identically.
    presets::Preset p;
    REQUIRE(presets::loadPresetFile(userFile, p).ok());
    CHECK(p.name == "My Voice");
    CHECK(p.parameters == live);
    CHECK(presets::toJson(p) == presets::toJson(lib.current()).substr(0, presets::toJson(p).size()));

    CHECK(ui::PresetLibrary::withPresetExtension("abc").filename() == "abc.radvoice.json");
    CHECK(ui::PresetLibrary::withPresetExtension("abc.radvoice.json").filename() == "abc.radvoice.json");

    // Delete: factory presets are refused, user presets go.
    CHECK_FALSE(lib.deleteUserPreset(it->file).empty());
    CHECK(fs::exists(it->file));
    CHECK(lib.deleteUserPreset(userFile).empty());
    lib.rescan();
    CHECK_FALSE(fs::exists(userFile));
    CHECK(lib.user().empty());

    lib.resetToDefaults();
    CHECK(lib.current().name.empty());
    CHECK(lib.currentFile().empty());
    CHECK_FALSE(lib.isDirty(core::ParameterSet{}));
}
