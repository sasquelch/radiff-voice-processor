#include <doctest.h>

#include "radiff/core/Parameters.h"

using namespace radiff::core;

TEST_CASE("parameter table keys are unique and resolvable") {
    for (const auto& p : paramTable()) {
        auto id = paramIdForKey(p.key);
        REQUIRE(id.has_value());
        CHECK(*id == p.id);
        CHECK(p.minValue <= p.defaultValue);
        CHECK(p.defaultValue <= p.maxValue);
    }
    CHECK_FALSE(paramIdForKey("definitely_not_a_param").has_value());
}

TEST_CASE("parameter set clamps to range") {
    ParameterSet s;
    s.set(ParamId::OutputGainDb, 1000.0f);
    CHECK(s.get(ParamId::OutputGainDb) == paramInfo(ParamId::OutputGainDb).maxValue);
    s.set(ParamId::OutputGainDb, -1000.0f);
    CHECK(s.get(ParamId::OutputGainDb) == paramInfo(ParamId::OutputGainDb).minValue);
}

TEST_CASE("stepped parameters snap") {
    ParameterSet s;
    s.set(ParamId::Bypass, 0.7f);
    CHECK(s.get(ParamId::Bypass) == 1.0f);
    s.set(ParamId::Bypass, 0.2f);
    CHECK(s.get(ParamId::Bypass) == 0.0f);
}

TEST_CASE("default parameter set equals a fresh one") {
    ParameterSet a, b;
    CHECK(a == b);
    b.set(ParamId::InputGainDb, 1.0f);
    CHECK_FALSE(a == b);
}
